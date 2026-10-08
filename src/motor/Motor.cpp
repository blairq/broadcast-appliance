// ============================================================================
// src/motor/Motor.cpp — ver Motor.hpp
// ============================================================================
#include "Motor.hpp"
#include "Candado.hpp"
#include "OrigenRed.hpp"

#include <gst/net/net.h>

#include <gst/app/gstappsink.h>

#include <algorithm>
#include <chrono>

namespace ba::motor {

using json = nlohmann::json;

namespace {
constexpr std::int64_t kDirectoNs = 40 * GST_MSECOND; ///< lo que espera el bus «directo»
}

Motor::Motor(Config c) : cfg_(std::move(c)) {
    fijarColorSalida(cfg_.lienzo);
    reloj_ = std::make_unique<Reloj>(cfg_.reloj, cfg_.relojServidor, cfg_.relojPuerto, cfg_.relojDominio);
    reloj_->ponerTope(cfg_.topeMs);
}

Motor::~Motor() { detener(); }

std::string Motor::iniciar() {
    BA_CANDADO(mu_);
    if (andando()) return {};
    Mezcla::Opciones op;
    op.limitador = true;
    op.loudness = true;
    programa_ = std::make_unique<Mezcla>(*reloj_, "programa", op);
    if (!programa_->anda()) return "el bus PROGRAMA no arrancó: " + programa_->error();
    escucha_ = std::make_unique<Escucha>(*reloj_, cfg_.sinkEscucha);
    if (cfg_.video) {
        Composicion::Opciones vop;
        vop.gpu = cfg_.videoGpu;
        Composicion::Opciones pop = vop;
        pop.dsk = true; // el PROGRAMA con DSK y clean feed
        vPrograma_ = std::make_unique<Composicion>(*reloj_, cfg_.lienzo, "programa", pop);
        vPreview_ = std::make_unique<Composicion>(*reloj_, cfg_.lienzo, "preview", vop);
        if (!vPrograma_->anda()) registrar(Nivel::Error, "el video del PROGRAMA no arrancó: " + vPrograma_->error());
    }
    vistas_ = std::make_unique<Vistas>(*reloj_, [this](const std::string& clave, std::int64_t* lat) -> std::string {
        // (lo llaman pedir y tic, con mu_ del motor ya tomado o no: el recursive_mutex lo permite)
        BA_CANDADO(mu_);
        if (clave == "programa" || clave == "preview" || clave == "multiview" || clave == "limpio")
            return productorDeVideo(clave, lat);
        if (clave.rfind("escena:", 0) == 0) {
            auto it = vEscenas_.find(clave.substr(7));
            if (it == vEscenas_.end() || !it->second.comp) return {};
            if (lat != nullptr) *lat = it->second.comp->latenciaSalidaNs();
            return it->second.comp->productor();
        }
        if (clave.rfind("entrada:", 0) == 0) {
            auto it = canales_.find(clave.substr(8));
            if (it == canales_.end() || !it->second.entrada->daVideo()) return {};
            if (lat != nullptr) *lat = std::max<std::int64_t>(reloj_->latencia(), it->second.entrada->latenciaPropia());
            return it->second.entrada->productorVideo();
        }
        return {};
    });
    // El programa para el gateway (lo que oyen los invitados en modo «programa»).
    if (!cfg_.prefijoAnillos.empty() && hayFabrica("baanillosink")) {
        const std::string d = "intersrc producer-name=" + entreComillas(programa_->productor()) +
                              " ! queue max-size-time=2000000000 max-size-buffers=0 max-size-bytes=0 ! baanillosink canal=" +
                              entreComillas(anillo("ContentForReturn"));
        GError* e = nullptr;
        puente_ = gst_parse_launch(d.c_str(), &e);
        if (e != nullptr) {
            registrar(Nivel::Aviso, std::string("el programa para el gateway: ") + e->message);
            g_error_free(e);
            if (puente_ != nullptr) gst_object_unref(puente_);
            puente_ = nullptr;
        } else {
            reloj_->adoptar(puente_);
            puenteLatNs_ = programa_->latenciaSalidaNs() + kMargenMezclaNs;
            gst_pipeline_set_latency(GST_PIPELINE(puente_), static_cast<GstClockTime>(puenteLatNs_));
            gst_element_set_state(puente_, GST_STATE_PLAYING);
        }
    }
    seguir_ = true;
    hilo_ = std::thread([this]() { bucle(); });
    hiloCuadros_ = std::thread([this]() { cuadros(); });
    return {};
}

void Motor::detener() {
    seguir_ = false;
    colaCv_.notify_all();
    buzonesCv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
    if (hiloCuadros_.joinable()) hiloCuadros_.join();
    {
        std::lock_guard<std::mutex> l(buzonesM_);
        deApp_.clear();
        noDeApp_.clear();
        todas_.clear();
        buzones_.clear();
    }
    hiloId_.store(std::thread::id());
    {
        // Lo que quedó en la cola se descarta: el motor se apaga.
        std::lock_guard<std::mutex> l(colaM_);
        if (!cola_.empty())
            registrar(Nivel::Info, "al apagar quedaron " + std::to_string(cola_.size()) + " órdenes sin correr");
        cola_.clear();
    }
    {
        // Las composiciones que se estaban desarmando usan el reloj del motor.
        std::lock_guard<std::mutex> l(desarmesM_);
        for (auto& f : desarmes_) f.wait();
        desarmes_.clear();
    }
    BA_CANDADO(mu_);
    if (relojPublicado_ != nullptr) {
        gst_object_unref(relojPublicado_);
        relojPublicado_ = nullptr;
    }
    for (auto& [n, t] : tomasEntrada_) {
        gst_element_set_state(t->pipeline, GST_STATE_NULL);
        gst_object_unref(t->pipeline);
    }
    tomasEntrada_.clear();
    vistas_.reset();
    if (tomaAudio_ != nullptr) {
        gst_element_set_state(tomaAudio_, GST_STATE_NULL);
        gst_object_unref(tomaAudio_);
        tomaAudio_ = nullptr;
    }
    vEscenas_.clear();
    dskEscenas_.clear();
    vMulti_.reset();
    mvAux_.clear();
    escucha_.reset();
    vPreview_.reset();
    vPrograma_.reset();
    retornos_.clear();
    if (puente_ != nullptr) {
        gst_element_set_state(puente_, GST_STATE_NULL);
        gst_object_unref(puente_);
        puente_ = nullptr;
    }
    canales_.clear();
    pistas_.clear();
    directo_.reset();
    seleccion_.reset();
    programa_.reset();
}

void Motor::bucle() {
    hiloId_.store(std::this_thread::get_id());
    while (seguir_) {
        {
            Tramo t("cámaras de red (sin el candado del motor, con el suyo)");
            origen_red::tic(); // las cámaras de red compartidas (tiene su propio candado)
        }
        static int vueltas = 0;
        const bool revisarUso = ++vueltas % 10 == 0; // cada ~200 ms
        {
            // Con el candado vigilado y cada tramo con nombre: una vuelta que
            // tarda dice cuál de sus partes fue (ver Candado.hpp).
            BA_CANDADO(mu_);
            correrOrdenes();
            if (revisarUso) {
                Tramo t("actualizarUso");
                actualizarUso();
            }
            for (auto& [id, c] : canales_) {
                Tramo t("tic de las entradas");
                c.entrada->tic();
            }
            {
                Tramo t("tic de los buses de audio");
                if (programa_) programa_->tic();
                if (directo_) directo_->tic();
                if (seleccion_) seleccion_->tic();
                for (auto& [n, p] : pistas_) p->tic();
                for (auto& [n, r] : retornos_)
                    if (r.bus) r.bus->tic();
            }
            {
                Tramo t("tic de las composiciones");
                if (vPrograma_) vPrograma_->tic();
                if (vPreview_) vPreview_->tic();
                if (vMulti_) vMulti_->tic();
                for (auto& [n, c] : vEscenas_)
                    if (c.comp) c.comp->tic();
            }
            Tramo tResto("resto de la vuelta (toma de audio, multiview, puente, escucha)");
            if (tomaAudio_ != nullptr) {
                GstBus* bus = gst_element_get_bus(tomaAudio_);
                while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                gst_object_unref(bus);
            }
            for (auto& [n, a] : mvAux_) a->tic();
            if (vistas_) {
                Tramo t("tic de las vistas");
                vistas_->tic();
            }
            if (revisarUso || fotoPedida_.exchange(false)) {
                Tramo t("foto del estado");
                publicarInstantanea();
            }
            // Los que consumen el programa esperan lo que tarda en salir.
            if (puente_ != nullptr && programa_) {
                const std::int64_t lat = programa_->latenciaSalidaNs() + kMargenMezclaNs;
                if (lat > puenteLatNs_ + static_cast<std::int64_t>(5 * GST_MSECOND) ||
                    lat < puenteLatNs_ - static_cast<std::int64_t>(60 * GST_MSECOND)) {
                    puenteLatNs_ = lat;
                    recalcularLatenciaAparte(puente_, [p = refCompartida(puente_), lat]() {
                        gst_pipeline_set_latency(GST_PIPELINE(p.get()), static_cast<GstClockTime>(lat));
                    });
                }
                GstBus* bus = gst_element_get_bus(puente_);
                while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                gst_object_unref(bus);
            }
            if (escucha_) {
                std::int64_t lat = 0;
                if (escuchaModo_ == "programa" && programa_)
                    lat = programa_->latenciaSalidaNs();
                else if (escuchaModo_ == "directo" && directo_)
                    lat = directo_->latenciaSalidaNs();
                else if (escuchaModo_ == "pfl")
                    lat = reloj_->latencia();
                else if (escuchaModo_ == "seleccion" && seleccion_)
                    lat = seleccion_->latenciaSalidaNs();
                escucha_->tic(lat);
            }
        }
        // 20 ms, o menos si llega una orden.
        std::unique_lock<std::mutex> lc(colaM_);
        colaCv_.wait_for(lc, std::chrono::milliseconds(20), [this]() { return !cola_.empty() || !seguir_.load(); });
    }
}

void Motor::enBus(Mezcla& m, const std::string& id, const Canal& c, bool incluir, int fundidoMs) {
    if (!incluir || !c.entrada->daAudio()) {
        m.quitar(id);
        return;
    }
    m.agregar(id);
    m.ponerNivel(id, c.nivel);
    m.ponerMute(id, c.mute);
    m.ponerEscena(id, c.global || c.enEscena ? 1.0 : 0.0, fundidoMs);
}

Mezcla* Motor::pista(int n, bool crear) {
    auto it = pistas_.find(n);
    if (it != pistas_.end()) return it->second.get();
    if (!crear) return nullptr;
    Mezcla::Opciones op;
    op.medidores = false;
    auto m = std::make_unique<Mezcla>(*reloj_, "pista" + std::to_string(n), op);
    Mezcla* p = m.get();
    pistas_[n] = std::move(m);
    return p;
}

void Motor::aplicar(const std::string& id, int fundidoMs) {
    auto it = canales_.find(id);
    if (it == canales_.end()) return;
    const Canal& c = it->second;
    const bool alPrograma = (c.pistas & 1u) != 0;
    if (programa_) enBus(*programa_, id, c, alPrograma, fundidoMs);
    for (int n = 2; n <= 6; ++n) {
        const bool va = (c.pistas & (1u << (n - 1))) != 0;
        if (Mezcla* p = pista(n, va)) enBus(*p, id, c, va, fundidoMs);
    }
    for (auto& [n, r] : retornos_)
        if (r.bus) enBus(*r.bus, id, c, alPrograma && r.propia != id, fundidoMs);
    if (directo_) enBus(*directo_, id, c, c.global, 0);
    if (seleccion_) enSeleccion(id, c);
}

std::string Motor::agregarEntradaYa(const std::string& id, const json& ajustes, const json& opcionesPedidas) {
    BA_CANDADO(mu_);
    const json opciones = opcionesPedidas.is_object() ? opcionesPedidas : json::object(); // {} llega como null
    if (id.empty()) return "falta el id";
    if (id.find(':') != std::string::npos) return "el id no puede tener «:»";
    if (canales_.count(id) > 0) return "ya hay una entrada «" + id + "»";
    {
        // La anterior con este id todavía se está desarmando: se la espera (acá,
        // en el hilo del motor), porque sus productores llevan los mismos nombres.
        std::shared_future<void> antes;
        {
            std::lock_guard<std::mutex> l(desarmandoM_);
            auto d = desarmando_.find(id);
            if (d != desarmando_.end()) {
                antes = d->second;
                desarmando_.erase(d);
            }
        }
        if (antes.valid()) antes.wait();
    }
    Canal c;
    c.global = opciones.value("global", false);
    c.enEscena = opciones.value("en_escena", c.global);
    c.interna = opciones.value("interna", false);
    c.nivel = opciones.value("nivel", 1.0);
    c.mute = opciones.value("mute", false);
    if (opciones.contains("pistas") && opciones["pistas"].is_array()) {
        c.pistas = 0;
        for (const auto& p : opciones["pistas"])
            if (p.is_number_integer() && p.get<int>() >= 1 && p.get<int>() <= 6) c.pistas |= 1u << (p.get<int>() - 1);
    }
    if (!opciones.value("audio", true)) c.pistas = 0;
    c.entrada = std::make_shared<Entrada>(*reloj_, cfg_.lienzo, id, ajustes.is_object() ? ajustes : json::object());
    indexarDeApp(id, c.entrada);
    canales_[id] = std::move(c);
    aplicar(id, 0);
    actualizarMultiview();
    return {};
}

std::string Motor::actualizarEntradaYa(const std::string& id, const json& ajustes) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    // El rearmado va al trabajador de la entrada; lo que depende de los ajustes
    // nuevos (si es de la app, si tiene audio) vuelve a la cola del motor al terminar.
    it->second.entrada->actualizar(ajustes, [this, id]() {
        // Siempre a la cola (nunca directo): al apagar, detener tiene mu_ y espera
        // a este trabajo; la cola la descarta.
        encolar("actualizarEntrada (aplicar)", [this, id]() {
            auto e = canales_.find(id);
            if (e == canales_.end()) return std::string();
            indexarDeApp(id, e->second.entrada); // pudo dejar de ser (o pasar a ser) de la app
            aplicar(id, 0);                      // pudo pasar a tener (o dejar de tener) audio
            return std::string();
        });
    });
    return {};
}

