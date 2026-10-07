// ============================================================================
// src/motor/Motor.cpp — ver Motor.hpp
// ============================================================================
#include "Motor.hpp"
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
    reloj_ = std::make_unique<Reloj>(cfg_.reloj, cfg_.relojServidor, cfg_.relojPuerto, cfg_.relojDominio);
    reloj_->ponerTope(cfg_.topeMs);
}

Motor::~Motor() { detener(); }

std::string Motor::iniciar() {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
        std::lock_guard<std::recursive_mutex> l(mu_);
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
    return {};
}

void Motor::detener() {
    seguir_ = false;
    if (hilo_.joinable()) hilo_.join();
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    std::int64_t ultimoAvisoUs = 0;
    while (seguir_) {
        // Vigía: este candado lo esperan la interfaz y el MCP. Una vuelta que
        // lo tiene mucho (algo que bloquea adentro) traba la app entera; queda
        // en el registro para encontrarlo.
        const std::int64_t desdeUs = g_get_monotonic_time();
        struct Vigia {
            std::int64_t desde;
            std::int64_t& ultimo;
            ~Vigia() {
                const std::int64_t ahora = g_get_monotonic_time();
                const std::int64_t ms = (ahora - desde) / 1000;
                if (ms > 200 && ahora - ultimo > 10'000'000) {
                    ultimo = ahora;
                    registrar(Nivel::Aviso, "el bucle del motor tuvo el candado " + std::to_string(ms) +
                                                " ms: la interfaz y el MCP esperaron eso");
                }
            }
        };
        origen_red::tic(); // las cámaras de red compartidas (tiene su propio candado)
        static int vueltas = 0;
        const bool revisarUso = ++vueltas % 10 == 0; // cada ~200 ms
        {
            std::lock_guard<std::recursive_mutex> l(mu_);
            Vigia vigia{desdeUs, ultimoAvisoUs};
            if (revisarUso) actualizarUso();
            for (auto& [id, c] : canales_) c.entrada->tic();
            if (programa_) programa_->tic();
            if (directo_) directo_->tic();
            if (seleccion_) seleccion_->tic();
            for (auto& [n, p] : pistas_) p->tic();
            for (auto& [n, r] : retornos_)
                if (r.bus) r.bus->tic();
            if (vPrograma_) vPrograma_->tic();
            if (vPreview_) vPreview_->tic();
            if (vMulti_) vMulti_->tic();
            for (auto& [n, c] : vEscenas_)
                if (c.comp) c.comp->tic();
            if (tomaAudio_ != nullptr) {
                GstBus* bus = gst_element_get_bus(tomaAudio_);
                while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                gst_object_unref(bus);
            }
            for (auto& [n, a] : mvAux_) a->tic();
            if (vistas_) vistas_->tic();
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
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
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

std::string Motor::agregarEntrada(const std::string& id, const json& ajustes, const json& opcionesPedidas) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    const json opciones = opcionesPedidas.is_object() ? opcionesPedidas : json::object(); // {} llega como null
    if (id.empty()) return "falta el id";
    if (id.find(':') != std::string::npos) return "el id no puede tener «:»";
    if (canales_.count(id) > 0) return "ya hay una entrada «" + id + "»";
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
    c.entrada = std::make_unique<Entrada>(*reloj_, cfg_.lienzo, id, ajustes.is_object() ? ajustes : json::object());
    canales_[id] = std::move(c);
    aplicar(id, 0);
    actualizarMultiview();
    return {};
}

std::string Motor::actualizarEntrada(const std::string& id, const json& ajustes) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.entrada->actualizar(ajustes);
    aplicar(id, 0); // pudo pasar a tener (o dejar de tener) audio
    return {};
}

std::string Motor::publicarReloj(int puerto) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::propiedadEntrada(const std::string& id, const std::string& elemento, const std::string& prop,
                                    const std::string& valor) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    return it->second.entrada->propiedad(elemento, prop, valor);
}

json Motor::propiedadesEntrada(const std::string& id, const std::string& elemento) const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    return it == canales_.end() ? json::object() : it->second.entrada->leerPropiedades(elemento);
}

std::string Motor::controlEntrada(const std::string& id, const json& campos) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.entrada->control(campos);
    return {};
}

std::string Motor::quitarEntrada(const std::string& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    if (programa_) programa_->quitar(id);
    if (directo_) directo_->quitar(id);
    if (seleccion_) seleccion_->quitar(id);
    for (auto& [n, p] : pistas_) p->quitar(id);
    for (auto& [n, r] : retornos_)
        if (r.bus) r.bus->quitar(id);
    if (escuchaModo_ == "pfl" && escuchaId_ == id) escuchar("apagado");
    canales_.erase(it);
    actualizarMultiview();
    return {};
}