std::string Motor::publicarReloj(int puerto) {
    BA_CANDADO(mu_);
    if (relojPublicado_ != nullptr) {
        gst_object_unref(relojPublicado_);
        relojPublicado_ = nullptr;
    }
    if (puerto <= 0) return {};
    GstNetTimeProvider* p = gst_net_time_provider_new(reloj_->clock(), nullptr, puerto);
    if (p == nullptr) return "no se pudo publicar el reloj en el puerto " + std::to_string(puerto);
    relojPublicado_ = GST_OBJECT(p);
    return {};
}

std::string Motor::propiedadEntradaYa(const std::string& id, const std::string& elemento, const std::string& prop,
                                    const std::string& valor) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    return it->second.entrada->propiedad(elemento, prop, valor);
}

json Motor::propiedadesEntrada(const std::string& id, const std::string& elemento) const {
    // Sin el candado del motor (lo lee la interfaz): la entrada por el índice.
    std::shared_ptr<Entrada> e;
    {
        std::lock_guard<std::mutex> l(const_cast<std::mutex&>(buzonesM_));
        auto it = todas_.find(id);
        if (it != todas_.end()) e = it->second;
    }
    json o = e ? e->leerPropiedades(elemento) : json::object();
    const_cast<Motor*>(this)->soltarEntrada(std::move(e));
    return o;
}

std::string Motor::controlEntradaYa(const std::string& id, const json& campos) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.entrada->control(campos);
    return {};
}

std::string Motor::quitarEntradaYa(const std::string& id) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    if (programa_) programa_->quitar(id);
    if (directo_) directo_->quitar(id);
    if (seleccion_) seleccion_->quitar(id);
    for (auto& [n, p] : pistas_) p->quitar(id);
    for (auto& [n, r] : retornos_)
        if (r.bus) r.bus->quitar(id);
    if (escuchaModo_ == "pfl" && escuchaId_ == id) escuchar("apagado");
    indexarDeApp(id, nullptr);
    std::shared_ptr<Entrada> e = std::move(it->second.entrada);
    canales_.erase(it);
    soltarEntrada(std::move(e));
    actualizarMultiview();
    return {};
}

std::string Motor::ponerNivelYa(const std::string& id, double lineal) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.nivel = std::clamp(lineal, 0.0, 4.0);
    aplicar(id, 0);
    return {};
}

std::string Motor::ponerMuteYa(const std::string& id, bool mute) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.mute = mute;
    for (auto& [n, t] : tomasEntrada_)
        if (t->id == id) t->mute = mute;
    aplicar(id, 0);
    return {};
}

std::string Motor::ponerPistasYa(const std::string& id, const std::vector<int>& pistas) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    unsigned m = 0;
    for (int p : pistas) {
        if (p < 1 || p > 6) return "pista fuera de rango (1…6)";
        m |= 1u << (p - 1);
    }
    it->second.pistas = m;
    aplicar(id, 0);
    return {};
}

void Motor::escenaAlAireYa(const std::vector<std::string>& ids, int fundidoMs) {
    BA_CANDADO(mu_);
    const std::set<std::string> al(ids.begin(), ids.end());
    for (auto& [id, c] : canales_) {
        if (c.global) continue;
        const bool antes = c.enEscena;
        c.enEscena = al.count(id) > 0;
        if (antes != c.enEscena) aplicar(id, fundidoMs);
    }
}

std::string Motor::definirEscenaYa(const json& j) {
    BA_CANDADO(mu_);
    if (!j.is_object()) return "la escena tiene que ser un objeto {nombre, capas}";
    Escena e = Escena::desdeJson(j);
    if (e.nombre.empty()) return "falta el nombre de la escena";
    escenas_[e.nombre] = e;
    // Al aire o armada: se actualiza andando (posición, tamaño, recorte, capas).
    if (e.nombre == alAire_ && vPrograma_) {
        vPrograma_->mostrar(e);
        std::vector<std::string> ids;
        for (const auto& c : e.capas) ids.push_back(c.entrada);
        escenaAlAire(ids, 0);
    }
    if (e.nombre == armada_ && vPreview_) vPreview_->mostrar(e);
    if (auto it = vEscenas_.find(e.nombre); it != vEscenas_.end() && it->second.comp) {
        // Otro lienzo u otros filtros (no solo sus valores): se rearma; los consumidores
        // la siguen leyendo por el mismo nombre.
        const Lienzo l = lienzoDe(e);
        Composicion& c = *it->second.comp;
        if (c.lienzo().ancho != l.ancho || c.lienzo().alto != l.alto || !c.ponerFiltros(e.filtros)) {
            std::unique_ptr<Composicion> vieja = std::move(it->second.comp);
            Composicion::Opciones op;
            op.filtros = e.filtros;
            it->second.comp = std::make_unique<Composicion>(*reloj_, l, "escena:" + e.nombre, op);
            desarmarAparte(std::move(vieja));
        }
        it->second.comp->mostrar(e);
    }
    actualizarUso();
    return {};
}

std::string Motor::quitarEscenaYa(const std::string& nombre) {
    BA_CANDADO(mu_);
    if (escenas_.erase(nombre) == 0) return "no hay una escena «" + nombre + "»";
    if (armada_ == nombre) {
        armada_.clear();
        if (vPreview_) vPreview_->mostrar(Escena{"", {}});
    }
    return {};
}

std::string Motor::armarYa(const std::string& nombre) {
    BA_CANDADO(mu_);
    if (nombre.empty()) { // nada armado
        armada_.clear();
        if (vPreview_) vPreview_->mostrar(Escena{"", {}}, "corte", 0);
        actualizarUso();
        return {};
    }
    auto it = escenas_.find(nombre);
    if (it == escenas_.end()) return "no hay una escena «" + nombre + "»";
    armada_ = nombre;
    if (vPreview_) vPreview_->mostrar(it->second, "corte", 0);
    actualizarMultiview();
    actualizarUso(); // lo armado ya corre entero: el TAKE no la agarra a 2 cuadros/s
    return {};
}

void Motor::actualizarUso() {
    std::set<std::string> uso, leidos;
    const auto sumar = [](std::set<std::string>& a, const std::set<std::string>& s) { a.insert(s.begin(), s.end()); };
    for (Composicion* c : {vPrograma_.get(), vPreview_.get(), vMulti_.get()})
        if (c != nullptr) {
            sumar(uso, c->entradasEnUso());
            sumar(leidos, c->productoresEnUso());
        }
    if (vistas_) {
        sumar(uso, vistas_->entradasEnUso());
        sumar(leidos, vistas_->productoresEnUso());
    }
    // Una composición interna (un compositor, una escena del DSK, la vista de
    // una escena) cuenta solo si algo en uso la lee: si no, sus capas también
    // descansan. En cadena (un compositor dentro de otro).
    std::set<const Composicion*> vistas;
    for (bool cambio = true; cambio;) {
        cambio = false;
        for (const auto& [n, c] : vEscenas_) {
            if (!c.comp || vistas.count(c.comp.get()) || !leidos.count(c.comp->productor())) continue;
            vistas.insert(c.comp.get());
            sumar(uso, c.comp->entradasEnUso());
            sumar(leidos, c.comp->productoresEnUso());
            cambio = true;
        }
    }
    for (auto& [id, c] : canales_) c.entrada->ponerEnUso(uso.count(id) > 0);
}

std::string Motor::alAireYa(const std::string& nombre, const std::string& transicion, int ms) {
    BA_CANDADO(mu_);
    auto it = escenas_.find(nombre);
    if (it == escenas_.end()) return "no hay una escena «" + nombre + "»";
    if (transicion != "corte" && transicion != "mezcla" && transicion != "empuje" && transicion != "cortina")
        return "transición: corte, mezcla, empuje o cortina";
    alAire_ = nombre;
    if (vPrograma_) vPrograma_->mostrar(it->second, transicion, ms);
    actualizarMultiview();
    // El audio de la escena la sigue, con el mismo fundido.
    std::vector<std::string> ids;
    for (const auto& c : it->second.capas)
        if (c.visible) ids.push_back(c.entrada);
    escenaAlAire(ids, transicion == "corte" ? 0 : ms);
    actualizarUso(); // las que entran, a todos sus cuadros ya
    return {};
}

std::string Motor::takeYa(const std::string& transicion, int ms) {
    BA_CANDADO(mu_);
    if (armada_.empty()) return "no hay nada armado en el preview";
    const std::string antes = alAire_;
    const std::string e = alAire(armada_, transicion, ms);
    if (!e.empty()) return e;
    // Lo que estaba al aire queda armado (el take intercambia, como una mesa de corte).
    if (!antes.empty() && escenas_.count(antes) > 0) armar(antes);
    return {};
}

std::string Motor::dskYa(const json& capas, int fundidoMs) {
    BA_CANDADO(mu_);
    if (!vPrograma_) return "el motor está sin video";
    // Una capa puede ser una entrada o una ESCENA entera ({"escena": "Zócalo"}).
    json lista = capas.is_array() ? capas : json::array();
    std::set<std::string> escenasUsadas;
    for (auto& c : lista) {
        if (!c.is_object() || !c.contains("escena")) continue;
        const std::string n = c.value("escena", std::string());
        if (escenas_.count(n) == 0) return "no hay una escena «" + n + "»";
        escenasUsadas.insert(n);
        if (c.value("entrada", std::string()).empty()) c["entrada"] = "escena:" + n;
    }
    std::vector<Capa> v = Escena::desdeJson(json{{"nombre", "dsk"}, {"capas", lista}}).capas;
    for (auto& c : v) {
        if (c.entrada.rfind("escena:", 0) == 0) continue;
        if (canales_.count(c.entrada) == 0) return "no hay una entrada «" + c.entrada + "»";
    }
    // Las escenas del DSK: se piden las nuevas (antes de soltar las viejas, así no se rearman).
    for (const auto& n : escenasUsadas)
        if (dskEscenas_.count(n) == 0) usarEscena(n);
    for (auto& c : v)
        if (c.entrada.rfind("escena:", 0) == 0) {
            auto it = vEscenas_.find(c.entrada.substr(7));
            if (it != vEscenas_.end() && it->second.comp) c.productor = it->second.comp->productor();
        }
    const bool ok = vPrograma_->ponerDsk(v, fundidoMs);
    for (const auto& n : dskEscenas_)
        if (escenasUsadas.count(n) == 0) soltarEscena(n);
    dskEscenas_ = escenasUsadas;
    return ok ? std::string() : "el PROGRAMA no tiene DSK";
}