std::string Motor::ponerNivel(const std::string& id, double lineal) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.nivel = std::clamp(lineal, 0.0, 4.0);
    aplicar(id, 0);
    return {};
}

std::string Motor::ponerMute(const std::string& id, bool mute) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.mute = mute;
    for (auto& [n, t] : tomasEntrada_)
        if (t->id == id) t->mute = mute;
    aplicar(id, 0);
    return {};
}

std::string Motor::ponerPistas(const std::string& id, const std::vector<int>& pistas) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

void Motor::escenaAlAire(const std::vector<std::string>& ids, int fundidoMs) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    const std::set<std::string> al(ids.begin(), ids.end());
    for (auto& [id, c] : canales_) {
        if (c.global) continue;
        const bool antes = c.enEscena;
        c.enEscena = al.count(id) > 0;
        if (antes != c.enEscena) aplicar(id, fundidoMs);
    }
}

std::string Motor::definirEscena(const json& j) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    if (auto it = vEscenas_.find(e.nombre); it != vEscenas_.end() && it->second.comp) it->second.comp->mostrar(e);
    actualizarUso();
    return {};
}

std::string Motor::quitarEscena(const std::string& nombre) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (escenas_.erase(nombre) == 0) return "no hay una escena «" + nombre + "»";
    if (armada_ == nombre) {
        armada_.clear();
        if (vPreview_) vPreview_->mostrar(Escena{"", {}});
    }
    return {};
}

std::string Motor::armar(const std::string& nombre) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::alAire(const std::string& nombre, const std::string& transicion, int ms) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::take(const std::string& transicion, int ms) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (armada_.empty()) return "no hay nada armado en el preview";
    const std::string antes = alAire_;
    const std::string e = alAire(armada_, transicion, ms);
    if (!e.empty()) return e;
    // Lo que estaba al aire queda armado (el take intercambia, como una mesa de corte).
    if (!antes.empty() && escenas_.count(antes) > 0) armar(antes);
    return {};
}

std::string Motor::dsk(const json& capas, int fundidoMs) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::multiview(bool activo, int ancho, int alto) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::verPedir(const std::string& clave, int ancho, int alto) {
    // Siempre en este orden: el lock del motor y después el de las vistas (el hilo
    // del motor hace lo mismo en el tic; al revés se trababan).
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!vistas_) return "el motor no está andando";
    const std::string b = clave.substr(0, clave.find('@'));
    if (b.rfind("escena:", 0) == 0 && usarEscena(b.substr(7)).empty()) return "no hay una escena «" + b.substr(7) + "»";
    const std::string e = vistas_->pedir(clave, ancho, alto);
    actualizarUso();
    return e;
}

void Motor::verSoltar(const std::string& clave) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (vistas_) vistas_->soltar(clave);
    const std::string b = clave.substr(0, clave.find('@'));
    if (b.rfind("escena:", 0) == 0) soltarEscena(b.substr(7));
}

void Motor::verAvisar(const std::string& clave, std::function<void(const Cuadro&)> fn) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (vistas_) vistas_->avisar(clave, std::move(fn));
}

bool Motor::empujarCuadro(const std::string& id, const std::uint8_t* bgra, int ancho, int alto, int stride) {
    Entrada* e = nullptr;
    {
        std::lock_guard<std::recursive_mutex> l(mu_);
        auto it = canales_.find(id);
        if (it == canales_.end()) return false;
        e = it->second.entrada.get();
    }
    return e->empujar(bgra, ancho, alto, stride); // (la entrada tiene su lock)
}

std::string Motor::usarEscena(const std::string& nombre) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto e = escenas_.find(nombre);
    if (e == escenas_.end() || !cfg_.video) return {};
    CompEscena& c = vEscenas_[nombre];
    if (!c.comp) {
        c.comp = std::make_unique<Composicion>(*reloj_, cfg_.lienzo, "escena:" + nombre);
        c.comp->mostrar(e->second);
    }
    ++c.pedidos;
    return c.comp->productor();
}

void Motor::soltarEscena(const std::string& nombre) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = vEscenas_.find(nombre);
    if (it == vEscenas_.end()) return;
    if (--it->second.pedidos <= 0) vEscenas_.erase(it);
}

bool Motor::tbar(const std::string& nombre, double t, bool soltar, std::string* error) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    std::lock_guard<std::recursive_mutex> l(mu_);
    return vPrograma_ ? vPrograma_->manualHacia() : std::string();
}