std::string Motor::productorDeVideo(const std::string& bus, std::int64_t* latenciaNs) {
    BA_CANDADO(mu_);
    if (bus == "limpio" && vPrograma_) {
        if (latenciaNs != nullptr) *latenciaNs = vPrograma_->latenciaSalidaNs();
        return vPrograma_->productorLimpio();
    }
    Composicion* c = bus == "programa"    ? vPrograma_.get()
                     : bus == "preview"   ? vPreview_.get()
                     : bus == "multiview" ? vMulti_.get()
                                          : nullptr;
    if (c == nullptr) return {};
    if (latenciaNs != nullptr) *latenciaNs = c->latenciaSalidaNs();
    return c->productor();
}

std::string Motor::multiviewYa(bool activo, int ancho, int alto) {
    BA_CANDADO(mu_);
    if (!activo) {
        vMulti_.reset();
        mvAux_.clear();
        return {};
    }
    if (!cfg_.video) return "el motor está sin video";
    mvAncho_ = std::clamp(ancho, 320, 3840) & ~1;
    mvAlto_ = std::clamp(alto, 180, 2160) & ~1;
    Lienzo lz = cfg_.lienzo;
    lz.ancho = mvAncho_;
    lz.alto = mvAlto_;
    vMulti_ = std::make_unique<Composicion>(*reloj_, lz, "multiview");
    if (!vMulti_->anda()) return "el multiview no arrancó: " + vMulti_->error();
    // Los tres colores del tally, compartidos por todos los recuadros.
    for (const auto& [n, c] : std::vector<std::pair<std::string, std::string>>{
             {"mv_rojo", "#E0242F"}, {"mv_verde", "#2FBF3A"}, {"mv_gris", "#3A3A40"}})
        mvAux_[n] = std::make_unique<Entrada>(*reloj_, lz, n, json{{"tipo", "color"}, {"color", c}, {"ancho", 64}, {"alto", 36}});
    actualizarMultiview();
    return {};
}

void Motor::actualizarMultiview() {
    if (!vMulti_) return;
    const int W = mvAncho_, H = mvAlto_, borde = std::max(2, H / 180);
    std::set<std::string> alAire, armada;
    if (auto it = escenas_.find(alAire_); it != escenas_.end())
        for (const auto& c : it->second.capas) alAire.insert(c.entrada);
    if (auto it = escenas_.find(armada_); it != escenas_.end())
        for (const auto& c : it->second.capas) armada.insert(c.entrada);
    Escena e;
    e.nombre = "multiview";
    Lienzo lz = cfg_.lienzo;
    const auto nombre = [&](const std::string& celda, const std::string& texto, int x, int y, int w, int h) {
        const std::string id = "mv_nombre_" + celda;
        const json aj{{"tipo", "texto"}, {"texto", texto}, {"tamano", std::max(9, h / 12)}, {"ancho", w}, {"alto", h / 5},
                      {"vertical", "abajo"}, {"color", "#FFFFFF"}};
        auto& p = mvAux_[id];
        if (!p) p = std::make_unique<Entrada>(*reloj_, lz, id, aj);
        Capa k;
        k.entrada = id;
        k.x = x;
        k.y = y + h - h / 5;
        k.ancho = w;
        k.alto = h / 5;
        k.orden = 3;
        e.capas.push_back(k);
    };
    const auto celda = [&](const std::string& id, const std::string& productor, const std::string& texto, const char* tally,
                           int x, int y, int w, int h) {
        Capa t;
        t.entrada = "tally_" + id;
        t.productor = std::string(tally) + ":video";
        t.x = x;
        t.y = y;
        t.ancho = w;
        t.alto = h;
        t.orden = 1;
        e.capas.push_back(t);
        Capa c;
        c.entrada = "celda_" + id;
        c.productor = productor;
        c.x = x + borde;
        c.y = y + borde;
        c.ancho = w - 2 * borde;
        c.alto = h - 2 * borde;
        c.orden = 2;
        e.capas.push_back(c);
        nombre(id, texto, x + borde, y + borde, w - 2 * borde, h - 2 * borde);
    };
    celda("preview", "video:preview", "PREVIEW" + (armada_.empty() ? std::string() : " · " + armada_), "mv_verde", 0, 0, W / 2, H / 2);
    celda("programa", "video:programa", "PROGRAMA" + (alAire_.empty() ? std::string() : " · " + alAire_), "mv_rojo", W / 2, 0, W / 2, H / 2);
    int i = 0;
    for (const auto& [id, c] : canales_) {
        if (!c.entrada->daVideo() || c.interna || i >= 8) continue;
        const char* tally = alAire.count(id) > 0 ? "mv_rojo" : armada.count(id) > 0 ? "mv_verde" : "mv_gris";
        celda(id, c.entrada->productorVideo(), c.entrada->estado().value("nombre", id), tally, (i % 4) * W / 4,
              H / 2 + (i / 4) * H / 4, W / 4, H / 4);
        ++i;
    }
    vMulti_->mostrar(e);
}

std::string Motor::verPedirYa(const std::string& clave, int ancho, int alto) {
    // Siempre en este orden: el lock del motor y después el de las vistas (el hilo
    // del motor hace lo mismo en el tic; al revés se trababan).
    BA_CANDADO(mu_);
    if (!vistas_) return "el motor no está andando";
    const std::string b = clave.substr(0, clave.find('@'));
    if (b.rfind("escena:", 0) == 0 && usarEscena(b.substr(7)).empty()) return "no hay una escena «" + b.substr(7) + "»";
    const std::string e = vistas_->pedir(clave, ancho, alto);
    actualizarUso();
    return e;
}

void Motor::verSoltarYa(const std::string& clave) {
    BA_CANDADO(mu_);
    if (vistas_) vistas_->soltar(clave);
    const std::string b = clave.substr(0, clave.find('@'));
    if (b.rfind("escena:", 0) == 0) soltarEscena(b.substr(7));
}

void Motor::verAvisar(const std::string& clave, std::function<void(const Cuadro&)> fn) {
    // Soltar el aviso es YA (quien lo pide puede estar por morir: no se lo puede
    // llamar después) y sin el candado del motor: las vistas tienen el suyo. Y
    // también en la cola, para que no lo pise un aviso que estaba esperando.
    // Ponerlo va a la cola: tiene que llegar después del verPedir que la crea.
    if (!fn && vistas_) vistas_->avisar(clave, nullptr);
    orden("verAvisar", [this, clave, fn = std::move(fn)]() mutable {
        if (vistas_) vistas_->avisar(clave, std::move(fn));
        return std::string();
    });
}

// EL BUZÓN DE CUADROS. Lo que pinta la app (rótulos, gráficos, la pizarra) llega
// del hilo de la interfaz. Empujarlo a la entrada toma el candado de la entrada,
// que se tiene mientras se rearma (cambios de estado): la interfaz esperaba eso.
// Ahora deja el cuadro en el buzón (una copia, el último gana) y vuelve; el hilo
// de cuadros se lo entrega a la entrada, y si alguien espera, es él.
bool Motor::empujarCuadro(const std::string& id, const std::uint8_t* bgra, int ancho, int alto, int stride) {
    if (bgra == nullptr || ancho <= 0 || alto <= 0) return false;
    {
        std::lock_guard<std::mutex> l(buzonesM_);
        // Una que todavía no existe (su agregarEntrada está en la cola) guarda el
        // cuadro: se lo entrega apenas aparece. Un rótulo quieto se pinta una vez.
        if (noDeApp_.count(id) > 0) return false;
        Buzon& b = buzones_[id];
        const std::size_t fila = static_cast<std::size_t>(ancho) * 4;
        b.px.resize(fila * static_cast<std::size_t>(alto));
        for (int y = 0; y < alto; ++y)
            std::memcpy(b.px.data() + fila * static_cast<std::size_t>(y), bgra + static_cast<std::size_t>(stride) * y, fila);
        b.ancho = ancho;
        b.alto = alto;
        b.hay = true;
    }
    buzonesCv_.notify_one();
    return true;
}

void Motor::soltarEntrada(std::shared_ptr<Entrada>&& e) {
    if (!e) return;
    // Sacada del índice, nadie más puede tomarla: si es la única, es la última.
    if (e.use_count() == 1) {
        const std::string id = e->id();
        std::shared_future<void> f = desarmarAparte([x = std::move(e)]() mutable { x.reset(); });
        std::lock_guard<std::mutex> l(desarmandoM_);
        desarmando_[id] = f;
        return;
    }
    e.reset();
}

void Motor::indexarDeApp(const std::string& id, const std::shared_ptr<Entrada>& e) {
    {
        std::lock_guard<std::mutex> l(buzonesM_);
        if (e)
            todas_[id] = e;
        else
            todas_.erase(id);
        if (e && e->deApp()) {
            deApp_[id] = e;
            noDeApp_.erase(id);
        } else {
            deApp_.erase(id);
            buzones_.erase(id);
            if (e)
                noDeApp_.insert(id);
            else
                noDeApp_.erase(id);
        }
    }
    buzonesCv_.notify_one(); // lo que esperaba en el buzón, ahora tiene a quién
}

void Motor::cuadros() {
    std::vector<std::pair<std::shared_ptr<Entrada>, Buzon>> tanda;
    while (seguir_.load()) {
        {
            std::unique_lock<std::mutex> l(buzonesM_);
            buzonesCv_.wait_for(l, std::chrono::milliseconds(200), [this]() {
                if (!seguir_.load()) return true;
                for (const auto& [id, b] : buzones_)
                    if (b.hay && deApp_.count(id) > 0) return true;
                return false;
            });
            for (auto& [id, b] : buzones_) {
                if (!b.hay) continue;
                auto e = deApp_.find(id);
                if (e == deApp_.end()) continue; // todavía no existe: espera en el buzón
                tanda.emplace_back(e->second, b);
                b.hay = false;
            }
        }
        for (auto& [e, b] : tanda) {
            e->empujar(b.px.data(), b.ancho, b.alto, b.ancho * 4);
            soltarEntrada(std::move(e));
        }
        tanda.clear();
    }
}

Lienzo Motor::lienzoDe(const Escena& e) const {
    Lienzo l = cfg_.lienzo;
    if (e.lienzoAncho > 0 && e.lienzoAlto > 0) {
        l.ancho = e.lienzoAncho;
        l.alto = e.lienzoAlto;
    }
    return l;
}

std::string Motor::usarEscenaYa(const std::string& nombre) {
    BA_CANDADO(mu_);
    auto e = escenas_.find(nombre);
    if (e == escenas_.end() || !cfg_.video) return {};
    CompEscena& c = vEscenas_[nombre];
    if (!c.comp) {
        Composicion::Opciones op;
        op.filtros = e->second.filtros;
        c.comp = std::make_unique<Composicion>(*reloj_, lienzoDe(e->second), "escena:" + nombre, op);
        c.comp->mostrar(e->second);
    }
    ++c.pedidos;
    return c.comp->productor();
}

void Motor::soltarEscenaYa(const std::string& nombre) {
    BA_CANDADO(mu_);
    auto it = vEscenas_.find(nombre);
    if (it == vEscenas_.end()) return;
    if (--it->second.pedidos > 0) return;
    std::unique_ptr<Composicion> comp = std::move(it->second.comp);
    vEscenas_.erase(it);
    desarmarAparte(std::move(comp));
}

// SE DESARMA EN OTRO HILO, sin el candado del motor. Pasarla a NULL espera a que
// el mezclador suelte su hilo, y el mezclador de GL puede estar soltando un
// buffer que le pide algo al contexto de GL; en macOS eso corre en el hilo
// PRINCIPAL. Si quien la desarma es el hilo principal (la interfaz: una pantalla
// que deja de mostrar una escena), se esperan los dos para siempre. Pasó: la app
// colgada hasta que el launcher la mató. Aparte, el hilo principal queda libre
// para contestarle a GL y el desarme termina solo.
void Motor::desarmarAparte(std::unique_ptr<Composicion> c) {
    if (!c) return;
    std::shared_ptr<Composicion> s(std::move(c));
    desarmarAparte([s]() mutable { s.reset(); });
}

std::shared_future<void> Motor::desarmarAparte(std::function<void()> f) {
    std::lock_guard<std::mutex> l(desarmesM_);
    desarmes_.erase(std::remove_if(desarmes_.begin(), desarmes_.end(),
                                   [](std::shared_future<void>& f) {
                                       return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                   }),
                    desarmes_.end());
    std::shared_future<void> sf = std::async(std::launch::async, std::move(f)).share();
    desarmes_.push_back(sf);
    return sf;
}

bool Motor::tbarYa(const std::string& nombre, double t, bool soltar, std::string* error) {
    BA_CANDADO(mu_);
    auto it = escenas_.find(nombre);
    if (it == escenas_.end() || !vPrograma_) {
        if (error != nullptr) *error = "no hay una escena «" + nombre + "»";
        return false;
    }
    const bool quedo = vPrograma_->manual(it->second, t, soltar);
    if (soltar || t >= 1.0) {
        if (quedo) {
            alAire_ = nombre;
            std::vector<std::string> ids;
            for (const auto& c : it->second.capas)
                if (c.visible) ids.push_back(c.entrada);
            escenaAlAire(ids, 200);
            actualizarMultiview();
        }
        return quedo;
    }
    return false;
}

std::string Motor::tbarHacia() const {
    const auto f = instantanea();
    return f && f->contains("tbar_hacia") ? (*f)["tbar_hacia"].get<std::string>() : std::string();
}

void Motor::masterVolumenYa(double lineal, int ms) {
    BA_CANDADO(mu_);
    if (programa_) programa_->ponerMaster(lineal, ms);
}

GstFlowReturn Motor::alAudio(GstElement* sink, gpointer d) {
    auto* m = static_cast<Motor*>(d);
    GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (s == nullptr) return GST_FLOW_OK;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo map;
    if (b != nullptr && gst_buffer_map(b, &map, GST_MAP_READ)) {
        std::lock_guard<std::mutex> l(m->tomaMu_);
        for (auto& [id, fn] : m->oyentes_) fn(reinterpret_cast<const float*>(map.data), map.size / (2 * sizeof(float)));
        gst_buffer_unmap(b, &map);
    }
    gst_sample_unref(s);
    return GST_FLOW_OK;
}

int Motor::escucharPrograma(std::function<void(const float*, std::size_t)> fn) {
    int id = 0;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        id = proximoOyente_++;
        oyentes_[id] = std::move(fn);
    }
    orden("escucharPrograma", [this]() {
        armarTomaAudio();
        return std::string();
    });
    return id;
}

void Motor::armarTomaAudio() {
    BA_CANDADO(mu_);
    if (tomaAudio_ == nullptr && programa_) {
        const std::string d = "intersrc producer-name=" + entreComillas(programa_->productor()) +
                              " ! queue max-size-time=2000000000 max-size-buffers=0 max-size-bytes=0 ! "
                              "appsink name=s emit-signals=true sync=true max-buffers=0 drop=false";
        tomaAudio_ = gst_parse_launch(d.c_str(), nullptr);
        if (tomaAudio_ != nullptr) {
            GstElement* sk = gst_bin_get_by_name(GST_BIN(tomaAudio_), "s");
            g_signal_connect(sk, "new-sample", G_CALLBACK(&Motor::alAudio), this);
            gst_object_unref(sk);
            reloj_->adoptar(tomaAudio_);
            gst_pipeline_set_latency(GST_PIPELINE(tomaAudio_),
                                     static_cast<GstClockTime>(programa_->latenciaSalidaNs() + kMargenMezclaNs));
            gst_element_set_state(tomaAudio_, GST_STATE_PLAYING);
        }
    }
}