void Motor::masterVolumen(double lineal, int ms) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    std::lock_guard<std::recursive_mutex> l(mu_);
    int id = 0;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        id = proximoOyente_++;
        oyentes_[id] = std::move(fn);
    }
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
    return id;
}

void Motor::dejarDeEscuchar(int id) {
    bool vacio = false;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        oyentes_.erase(id);
        vacio = oyentes_.empty();
    }
    if (!vacio) return;
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (tomaAudio_ != nullptr) {
        gst_element_set_state(tomaAudio_, GST_STATE_NULL);
        gst_object_unref(tomaAudio_);
        tomaAudio_ = nullptr;
    }
}

int Motor::escucharEntrada(const std::string& id, std::function<void(const float*, std::size_t)> fn) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end() || !it->second.entrada->daAudio()) return 0;
    const std::string d = "intersrc producer-name=" + entreComillas(it->second.entrada->productorAudio()) +
                          " ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,layout=interleaved,rate=48000,"
                          "channels=2 ! appsink name=s emit-signals=true sync=false max-buffers=50 drop=true";
    GstElement* p = gst_parse_launch(d.c_str(), nullptr);
    if (p == nullptr) return 0;
    auto t = std::make_unique<TomaEntrada>();
    t->pipeline = p;
    t->id = id;
    t->mute = it->second.mute;
    t->fn = std::move(fn);
    GstElement* sk = gst_bin_get_by_name(GST_BIN(p), "s");
    g_signal_connect(sk, "new-sample", G_CALLBACK(+[](GstElement* sink, gpointer dd) -> GstFlowReturn {
                         auto* toma = static_cast<TomaEntrada*>(dd);
                         GstSample* smp = gst_app_sink_pull_sample(GST_APP_SINK(sink));
                         if (smp == nullptr) return GST_FLOW_OK;
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
    // Únicos en el proceso: una toma de un motor anterior nunca es una de este.
    static std::atomic<int> proxima{1};
    const int n = proxima++;
    tomasEntrada_[n] = std::move(t);
    return n;
}

bool Motor::tomaViva(int toma) const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return tomasEntrada_.count(toma) > 0;
}

void Motor::dejarDeEscucharEntrada(int toma) {
    std::unique_ptr<TomaEntrada> t;
    {
        std::lock_guard<std::recursive_mutex> l(mu_);
        auto it = tomasEntrada_.find(toma);
        if (it == tomasEntrada_.end()) return;
        t = std::move(it->second);
        tomasEntrada_.erase(it);
    }
    // Fuera del lock: set_state espera a que termine el callback.
    gst_element_set_state(t->pipeline, GST_STATE_NULL);
    gst_object_unref(t->pipeline);
}

bool Motor::verUltimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const {
    return vistas_ && vistas_->ultimo(clave, out, serie);
}

json Motor::medio(const std::string& id) const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return nullptr;
    Entrada* e = it->second.entrada.get();
    // Solo pregunta si es un medio: armar el estado entero (las propiedades de
    // todos sus filtros) en cada tic de la interfaz la trababa.
    if (!e->esMedio()) return nullptr;
    return {{"estado", e->estadoMedio()}, {"tiempo_ms", e->tiempoMs()}, {"duracion_ms", e->duracionMs()}};
}

std::string Motor::controlMedio(const std::string& id, const std::string& accion, std::int64_t ms) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::retorno(int n, const std::string& modo, const std::string& propia) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::escuchar(const std::string& modo, const std::string& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

std::string Motor::ponerEscucha(const std::string& id, bool on) {
    std::lock_guard<std::recursive_mutex> l(mu_);
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

void Motor::escuchaDispositivo(const std::string& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (escucha_) escucha_->ponerDispositivo(id);
}

void Motor::escuchaNivel(double lineal) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (escucha_) escucha_->ponerNivel(lineal);
}

bool Motor::ponerLimitador(bool activo, double techoDb) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return programa_ && programa_->ponerLimitador(activo, techoDb);
}

void Motor::ponerTopeEstudio(std::int64_t ms) { reloj_->ponerTope(ms); }

std::string Motor::productorDeBus(const std::string& bus, std::int64_t* latenciaNs) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    Mezcla* m = nullptr;
    if (bus == "programa")
        m = programa_.get();
    else if (bus.rfind("pista", 0) == 0 && bus.size() == 6)
        m = pista(bus[5] - '0', true);
    if (m == nullptr) return {};
    if (latenciaNs != nullptr) *latenciaNs = m->latenciaSalidaNs();
    return m->productor();
}

json Motor::estado() const {
    std::lock_guard<std::recursive_mutex> l(mu_);
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
    return {{"andando", andando()},
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

} // namespace ba::motor