void Motor::dejarDeEscuchar(int id) {
    bool vacio = false;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        oyentes_.erase(id); // desde acá no se lo llama más (alAudio llama con tomaMu_)
        vacio = oyentes_.empty();
    }
    if (!vacio) return;
    orden("dejarDeEscuchar", [this]() {
        {
            std::lock_guard<std::mutex> lt(tomaMu_);
            if (!oyentes_.empty()) return std::string(); // volvió alguien mientras tanto
        }
        if (tomaAudio_ != nullptr) {
            GstElement* p = tomaAudio_;
            tomaAudio_ = nullptr;
            desarmarAparte([p]() {
                gst_element_set_state(p, GST_STATE_NULL);
                gst_object_unref(p);
            });
        }
        return std::string();
    });
}

int Motor::escucharEntrada(const std::string& id, std::function<void(const float*, std::size_t)> fn) {
    // El número se da ya (único en el proceso: una toma de un motor anterior
    // nunca es una de este); armarla va a la cola. Si no se puede, nunca queda
    // viva y quien la usa lo ve con tomaViva.
    static std::atomic<int> proxima{1};
    const int n = proxima++;
    {
        // Viva desde ya («en camino»): quien la vigila con tomaViva no la da por
        // muerta en los milisegundos que tarda en armarse.
        std::lock_guard<std::mutex> lv(tomasVivasM_);
        tomasVivas_[n] = nullptr;
    }
    orden("escucharEntrada", [this, n, id, fn = std::move(fn)]() mutable {
        escucharEntradaYa(n, id, std::move(fn));
        return std::string();
    });
    return n;
}

void Motor::escucharEntradaYa(int n, const std::string& id, std::function<void(const float*, std::size_t)> fn) {
    BA_CANDADO(mu_);
    const auto noSePudo = [this, n]() {
        std::lock_guard<std::mutex> lv(tomasVivasM_);
        tomasVivas_.erase(n);
    };
    {
        // La soltaron antes de que se armara: no se arma.
        std::lock_guard<std::mutex> lv(tomasVivasM_);
        if (tomasVivas_.count(n) == 0) return;
    }
    auto it = canales_.find(id);
    if (it == canales_.end() || !it->second.entrada->daAudio()) {
        noSePudo();
        return;
    }
    const std::string d = "intersrc producer-name=" + entreComillas(it->second.entrada->productorAudio()) +
                          " ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,layout=interleaved,rate=48000,"
                          "channels=2 ! appsink name=s emit-signals=true sync=false max-buffers=50 drop=true";
    GstElement* p = gst_parse_launch(d.c_str(), nullptr);
    if (p == nullptr) {
        noSePudo();
        return;
    }
    auto t = std::make_shared<TomaEntrada>();
    t->pipeline = p;
    t->id = id;
    t->mute = it->second.mute;
    t->fn = std::move(fn);
    GstElement* sk = gst_bin_get_by_name(GST_BIN(p), "s");
    g_signal_connect(sk, "new-sample", G_CALLBACK(+[](GstElement* sink, gpointer dd) -> GstFlowReturn {
                         auto* toma = static_cast<TomaEntrada*>(dd);
                         GstSample* smp = gst_app_sink_pull_sample(GST_APP_SINK(sink));
                         if (smp == nullptr) return GST_FLOW_OK;
                         // Con fnM: cuando dejarDeEscucharEntrada vuelve, ya no se la llama.
                         std::lock_guard<std::mutex> lf(toma->fnM);
                         if (toma->apagada) {
                             gst_sample_unref(smp);
                             return GST_FLOW_OK;
                         }
                         GstBuffer* b = gst_sample_get_buffer(smp);
                         GstMapInfo map;
                         if (b != nullptr && gst_buffer_map(b, &map, GST_MAP_READ)) {
                             const std::size_t cuadros = map.size / (2 * sizeof(float));
                             if (toma->mute.load()) {
                                 thread_local std::vector<float> ceros;
                                 ceros.assign(cuadros * 2, 0.0f);
                                 toma->fn(ceros.data(), cuadros);
                             } else {
                                 toma->fn(reinterpret_cast<const float*>(map.data), cuadros);
                             }
                             gst_buffer_unmap(b, &map);
                         }
                         gst_sample_unref(smp);
                         return GST_FLOW_OK;
                     }),
                     t.get());
    gst_object_unref(sk);
    reloj_->adoptar(p);
    gst_element_set_state(p, GST_STATE_PLAYING);
    {
        std::lock_guard<std::mutex> lv(tomasVivasM_);
        auto v = tomasVivas_.find(n);
        if (v == tomasVivas_.end()) {
            // La soltaron mientras se armaba: no se la resucita (ni se llama a fn).
            {
                std::lock_guard<std::mutex> lf(t->fnM);
                t->apagada = true;
            }
            desarmarAparte([t]() {
                gst_element_set_state(t->pipeline, GST_STATE_NULL);
                gst_object_unref(t->pipeline);
            });
            return;
        }
        v->second = t;
    }
    tomasEntrada_[n] = std::move(t);
}

bool Motor::tomaViva(int toma) const {
    std::lock_guard<std::mutex> lv(tomasVivasM_);
    return tomasVivas_.count(toma) > 0;
}

void Motor::dejarDeEscucharEntrada(int toma) {
    std::shared_ptr<TomaEntrada> viva;
    {
        std::lock_guard<std::mutex> lv(tomasVivasM_);
        auto it = tomasVivas_.find(toma);
        if (it != tomasVivas_.end()) {
            viva = it->second;
            tomasVivas_.erase(it);
        }
    }
    // Al volver, la función de quien escuchaba no se llama más (puede morir
    // tranquilo); desarmar la toma va aparte. (Si estaba «en camino», viva es
    // nulo y escucharEntradaYa ya no la arma.)
    if (viva) {
        std::lock_guard<std::mutex> lf(viva->fnM);
        viva->apagada = true;
    }
    orden("dejarDeEscucharEntrada", [this, toma]() {
        auto it = tomasEntrada_.find(toma);
        if (it == tomasEntrada_.end()) return std::string();
        std::shared_ptr<TomaEntrada> t = std::move(it->second);
        tomasEntrada_.erase(it);
        desarmarAparte([t]() {
            gst_element_set_state(t->pipeline, GST_STATE_NULL);
            gst_object_unref(t->pipeline);
        });
        return std::string();
    });
}

bool Motor::verUltimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const {
    return vistas_ && vistas_->ultimo(clave, out, serie);
}

json Motor::medio(const std::string& id) const {
    // De la foto: lo pide la interfaz en cada tic (ver Motor::instantanea).
    const auto f = instantanea();
    if (!f || !f->contains("medios")) return nullptr;
    const json& m = (*f)["medios"];
    return m.contains(id) ? m[id] : json(nullptr);
}

json Motor::medioYa(const std::string& id) const {
    auto it = canales_.find(id);
    if (it == canales_.end()) return nullptr;
    Entrada* e = it->second.entrada.get();
    // Solo pregunta si es un medio: armar el estado entero (las propiedades de
    // todos sus filtros) en cada tic de la interfaz la trababa.
    if (!e->esMedio()) return nullptr;
    return {{"estado", e->estadoMedio()}, {"tiempo_ms", e->tiempoMs()}, {"duracion_ms", e->duracionMs()}};
}

std::string Motor::controlMedioYa(const std::string& id, const std::string& accion, std::int64_t ms) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    if (accion == "ir") {
        it->second.entrada->irA(ms);
        return {};
    }
    return it->second.entrada->controlMedio(accion);
}

void Motor::armarRetorno(int n, Retorno& r) {
    r.bus.reset();
    if (r.modo != "mix_minus" || cfg_.prefijoAnillos.empty()) return;
    Mezcla::Opciones op;
    op.limitador = true; // lo que oye el invitado tampoco satura
    op.medidores = false;
    op.anillo = anillo("Return." + std::to_string(n));
    r.bus = std::make_unique<Mezcla>(*reloj_, "retorno" + std::to_string(n), op);
    for (const auto& [id, c] : canales_) enBus(*r.bus, id, c, (c.pistas & 1u) != 0 && id != r.propia, 0);
}

std::string Motor::retornoYa(int n, const std::string& modo, const std::string& propia) {
    BA_CANDADO(mu_);
    if (n < 1 || n > 64) return "invitado fuera de rango (1…64)";
    if (modo != "programa" && modo != "mix_minus" && modo != "apagado") return "modo: programa, mix_minus o apagado";
    if (modo == "programa" || modo == "apagado") {
        retornos_.erase(n);
        return {};
    }
    Retorno& r = retornos_[n];
    if (r.modo == modo && r.propia == propia && r.bus) return {};
    r.modo = modo;
    r.propia = propia;
    armarRetorno(n, r);
    return r.bus && r.bus->anda() ? std::string() : "el retorno no arrancó";
}

std::string Motor::escucharYa(const std::string& modo, const std::string& id) {
    BA_CANDADO(mu_);
    if (!escucha_) return "el motor no está andando";
    if (modo != "seleccion") seleccion_.reset();
    if (modo == "apagado") {
        escucha_->apagar();
        escuchaModo_ = modo;
        escuchaId_.clear();
        directo_.reset();
        return {};
    }
    if (modo == "seleccion") {
        if (!seleccion_) {
            Mezcla::Opciones op;
            op.medidores = false;
            seleccion_ = std::make_unique<Mezcla>(*reloj_, "seleccion", op);
            for (const auto& [cid, c] : canales_) enSeleccion(cid, c);
        }
        escuchaModo_ = modo;
        escuchaId_.clear();
        directo_.reset();
        return escucha_->escuchar(seleccion_->productor(), seleccion_->latenciaSalidaNs());
    }
    if (modo == "programa") {
        escuchaModo_ = modo;
        escuchaId_.clear();
        directo_.reset();
        return escucha_->escuchar(programa_->productor(), programa_->latenciaSalidaNs());
    }
    if (modo == "directo") {
        if (!directo_) {
            Mezcla::Opciones op;
            op.medidores = false;
            op.latenciaFijaNs = kDirectoNs;
            directo_ = std::make_unique<Mezcla>(*reloj_, "directo", op);
            for (const auto& [cid, c] : canales_) enBus(*directo_, cid, c, c.global, 0);
        }
        escuchaModo_ = modo;
        escuchaId_.clear();
        return escucha_->escuchar(directo_->productor(), kDirectoNs + 20 * static_cast<std::int64_t>(GST_MSECOND));
    }
    if (modo == "pfl") {
        auto it = canales_.find(id);
        if (it == canales_.end()) return "no hay una entrada «" + id + "»";
        if (!it->second.entrada->daAudio()) return "«" + id + "» no tiene audio";
        escuchaModo_ = modo;
        escuchaId_ = id;
        directo_.reset();
        return escucha_->escuchar(it->second.entrada->productorAudio(), reloj_->latencia());
    }
    return "modo: programa, directo, pfl, seleccion o apagado";
}

void Motor::enSeleccion(const std::string& id, const Canal& c) {
    if (!c.escucha || !c.entrada->daAudio()) {
        seleccion_->quitar(id);
        return;
    }
    // En los auriculares aunque no esté al aire (sin el fundido de escena).
    seleccion_->agregar(id);
    seleccion_->ponerNivel(id, c.nivel);
    seleccion_->ponerMute(id, c.mute);
    seleccion_->ponerEscena(id, 1.0, 0);
}

std::string Motor::ponerEscuchaYa(const std::string& id, bool on) {
    BA_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.escucha = on;
    bool alguna = false;
    for (const auto& [cid, c] : canales_) alguna = alguna || c.escucha;
    if (!alguna) return escuchaModo_ == "seleccion" ? escuchar("apagado") : std::string();
    if (escuchaModo_ != "seleccion") return escuchar("seleccion");
    enSeleccion(id, it->second);
    return {};
}

void Motor::escuchaDispositivoYa(const std::string& id) {
    BA_CANDADO(mu_);
    if (escucha_) escucha_->ponerDispositivo(id);
}

void Motor::escuchaNivelYa(double lineal) {
    BA_CANDADO(mu_);
    if (escucha_) escucha_->ponerNivel(lineal);
}

bool Motor::ponerLimitadorYa(bool activo, double techoDb) {
    BA_CANDADO(mu_);
    return programa_ && programa_->ponerLimitador(activo, techoDb);
}

void Motor::ponerTopeEstudio(std::int64_t ms) { reloj_->ponerTope(ms); }

std::string Motor::productorDeBus(const std::string& bus, std::int64_t* latenciaNs) {
    BA_CANDADO(mu_);
    Mezcla* m = nullptr;
    if (bus == "programa")
        m = programa_.get();
    else if (bus.rfind("pista", 0) == 0 && bus.size() == 6)
        m = pista(bus[5] - '0', true);
    if (m == nullptr) return {};
    if (latenciaNs != nullptr) *latenciaNs = m->latenciaSalidaNs();
    return m->productor();
}

std::shared_ptr<const json> Motor::instantanea() const {
    std::lock_guard<std::mutex> l(instantaneaM_);
    return instantanea_;
}

void Motor::publicarInstantanea() {
    auto foto = std::make_shared<const json>(estado());
    std::lock_guard<std::mutex> l(instantaneaM_);
    instantanea_ = std::move(foto);
}

json Motor::estado() const {
    BA_CANDADO(mu_);
    json entradas = json::object();
    for (const auto& [id, c] : canales_) {
        json e = c.entrada->estado();
        e["global"] = c.global;
        e["en_escena"] = c.enEscena;
        e["nivel"] = c.nivel;
        e["mute"] = c.mute;
        json ps = json::array();
        for (int n = 1; n <= 6; ++n)
            if ((c.pistas & (1u << (n - 1))) != 0) ps.push_back(n);
        e["pistas"] = ps;
        entradas[id] = std::move(e);
    }
    json ret = json::object();
    for (const auto& [n, r] : retornos_)
        ret[std::to_string(n)] = {{"modo", r.modo}, {"propia", r.propia}, {"anda", r.bus && r.bus->anda()}};
    json pistas = json::object();
    for (const auto& [n, p] : pistas_) pistas[std::to_string(n)] = p->estado();
    json tarde = json::array();
    for (const auto& [id, c] : canales_)
        if (c.entrada->llegaTarde()) tarde.push_back(id);
    json esc = escucha_ ? escucha_->estado() : json::object();
    esc["modo"] = escuchaModo_;
    if (!escuchaId_.empty()) esc["entrada"] = escuchaId_;
    json medios = json::object();
    for (const auto& [id, c] : canales_)
        if (c.entrada->esMedio()) medios[id] = medioYa(id);
    return {{"andando", andando()},
            {"medios", medios},
            {"tbar_hacia", vPrograma_ ? vPrograma_->manualHacia() : std::string()},
            {"estudio",
             {{"latencia_ms", static_cast<double>(reloj_->latencia()) / 1e6},
              {"tope_ms", reloj_->topeMs()},
              {"tope_automatico", reloj_->topeAutomatico()},
              {"llegan_tarde", tarde}}},
            {"lienzo",
             {{"ancho", cfg_.lienzo.ancho},
              {"alto", cfg_.lienzo.alto},
              {"fps", static_cast<double>(cfg_.lienzo.fpsN) / std::max(1, cfg_.lienzo.fpsD)}}},
            {"programa", programa_ ? programa_->estado() : json()},
            {"video",
             {{"al_aire", alAire_},
              {"armada", armada_},
              {"escenas", [&]() {
                   json n = json::array();
                   for (const auto& [k, e] : escenas_) n.push_back(k);
                   return n;
               }()},
              {"programa", vPrograma_ ? vPrograma_->estado() : json()},
              {"preview", vPreview_ ? vPreview_->estado() : json()}}},
            {"pistas", pistas},
            {"retornos", ret},
            {"escucha", esc},
            {"vistas", vistas_ ? vistas_->estado() : json::object()},
            {"entradas", entradas}};
}


// ============================================================================
// La cola de órdenes (docs/planes/PLAN-MOTOR-SIN-ESPERAS.md, S3)
//
// Lo que cambia el motor y puede tardar se ENCOLA y lo corre el hilo del motor
// en su vuelta siguiente (lo despierta: no espera los 20 ms). Quien llama vuelve
// en el acto: la interfaz nunca espera un cambio de estado de GStreamer. Si la
// orden falla, el error va a alFallar (la app lo muestra) y al registro.
//
// Desde el propio hilo del motor (una orden que llama a otra) y con el motor
// parado (el arranque, antes de iniciar) se corre directo, como siempre, y
// devuelve el resultado de verdad.
// ============================================================================

bool Motor::enHiloDelMotor() const { return hiloId_.load() == std::this_thread::get_id(); }

std::string Motor::orden(const char* que, std::function<std::string()> f) {
    if (!seguir_.load() || enHiloDelMotor()) {
        BA_CANDADO(mu_);
        return f(); // directo: el resultado de verdad
    }
    {
        std::lock_guard<std::mutex> l(colaM_);
        cola_.emplace_back(que, std::move(f));
    }
    colaCv_.notify_one();
    return {};
}

void Motor::encolar(const char* que, std::function<std::string()> f) {
    {
        std::lock_guard<std::mutex> l(colaM_);
        cola_.emplace_back(que, std::move(f));
    }
    colaCv_.notify_one();
}

void Motor::alFallar(std::function<void(const std::string& que, const std::string& error)> fn) {
    std::lock_guard<std::mutex> l(colaM_);
    alFallar_ = std::move(fn);
}

void Motor::esperarCola() {
    if (!seguir_.load() || enHiloDelMotor()) return;
    if (enHiloInterfaz()) registrar(Nivel::Aviso, "esperarCola desde el hilo de la interfaz: no tendría que esperar");
    auto listo = std::make_shared<std::promise<void>>();
    std::future<void> f = listo->get_future();
    orden("esperarCola", [listo]() {
        listo->set_value();
        return std::string();
    });
    f.wait();
}

void Motor::correrOrdenes() {
    std::deque<std::pair<const char*, std::function<std::string()>>> ordenes;
    {
        std::lock_guard<std::mutex> l(colaM_);
        ordenes.swap(cola_);
    }
    if (ordenes.empty()) return;
    for (auto& [que, f] : ordenes) {
        Tramo t(que);
        const std::string e = f();
        if (e.empty()) continue;
        registrar(Nivel::Aviso, std::string("orden ") + que + ": " + e);
        std::function<void(const std::string&, const std::string&)> fn;
        {
            std::lock_guard<std::mutex> l(colaM_);
            fn = alFallar_;
        }
        if (fn) fn(que, e);
    }
    fotoPedida_.store(true); // que la foto muestre lo que cambió
}

// --- Lo que llama la interfaz: se encola ---------------------------------------

std::string Motor::agregarEntrada(const std::string& id, const json& ajustes, const json& opciones) {
    return orden("agregarEntrada", [=, this]() { return agregarEntradaYa(id, ajustes, opciones); });
}
std::string Motor::actualizarEntrada(const std::string& id, const json& ajustes) {
    return orden("actualizarEntrada", [=, this]() { return actualizarEntradaYa(id, ajustes); });
}
std::string Motor::quitarEntrada(const std::string& id) {
    return orden("quitarEntrada", [=, this]() { return quitarEntradaYa(id); });
}
std::string Motor::propiedadEntrada(const std::string& id, const std::string& elemento, const std::string& prop,
                                    const std::string& valor) {
    return orden("propiedadEntrada", [=, this]() { return propiedadEntradaYa(id, elemento, prop, valor); });
}
std::string Motor::controlEntrada(const std::string& id, const json& campos) {
    return orden("controlEntrada", [=, this]() { return controlEntradaYa(id, campos); });
}
std::string Motor::ponerNivel(const std::string& id, double lineal) {
    return orden("ponerNivel", [=, this]() { return ponerNivelYa(id, lineal); });
}
std::string Motor::ponerMute(const std::string& id, bool mute) {
    return orden("ponerMute", [=, this]() { return ponerMuteYa(id, mute); });
}
std::string Motor::ponerPistas(const std::string& id, const std::vector<int>& pistas) {
    return orden("ponerPistas", [=, this]() { return ponerPistasYa(id, pistas); });
}
void Motor::escenaAlAire(const std::vector<std::string>& ids, int fundidoMs) {
    orden("escenaAlAire", [=, this]() {
        escenaAlAireYa(ids, fundidoMs);
        return std::string();
    });
}
std::string Motor::controlMedio(const std::string& id, const std::string& accion, std::int64_t ms) {
    return orden("controlMedio", [=, this]() { return controlMedioYa(id, accion, ms); });
}
std::string Motor::definirEscena(const json& escena) {
    return orden("definirEscena", [=, this]() { return definirEscenaYa(escena); });
}
std::string Motor::quitarEscena(const std::string& nombre) {
    return orden("quitarEscena", [=, this]() { return quitarEscenaYa(nombre); });
}
std::string Motor::armar(const std::string& nombre) {
    return orden("armar", [=, this]() { return armarYa(nombre); });
}
std::string Motor::take(const std::string& transicion, int ms) {
    return orden("take", [=, this]() { return takeYa(transicion, ms); });
}
std::string Motor::alAire(const std::string& nombre, const std::string& transicion, int ms) {
    return orden("alAire", [=, this]() { return alAireYa(nombre, transicion, ms); });
}
std::string Motor::dsk(const json& capas, int fundidoMs) {
    return orden("dsk", [=, this]() { return dskYa(capas, fundidoMs); });
}
bool Motor::tbar(const std::string& nombre, double t, bool soltar, std::string* error) {
    // Lo que va a pasar se sabe sin esperar (Composicion::manual): termina con
    // t >= 1 o al soltar, y queda la nueva si se soltó pasada la mitad.
    const std::string e = orden("tbar", [=, this]() {
        std::string err;
        tbarYa(nombre, t, soltar, &err);
        return err;
    });
    if (error != nullptr) *error = e;
    pedirFoto();
    return e.empty() && (t >= 1.0 || (soltar && t >= 0.5));
}
std::string Motor::usarEscena(const std::string& nombre) {
    orden("usarEscena", [=, this]() {
        return usarEscenaYa(nombre).empty() ? "no hay una escena «" + nombre + "»" : std::string();
    });
    return "video:escena:" + nombre; // Composicion::productor
}
void Motor::soltarEscena(const std::string& nombre) {
    orden("soltarEscena", [=, this]() {
        soltarEscenaYa(nombre);
        return std::string();
    });
}
std::string Motor::retorno(int n, const std::string& modo, const std::string& propia) {
    return orden("retorno", [=, this]() { return retornoYa(n, modo, propia); });
}
std::string Motor::escuchar(const std::string& modo, const std::string& id) {
    return orden("escuchar", [=, this]() { return escucharYa(modo, id); });
}
std::string Motor::ponerEscucha(const std::string& id, bool on) {
    return orden("ponerEscucha", [=, this]() { return ponerEscuchaYa(id, on); });
}
void Motor::escuchaDispositivo(const std::string& id) {
    orden("escuchaDispositivo", [=, this]() {
        escuchaDispositivoYa(id);
        return std::string();
    });
}
void Motor::escuchaNivel(double lineal) {
    orden("escuchaNivel", [=, this]() {
        escuchaNivelYa(lineal);
        return std::string();
    });
}
void Motor::masterVolumen(double lineal, int ms) {
    orden("masterVolumen", [=, this]() {
        masterVolumenYa(lineal, ms);
        return std::string();
    });
}
bool Motor::ponerLimitador(bool activo, double techoDb) {
    return orden("ponerLimitador", [=, this]() {
               return ponerLimitadorYa(activo, techoDb) ? std::string() : std::string("no se pudo poner el limitador");
           }).empty();
}
std::string Motor::multiview(bool activo, int ancho, int alto) {
    return orden("multiview", [=, this]() { return multiviewYa(activo, ancho, alto); });
}
std::string Motor::verPedir(const std::string& clave, int ancho, int alto) {
    return orden("verPedir", [=, this]() { return verPedirYa(clave, ancho, alto); });
}
void Motor::verSoltar(const std::string& clave) {
    orden("verSoltar", [=, this]() {
        verSoltarYa(clave);
        return std::string();
    });
}
} // namespace ba::motor
