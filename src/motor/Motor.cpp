// ============================================================================
// src/motor/Motor.cpp — ver Motor.hpp
// ============================================================================
#include "Motor.hpp"
#include "Prioridad.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"
#include "Candado.hpp"
#include "OrigenRed.hpp"
#include "ipc/CanalesSalida.hpp"

#include <gst/net/net.h>

#include <gst/app/gstappsink.h>

#include <algorithm>
#include <chrono>
#include "ajustes/Ajustes.hpp"
#include "platform/Hilo.hpp"

namespace ba::motor {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

Motor::Motor(Config c) : cfg_(std::move(c)) {
    fijarColorSalida(cfg_.lienzo);
    reloj_ = std::make_unique<Reloj>(cfg_.reloj, cfg_.relojServidor, cfg_.relojPuerto, cfg_.relojDominio);
    reloj_->ponerTope(cfg_.topeMs);
    // La salida por omisión existe siempre (también antes de arrancar): lo que no
    // dice a qué salida apunta va a ella.
    salidas_[porOmision_].nombre = traducir("motor", "Principal");
}

Motor::~Motor() { detener(); }

std::string Motor::iniciar() {
    BA_CANDADO(mu_);
    if (andando()) return {};
    prioridadDelProceso(); // una vez: el proceso y la topología al registro
    // LOS PLUGINS DEL COMPOSITOR, CARGADOS ACÁ (detrás del cartel de carga): si no, el primero
    // que los pide es el bucle del motor al poner la primera escena (en Windows, ~500 ms con
    // su candado: cargar las DLL de D3D12 y lo que arrastran).
    {
        const std::int64_t t0 = g_get_monotonic_time();
        int cargados = 0;
        // También los nuestros de las capas (la sombra: gstbafiltros, con todos los filtros): si
        // no, el bucle esperaba a que un trabajador terminara de cargar ese plugin (el registro
        // tiene su candado): la capa con sombra tardaba 425 ms en «crear y enganchar».
        for (const char* f : {"intersrc", "intersink", "d3d12upload", "d3d12convert", "d3d12compositor", "glupload", "glcolorconvert",
                              "gltransformation", "glshader", "glvideomixer", "audiomixer", "imagefreeze", "input-selector",
                              "basombrad3d12", "basombragl", "basombra", "bavideoanillo", "baanillo"})
            if (GstElementFactory* fab = gst_element_factory_find(f)) {
                if (GstPluginFeature* cargada = gst_plugin_feature_load(GST_PLUGIN_FEATURE(fab))) {
                    ++cargados;
                    gst_object_unref(cargada);
                }
                gst_object_unref(fab);
            }
        registrar(Nivel::Info, llenar(traducir("motor", "motor: %1 plugins de la composición cargados en %2 ms"),
                                      {std::to_string(cargados), std::to_string((g_get_monotonic_time() - t0) / 1000)}));
    }
    // EL DRIVER DE LA GPU, ADELANTADO: la primera vez que se arma una capa con sombra en D3D12
    // el driver pasa sus shaders a su código (~420 ms la primera vez después de instalar o
    // actualizar; después lo tiene en su caché). Lo hacía el bucle del motor al poner la
    // primera escena, con su candado; ahora un cuadro pasa por la sombra acá, detrás del
    // cartel de carga (con la caché caliente, unos ms).
    if (gpu() == Gpu::D3D12 && hayFabrica("basombrad3d12") && hayFabrica("d3d12upload")) {
        const std::int64_t t0 = g_get_monotonic_time();
        GstElement* p = gst_parse_launch("videotestsrc num-buffers=2 ! video/x-raw,format=RGBA,width=64,height=64 ! d3d12upload ! "
                                         "basombrad3d12 ! fakesink sync=false", nullptr);
        if (p != nullptr) {
            darContextoGpu(p);
            gst_element_set_state(p, GST_STATE_PLAYING);
            GstBus* bus = gst_element_get_bus(p);
            if (GstMessage* m = gst_bus_timed_pop_filtered(bus, 3 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))
                gst_message_unref(m);
            gst_object_unref(bus);
            gst_element_set_state(p, GST_STATE_NULL);
            gst_object_unref(p);
        }
        registrar(Nivel::Info, llenar(traducir("motor", "motor: la sombra de D3D12, adelantada en %1 ms"),
                                      {std::to_string((g_get_monotonic_time() - t0) / 1000)}));
    }
    escucha_ = std::make_unique<Escucha>(*reloj_, cfg_.sinkEscucha);
    // Las salidas: la por omisión y las que se crearon antes de arrancar.
    for (auto& [sid, s] : salidas_)
        if (const std::string e = armarSalida(sid, s); !e.empty()) {
            if (sid == porOmision_) return e;
            registrar(Nivel::Error, e);
        }
    vistas_ = std::make_unique<Vistas>(*reloj_, [this](const std::string& clave, std::int64_t* lat) -> std::string {
        // Lo llaman pedir (verPedirYa, una orden) y tic (el bucle): con el candado tomado.
        BA_CON_CANDADO(mu_);
        // «programa», «preview», «limpio» (la salida por omisión) o con «:<salida>».
        const std::string bus = clave.substr(0, clave.find(':'));
        if (bus == "programa" || bus == "preview" || bus == "limpio" || clave == "multiview")
            return productorDeVideoYa(clave, lat, "");
        if (clave.rfind("escena:", 0) == 0) {
            auto it = vEscenas_.find(clave.substr(7));
            if (it == vEscenas_.end() || !it->second.comp) return {};
            if (lat != nullptr) *lat = it->second.comp->latenciaSalidaNs();
            return it->second.comp->productor();
        }
        if (clave.rfind("entrada:", 0) == 0 || clave.rfind("entrada-entera:", 0) == 0) {
            // «entrada-entera:»: la toma antes del recorte del PTZ digital (Entrada::productorEntera).
            const bool entera = clave.rfind("entrada-entera:", 0) == 0;
            auto it = canales_.find(clave.substr(entera ? 15 : 8));
            if (it == canales_.end() || !it->second.entrada->daVideo()) return {};
            if (lat != nullptr) *lat = std::max<std::int64_t>(reloj_->latencia(), it->second.entrada->latenciaPropia());
            return entera ? it->second.entrada->productorEntera() : it->second.entrada->productorVideo();
        }
        return {};
    });
    vistas_->ponerDesarmador([this](std::function<void()> f) { desarmarAparte(std::move(f)); });
    seguir_ = true;
    hilo_ = std::thread([this]() { ba::platform::nombrarHilo("motor"); bucle(); });
    hiloCuadros_ = std::thread([this]() { ba::platform::nombrarHilo("motor-cuadros"); cuadros(); });
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
            registrar(Nivel::Info, llenar(traducir("motor", "al apagar quedaron %1 órdenes sin correr"), {std::to_string(cola_.size())}));
        cola_.clear();
    }
    {
        // Las composiciones que se estaban desarmando usan el reloj del motor.
        std::lock_guard<std::mutex> l(desarmesM_);
        for (auto& f : desarmes_) f.wait();
        desarmes_.clear();
    }
    BA_CANDADO(mu_);
    esperanCola_.clear(); // promesas rotas: el que esperaba la cola vuelve
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
    for (auto& [sid, s] : salidas_) {
        desarmarSalida(s, false);
        Salida vacia; // sin nada al aire ni mandos: las entradas se van
        vacia.nombre = std::move(s.nombre);
        s = std::move(vacia);
    }
    vEscenas_.clear();
    matriz_.clear();
    vMulti_.reset();
    mvAux_.clear();
    escucha_.reset();
    retornos_.clear();
    canales_.clear();
    pistas_.clear();
    directo_.reset();
    seleccion_.reset();
    afl_.reset();
}

// ============================================================================
// Las salidas (docs/planes/PLAN-SALIDAS.md, etapa 1)
// ============================================================================

Motor::Salida* Motor::laSalida(const std::string& id) {
    auto it = salidas_.find(id.empty() ? porOmision_ : id);
    return it == salidas_.end() ? nullptr : &it->second;
}

const Motor::Salida* Motor::laSalida(const std::string& id) const {
    auto it = salidas_.find(id.empty() ? porOmision_ : id);
    return it == salidas_.end() ? nullptr : &it->second;
}

const Motor::Mando& Motor::mandoPorOmision(const std::string& id) const {
    static const Mando kNinguno;
    const auto& m = salidas_.at(porOmision_).mandos;
    auto it = m.find(id);
    return it == m.end() ? kNinguno : it->second;
}

std::string Motor::noHaySalida(const std::string& id) const {
    return llenar(traducir("motor", "no hay una salida «%1»"), {id.empty() ? porOmision_ : id});
}

std::string Motor::armarSalida(const std::string& id, Salida& s) {
    // La por omisión con los nombres de siempre; las demás con su id.
    const std::string suf = id == porOmision_ ? std::string() : ":" + id;
    Mezcla::Opciones op;
    op.limitador = true;
    // Sin ebur128level: el loudness del programa lo mide la app (audio::LoudnessPrograma,
    // con su objetivo, el reinicio al salir al aire y el true peak reciente) y el del
    // motor no lo leía nadie (~4 % de un núcleo, siempre).
    op.loudness = false;
    s.mezcla = std::make_unique<Mezcla>(*reloj_, "programa" + suf, op);
    if (!s.mezcla->anda()) {
        const std::string e = llenar(traducir("motor", "el bus PROGRAMA no arrancó: %1"), {s.mezcla->error()});
        s.mezcla.reset();
        return e;
    }
    s.mezcla->ponerLimitador(true, s.techoDb); // el loudness que tenía (ponerLoudness)
    s.mezcla->ponerAgc(s.agc, s.objetivoLufs);
    if (cfg_.video) {
        Composicion::Opciones vop;
        vop.gpu = cfg_.videoGpu;
        Composicion::Opciones pop = vop;
        pop.dsk = true; // el PROGRAMA con DSK y clean feed
        s.programa = std::make_unique<Composicion>(*reloj_, cfg_.lienzo, "programa" + suf, pop);
        s.preview = std::make_unique<Composicion>(*reloj_, cfg_.lienzo, "preview" + suf, vop);
        if (!s.programa->anda())
            registrar(Nivel::Error, llenar(traducir("motor", "el video del PROGRAMA no arrancó: %1"), {s.programa->error()}));
    }
    // Sus mix-minus (los invitados de esta salida no se oyen a sí mismos: F2-02).
    for (const auto& [n, r] : retornos_) armarRetorno(n, id, s);
    for (const auto& [cid, c] : canales_) aplicarEnSalida(s, cid, 0);
    // Su audio al gateway: la por omisión, el de siempre (lo que oyen los invitados en
    // modo «programa»); las demás, el suyo (el gateway lo manda con BA.Salida.<id>).
    armarPuente(s, id == porOmision_ ? std::string("ContentForReturn") : "Salida." + id);
    if (hayOyentes(id)) armarTomaAudio(id); // la pidieron antes de que existiera
    return {};
}

void Motor::armarPuente(Salida& s, const std::string& canal) {
    // El programa de la salida para el gateway, en el anillo BA.Audio.<canal>.
    if (cfg_.prefijoAnillos.empty() || !hayFabrica("baanillosink") || !s.mezcla) return;
    const std::string d = "intersrc producer-name=" + entreComillas(s.mezcla->productor()) +
                          " ! queue max-size-time=" + std::to_string(ajustes::sANs("audio.colaEscuchaS", 2)) + " max-size-buffers=0 max-size-bytes=0 ! baanillosink canal=" +
                          entreComillas(anillo(canal));
    GError* e = nullptr;
    s.puente = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        registrar(Nivel::Aviso,
                  llenar(traducir("motor", "el programa para el gateway: %1"), {ba::diag::utf8Valido(e->message)}));
        g_error_free(e);
        if (s.puente != nullptr) gst_object_unref(s.puente);
        s.puente = nullptr;
        return;
    }
    reloj_->adoptar(s.puente);
    s.puenteLatNs = s.mezcla->latenciaSalidaNs() + margenMezclaNs();
    gst_pipeline_set_latency(GST_PIPELINE(s.puente), static_cast<GstClockTime>(s.puenteLatNs));
    gst_element_set_state(s.puente, GST_STATE_PLAYING);
}

std::shared_future<void> Motor::desarmarSalida(Salida& s, bool aparte) {
    // Todo junto y en orden: lo que la lee (la toma, el puente) antes que lo que la
    // hace. La toma va con su estructura: GStreamer la nombra hasta pasar a NULL.
    auto desarmar = [toma = std::move(s.toma), puente = std::exchange(s.puente, nullptr),
                     programa = std::shared_ptr<Composicion>(std::move(s.programa)),
                     preview = std::shared_ptr<Composicion>(std::move(s.preview)),
                     mezcla = std::shared_ptr<Mezcla>(std::move(s.mezcla)),
                     retornos = std::make_shared<std::map<int, std::unique_ptr<Mezcla>>>(std::move(s.retornos))]() mutable {
        for (GstElement* p : {toma ? toma->pipeline : nullptr, puente})
            if (p != nullptr) {
                gst_element_set_state(p, GST_STATE_NULL);
                gst_object_unref(p);
            }
        toma.reset();
        retornos.reset();
        preview.reset();
        programa.reset();
        mezcla.reset();
    };
    // Las escenas que pedía su DSK (al apagar se van todas juntas).
    if (aparte)
        for (const auto& n : s.dskEscenas) soltarEscena(n);
    s.dskEscenas.clear();
    if (aparte) return desarmarAparte(std::function<void()>(std::move(desarmar)));
    desarmar();
    return {};
}

std::string Motor::crearSalidaYa(const std::string& id, const std::string& nombre) {
    BA_CON_CANDADO(mu_);
    if (id.empty()) return traducir("motor", "falta el id de la salida");
    if (id.find_first_of(":@") != std::string::npos) return traducir("motor", "el id de la salida no puede tener «:» ni «@»");
    if (salidas_.count(id) > 0) return llenar(traducir("motor", "ya hay una salida «%1»"), {id});
    esperarDesarme("salida:" + id); // una borrada con el mismo id: sus productores se llaman igual
    Salida& s = salidas_[id];
    s.nombre = nombre.empty() ? id : nombre;
    // Su mezcla arranca como la de la por omisión, pero sin nada al aire.
    for (const auto& [cid, c] : canales_) {
        Mando k = mandoPorOmision(cid);
        k.enEscena = false;
        s.mandos[cid] = k;
    }
    if (andando()) {
        if (std::string e = armarSalida(id, s); !e.empty()) {
            borrarSalidaYa(id);
            return e;
        }
        actualizarUso();
    }
    return {};
}

std::string Motor::borrarSalidaYa(const std::string& id) {
    BA_CON_CANDADO(mu_);
    if (id.empty() || id == porOmision_) return traducir("motor", "la salida por omisión no se puede borrar");
    auto it = salidas_.find(id);
    if (it == salidas_.end()) return noHaySalida(id);
    if ((escuchaModo_ == "programa" && escuchaId_ == id) || escuchaSalida_ == id) escuchar("apagado");
    std::shared_future<void> f = desarmarSalida(it->second, true);
    {
        std::lock_guard<std::mutex> l(desarmandoM_);
        desarmando_["salida:" + id] = std::move(f);
    }
    salidas_.erase(it);
    actualizarMultiview(); // el tally de lo que tenía al aire
    actualizarUso();
    return {};
}

json Motor::salidas() const {
    const auto f = instantanea();
    return f && f->contains("salidas") ? (*f)["salidas"] : json::object();
}

void Motor::bucle() {
    hiloId_.store(std::this_thread::get_id());
    prioridadDelHilo(ClaseHilo::Motor);
    while (seguir_) {
        DespuesDeLasOrdenes despues;
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
            despues = correrOrdenes();
            // La latencia del estudio: estable (sube si lo piden sostenido; baja con permiso).
            if (reloj_->revisarLatencia(permitirBajar_.load())) {
                const std::string quien = reloj_->laMarca();
                registrar(Nivel::Info,
                          quien.empty() ? llenar(traducir("motor", "latencia del estudio: %1 ms"),
                                                 {std::to_string(reloj_->latencia() / 1000000)})
                                        : llenar(traducir("motor", "latencia del estudio: %1 ms (la marca «%2»)"),
                                                 {std::to_string(reloj_->latencia() / 1000000), quien}));
            }
            if (revisarUso) {
                Tramo t("actualizarUso");
                actualizarUso();
            }
            for (auto& [id, c] : canales_) {
                const std::string donde = llenar(traducir("motor", "tic de la entrada «%1»"), {id}); // si tarda, cuál
                Tramo t(donde.c_str());
                c.entrada->tic();
            }
            {
                Tramo t("tic de los buses de audio");
                for (auto& [sid, s] : salidas_) {
                    if (s.mezcla) s.mezcla->tic();
                    for (auto& [n, m] : s.retornos)
                        if (m) m->tic();
                }
                if (directo_) directo_->tic();
                if (seleccion_) seleccion_->tic();
                if (afl_) afl_->tic();
                for (auto& [n, p] : pistas_) p->tic();
                for (auto& [id, b] : matriz_) {
                    b.mezcla->tic();
                    if (b.salida) b.salida->tic(b.mezcla->latenciaSalidaNs());
                }
            }
            {
                Tramo t("tic de las composiciones");
                for (auto& [sid, s] : salidas_) {
                    if (s.programa) s.programa->tic();
                    if (s.preview) s.preview->tic();
                }
                if (vMulti_) vMulti_->tic();
                for (auto& [n, c] : vEscenas_)
                    if (c.comp) c.comp->tic();
            }
            Tramo tResto("resto de la vuelta (toma de audio, multiview, puente, escucha)");
            for (auto& [sid, s] : salidas_)
                if (s.toma) {
                    GstBus* bus = gst_element_get_bus(s.toma->pipeline);
                    while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                    gst_object_unref(bus);
                }
            for (auto& [n, a] : mvAux_) a->tic();
            if (vistas_) {
                Tramo t("tic de las vistas");
                vistas_->tic();
            }
            if (vueltas % 2 == 0) {
                Tramo t("niveles para los vúmetros");
                publicarNiveles();
            }
            if (revisarUso || fotoPedida_.exchange(false)) {
                Tramo t("foto del estado");
                publicarInstantanea();
            }
            // Los que consumen el programa esperan lo que tarda en salir.
            for (auto& [sid, s] : salidas_) {
                if (s.puente == nullptr || !s.mezcla) continue;
                const std::int64_t lat = s.mezcla->latenciaSalidaNs() + margenMezclaNs();
                if (lat > s.puenteLatNs + static_cast<std::int64_t>(5 * GST_MSECOND) ||
                    lat < s.puenteLatNs - static_cast<std::int64_t>(60 * GST_MSECOND)) {
                    s.puenteLatNs = lat;
                    recalcularLatenciaAparte(s.puente, [p = refCompartida(s.puente), lat]() {
                        gst_pipeline_set_latency(GST_PIPELINE(p.get()), static_cast<GstClockTime>(lat));
                    });
                }
                GstBus* bus = gst_element_get_bus(s.puente);
                while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                gst_object_unref(bus);
            }
            if (escucha_) {
                std::int64_t lat = 0;
                const Salida* oida = escuchaModo_ == "programa" ? laSalida(escuchaId_) : nullptr;
                if (oida != nullptr && oida->mezcla)
                    lat = oida->mezcla->latenciaSalidaNs();
                else if (escuchaModo_ == "directo" && directo_)
                    lat = directo_->latenciaSalidaNs();
                else if (escuchaModo_ == "pfl") {
                    auto c = canales_.find(escuchaId_);
                    lat = c != canales_.end() ? latenciaPfl(*c->second.entrada) : latenciaOidoNs();
                } else if (escuchaModo_ == "afl" && afl_)
                    lat = afl_->latenciaSalidaNs();
                else if (escuchaModo_ == "seleccion" && seleccion_)
                    lat = seleccion_->latenciaSalidaNs();
                escucha_->tic(lat);
            }
        }
        // Las órdenes que fallaron, a quien escucha (la app), SIN el candado: lo que haga
        // con el aviso (mostrarlo, pedir otra orden) no puede volver a pedirlo. Después, los
        // que esperaban la cola.
        despuesDeLasOrdenes(despues);
        // 20 ms, o menos si llega una orden.
        std::unique_lock<std::mutex> lc(colaM_);
        colaCv_.wait_for(lc, std::chrono::milliseconds(20), [this]() { return !cola_.empty() || !seguir_.load(); });
    }
}

void Motor::enBus(Mezcla& m, const std::string& id, const Canal& c, const Mando& k, bool incluir, int fundidoMs) {
    if (!incluir || !c.entrada->daAudio()) {
        m.quitar(id);
        return;
    }
    m.agregar(id);
    m.ponerNivel(id, k.nivel);
    m.ponerMute(id, k.mute);
    m.ponerEscena(id, c.global || k.enEscena ? 1.0 : 0.0, fundidoMs);
}

Mezcla* Motor::pista(int n, bool crear) {
    auto it = pistas_.find(n);
    if (it != pistas_.end()) return it->second.get();
    if (!crear) return nullptr;
    Mezcla::Opciones op;
    op.medidores = false;
    // Con limitador como el programa: una pista se puede publicar (otro idioma) y sin
    // él recortaba en el códec (docs/arquitectura/03-hardware-y-sonido.md §3.5).
    op.limitador = true;
    auto m = std::make_unique<Mezcla>(*reloj_, "pista" + std::to_string(n), op);
    Mezcla* p = m.get();
    p->ponerLimitador(true, techoPistas_);
    pistas_[n] = std::move(m);
    return p;
}

void Motor::aplicar(const std::string& id, int fundidoMs) {
    for (auto& [sid, s] : salidas_) aplicarEnSalida(s, id, fundidoMs);
}

// EL AUDIO POR SALIDA. La entrada se procesa una vez (su cadena, su rack) y entrega
// <id>:audio; cada salida la suma a SU bus con SU mando (fader, mute, en escena).
// Cambiar el mando de una salida toca solo su bus: el fundido de escena de otra
// no se corta.
void Motor::aplicarEnSalida(Salida& s, const std::string& id, int fundidoMs) {
    auto it = canales_.find(id);
    if (it == canales_.end()) return;
    const Canal& c = it->second;
    const Mando& k = s.mandos[id];
    const bool alPrograma = (c.pistas & 1u) != 0;
    if (s.mezcla) enBus(*s.mezcla, id, c, k, alPrograma, fundidoMs);
    // Sus mix-minus: lo mismo que su bus, menos la voz de cada invitado.
    for (auto& [n, m] : s.retornos) {
        auto r = retornos_.find(n);
        if (m && r != retornos_.end()) enBus(*m, id, c, k, alPrograma && r->second.oye(id), fundidoMs);
    }
    if (&s == salidaDeEscucha()) aplicarEscucha(id, c, k);
    if (&s != &porOmision()) return;
    // Por ahora siguen a la salida por omisión (su mando): las pistas.
    for (int n = 2; n <= 6; ++n) {
        const bool va = (c.pistas & (1u << (n - 1))) != 0;
        if (Mezcla* p = pista(n, va)) enBus(*p, id, c, k, va, fundidoMs);
    }
}

void Motor::aplicarEscucha(const std::string& id, const Canal& c, const Mando& k) {
    if (directo_) enBus(*directo_, id, c, k, c.global, 0);
    if (seleccion_) enSeleccion(id, c);
    if (afl_) {
        enBus(*afl_, id, c, k, id == escuchaId_, 0);
        if (id == escuchaId_) afl_->ponerEscena(id, 1.0, 0); // se oye aunque no esté al aire
    }
}

std::string Motor::agregarEntradaYa(const std::string& id, const json& ajustes, const json& opcionesPedidas) {
    BA_CON_CANDADO(mu_);
    const json opciones = opcionesPedidas.is_object() ? opcionesPedidas : json::object(); // {} llega como null
    if (id.empty()) return traducir("motor", "falta el id");
    if (id.find(':') != std::string::npos) return traducir("motor", "el id no puede tener «:»");
    if (canales_.count(id) > 0) return llenar(traducir("motor", "ya hay una entrada «%1»"), {id});
    esperarDesarme(id);
    Canal c;
    c.global = opciones.value("global", false);
    c.interna = opciones.value("interna", false);
    Mando k; // el mismo en todas las salidas
    k.enEscena = opciones.value("en_escena", c.global);
    k.nivel = opciones.value("nivel", 1.0);
    k.mute = opciones.value("mute", false);
    if (opciones.contains("pistas") && opciones["pistas"].is_array()) {
        c.pistas = 0;
        for (const auto& p : opciones["pistas"])
            if (p.is_number_integer() && p.get<int>() >= 1 && p.get<int>() <= 6) c.pistas |= 1u << (p.get<int>() - 1);
    }
    if (!opciones.value("audio", true)) c.pistas = 0;
    c.entrada = std::make_shared<Entrada>(*reloj_, cfg_.lienzo, id, ajustes.is_object() ? ajustes : json::object());
    indexarDeApp(id, c.entrada);
    canales_[id] = std::move(c);
    for (auto& [sid, s] : salidas_) s.mandos[id] = k;
    aplicar(id, 0);
    actualizarMultiview();
    return {};
}

std::string Motor::actualizarEntradaYa(const std::string& id, const json& ajustes) {
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
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
    if (p == nullptr) return llenar(traducir("motor", "no se pudo publicar el reloj en el puerto %1"), {std::to_string(puerto)});
    relojPublicado_ = GST_OBJECT(p);
    return {};
}

std::string Motor::propiedadEntradaYa(const std::string& id, const std::string& elemento, const std::string& prop,
                                    const std::string& valor) {
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
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
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    it->second.entrada->control(campos);
    return {};
}

std::string Motor::quitarEntradaYa(const std::string& id) {
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    for (auto& [sid, s] : salidas_) {
        if (s.mezcla) s.mezcla->quitar(id);
        for (auto& [n, m] : s.retornos)
            if (m) m->quitar(id);
        s.mandos.erase(id);
    }
    if (directo_) directo_->quitar(id);
    if (seleccion_) seleccion_->quitar(id);
    if (afl_) afl_->quitar(id);
    for (auto& [n, p] : pistas_) p->quitar(id);
    if ((escuchaModo_ == "pfl" || escuchaModo_ == "afl") && escuchaId_ == id) escuchar("apagado");
    indexarDeApp(id, nullptr);
    std::shared_ptr<Entrada> e = std::move(it->second.entrada);
    canales_.erase(it);
    soltarEntrada(std::move(e));
    actualizarMultiview();
    return {};
}

std::string Motor::ponerNivelYa(const std::string& id, double lineal, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    if (canales_.count(id) == 0) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    s->mandos[id].nivel = std::clamp(lineal, 0.0, 4.0);
    aplicarEnSalida(*s, id, 0);
    return {};
}

std::string Motor::ponerMuteYa(const std::string& id, bool mute, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    if (canales_.count(id) == 0) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    s->mandos[id].mute = mute;
    if (s == &porOmision()) { // la matriz (sus tomas y sus buses) sigue a la por omisión
        for (auto& [n, t] : tomasEntrada_)
            if (t->id == id) t->mute = mute;
        for (auto& [bid, b] : matriz_)
            if (b.envios.count(id) > 0) b.mezcla->ponerMute(id, mute);
    }
    aplicarEnSalida(*s, id, 0);
    return {};
}

std::string Motor::ponerPistasYa(const std::string& id, const std::vector<int>& pistas) {
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    unsigned m = 0;
    for (int p : pistas) {
        if (p < 1 || p > 6) return traducir("motor", "pista fuera de rango (1…6)");
        m |= 1u << (p - 1);
    }
    it->second.pistas = m;
    aplicar(id, 0);
    return {};
}

void Motor::escenaAlAireYa(const std::vector<std::string>& ids, int fundidoMs, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) return;
    const std::set<std::string> al(ids.begin(), ids.end());
    for (auto& [id, c] : canales_) {
        if (c.global) continue;
        Mando& k = s->mandos[id];
        const bool antes = k.enEscena;
        k.enEscena = al.count(id) > 0;
        if (antes != k.enEscena) aplicarEnSalida(*s, id, fundidoMs);
    }
}

std::string Motor::definirEscenaYa(const json& j) {
    BA_CON_CANDADO(mu_);
    if (!j.is_object()) return traducir("motor", "la escena tiene que ser un objeto {nombre, capas}");
    Escena e = Escena::desdeJson(j);
    if (e.nombre.empty()) return traducir("motor", "falta el nombre de la escena");
    escenas_[e.nombre] = e;
    // Al aire o armada en alguna salida: se actualiza andando (posición, tamaño, recorte, capas).
    for (auto& [sid, s] : salidas_) {
        if (e.nombre == s.alAire && s.programa) {
            s.programa->mostrar(e);
            std::vector<std::string> ids;
            for (const auto& c : e.capas) ids.push_back(c.entrada);
            escenaAlAire(ids, 0, sid);
        }
        if (e.nombre == s.armada && s.preview) s.preview->mostrar(e);
    }
    if (auto it = vEscenas_.find(e.nombre); it != vEscenas_.end() && it->second.comp) {
        // Otro lienzo u otros filtros (no solo sus valores): se rearma; los consumidores
        // la siguen leyendo por el mismo nombre.
        const Lienzo l = lienzoDe(e);
        Composicion& c = *it->second.comp;
        // También otro mezclador (la «Mezcla de GStreamer»: otro módulo, o una propiedad fija).
        if (c.lienzo().ancho != l.ancho || c.lienzo().alto != l.alto || !c.ponerFiltros(e.filtros) || !c.ponerMezclador(e.mezclador)) {
            std::unique_ptr<Composicion> vieja = std::move(it->second.comp);
            it->second.comp = std::make_unique<Composicion>(*reloj_, l, "escena:" + e.nombre, Composicion::Opciones::de(e));
            desarmarAparte(std::move(vieja));
        }
        it->second.comp->mostrar(e);
    }
    actualizarUso();
    return {};
}

std::string Motor::quitarEscenaYa(const std::string& nombre) {
    BA_CON_CANDADO(mu_);
    if (escenas_.erase(nombre) == 0) return llenar(traducir("motor", "no hay una escena «%1»"), {nombre});
    for (auto& [sid, s] : salidas_)
        if (s.armada == nombre) {
            s.armada.clear();
            if (s.preview) s.preview->mostrar(Escena{"", {}});
        }
    return {};
}

std::string Motor::armarYa(const std::string& nombre, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    if (nombre.empty()) { // nada armado
        s->armada.clear();
        if (s->preview) s->preview->mostrar(Escena{"", {}}, "corte", 0);
        actualizarUso();
        return {};
    }
    auto it = escenas_.find(nombre);
    if (it == escenas_.end()) return llenar(traducir("motor", "no hay una escena «%1»"), {nombre});
    s->armada = nombre;
    if (s->preview) s->preview->mostrar(it->second, "corte", 0);
    actualizarMultiview();
    actualizarUso(); // lo armado ya corre entero: el TAKE no la agarra a 2 cuadros/s
    return {};
}

void Motor::actualizarUso() {
    std::set<std::string> uso, leidos;
    const auto sumar = [](std::set<std::string>& a, const std::set<std::string>& s) { a.insert(s.begin(), s.end()); };
    // Con tramos: si tarda, el registro dice cuál parte (llegó a 650 ms al arrancar).
    {
        Tramo t("uso: las composiciones");
        std::vector<const Composicion*> comps{vMulti_.get()};
        for (const auto& [sid, s] : salidas_) {
            comps.push_back(s.programa.get());
            comps.push_back(s.preview.get());
        }
        for (const Composicion* c : comps)
            if (c != nullptr) {
                sumar(uso, c->entradasEnUso());
                sumar(leidos, c->productoresEnUso());
            }
    }
    if (vistas_) {
        Tramo t("uso: las vistas");
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
    for (auto& [id, c] : canales_) {
        Tramo t("uso: poner en uso una entrada");
        c.entrada->ponerEnUso(uso.count(id) > 0);
    }
}

std::string Motor::alAireYa(const std::string& nombre, const std::string& transicion, int ms, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    auto it = escenas_.find(nombre);
    if (it == escenas_.end()) return llenar(traducir("motor", "no hay una escena «%1»"), {nombre});
    if (transicion != "corte" && transicion != "mezcla" && transicion != "empuje" && transicion != "cortina")
        return traducir("motor", "transición: corte, mezcla, empuje o cortina");
    s->alAire = nombre;
    // Con tramos: si tarda, el registro dice cuál parte.
    if (s->programa) {
        Tramo t("alAire: mostrar en el programa");
        s->programa->mostrar(it->second, transicion, ms);
    }
    {
        Tramo t("alAire: multiview");
        actualizarMultiview();
    }
    // El audio de la escena la sigue, con el mismo fundido.
    std::vector<std::string> ids;
    for (const auto& c : it->second.capas)
        if (c.visible) ids.push_back(c.entrada);
    {
        Tramo t("alAire: audio de la escena");
        escenaAlAire(ids, transicion == "corte" ? 0 : ms, salida);
    }
    Tramo t("alAire: uso de las entradas");
    actualizarUso(); // las que entran, a todos sus cuadros ya
    return {};
}

std::string Motor::takeYa(const std::string& transicion, int ms, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    if (s->armada.empty()) return traducir("motor", "no hay nada armado en el preview");
    const std::string antes = s->alAire, armada = s->armada;
    const std::string e = alAire(armada, transicion, ms, salida);
    if (!e.empty()) return e;
    // Lo que estaba al aire queda armado (el take intercambia, como una mesa de corte).
    if (!antes.empty() && escenas_.count(antes) > 0) armar(antes, salida);
    return {};
}

std::string Motor::dskYa(const json& capas, int fundidoMs, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    if (!s->programa) return traducir("motor", "el motor está sin video");
    // Una capa puede ser una entrada o una ESCENA entera ({"escena": "Zócalo"}).
    json lista = capas.is_array() ? capas : json::array();
    std::set<std::string> escenasUsadas;
    for (auto& c : lista) {
        if (!c.is_object() || !c.contains("escena")) continue;
        const std::string n = c.value("escena", std::string());
        if (escenas_.count(n) == 0) return llenar(traducir("motor", "no hay una escena «%1»"), {n});
        escenasUsadas.insert(n);
        if (c.value("entrada", std::string()).empty()) c["entrada"] = "escena:" + n;
    }
    std::vector<Capa> v = Escena::desdeJson(json{{"nombre", "dsk"}, {"capas", lista}}).capas;
    for (auto& c : v) {
        if (c.entrada.rfind("escena:", 0) == 0) continue;
        if (canales_.count(c.entrada) == 0) return llenar(traducir("motor", "no hay una entrada «%1»"), {c.entrada});
    }
    // Las escenas del DSK: se piden las nuevas (antes de soltar las viejas, así no se rearman).
    for (const auto& n : escenasUsadas)
        if (s->dskEscenas.count(n) == 0) usarEscena(n);
    for (auto& c : v)
        if (c.entrada.rfind("escena:", 0) == 0) {
            auto it = vEscenas_.find(c.entrada.substr(7));
            if (it != vEscenas_.end() && it->second.comp) c.productor = it->second.comp->productor();
        }
    const bool ok = s->programa->ponerDsk(v, fundidoMs);
    for (const auto& n : s->dskEscenas)
        if (escenasUsadas.count(n) == 0) soltarEscena(n);
    s->dskEscenas = escenasUsadas;
    return ok ? std::string() : traducir("motor", "el PROGRAMA no tiene DSK");
}

std::string Motor::productorDeVideo(const std::string& clave, std::int64_t* latenciaNs, const std::string& salida) {
    BA_CANDADO(mu_);
    return productorDeVideoYa(clave, latenciaNs, salida);
}

std::string Motor::productorDeVideoYa(const std::string& clave, std::int64_t* latenciaNs, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    // «programa:<salida>» (la clave de una vista) vale lo mismo que «programa» con `salida`.
    const auto dos = clave.find(':');
    const std::string bus = clave.substr(0, dos);
    const Salida* s = laSalida(dos == std::string::npos ? salida : clave.substr(dos + 1));
    if (bus == "limpio" && s != nullptr && s->programa) {
        if (latenciaNs != nullptr) *latenciaNs = s->programa->latenciaSalidaNs();
        return s->programa->productorLimpio();
    }
    const Composicion* c = bus == "multiview" ? vMulti_.get()
                           : s == nullptr     ? nullptr
                           : bus == "programa" ? s->programa.get()
                           : bus == "preview"  ? s->preview.get()
                                               : nullptr;
    if (c == nullptr) return {};
    if (latenciaNs != nullptr) *latenciaNs = c->latenciaSalidaNs();
    return c->productor();
}

std::string Motor::multiviewYa(bool activo, int ancho, int alto) {
    BA_CON_CANDADO(mu_);
    if (!activo) {
        vMulti_.reset();
        mvAux_.clear();
        return {};
    }
    if (!cfg_.video) return traducir("motor", "el motor está sin video");
    mvAncho_ = std::clamp(ancho, 320, 3840) & ~1;
    mvAlto_ = std::clamp(alto, 180, 2160) & ~1;
    Lienzo lz = cfg_.lienzo;
    lz.ancho = mvAncho_;
    lz.alto = mvAlto_;
    vMulti_ = std::make_unique<Composicion>(*reloj_, lz, "multiview");
    if (!vMulti_->anda()) return llenar(traducir("motor", "el multiview no arrancó: %1"), {vMulti_->error()});
    // Los tres colores del tally, compartidos por todos los recuadros. Los ids internos
    // llevan «:», que las entradas de la gente no pueden tener: nunca chocan con una.
    for (const auto& [n, c] : std::vector<std::pair<std::string, std::string>>{
             {"mv:rojo", "#E0242F"}, {"mv:verde", "#2FBF3A"}, {"mv:gris", "#3A3A40"}})
        mvAux_[n] = std::make_unique<Entrada>(*reloj_, lz, n, json{{"tipo", "color"}, {"color", c}, {"ancho", 64}, {"alto", 36}});
    actualizarMultiview();
    return {};
}

void Motor::actualizarMultiview() {
    if (!vMulti_) return;
    const int W = mvAncho_, H = mvAlto_, borde = std::max(2, H / 180);
    // Rojo: al aire en ALGUNA salida. Verde y los dos recuadros de arriba: la por omisión.
    const Salida& po = porOmision();
    std::set<std::string> alAire, armada;
    for (const auto& [sid, s] : salidas_)
        if (auto it = escenas_.find(s.alAire); it != escenas_.end())
            for (const auto& c : it->second.capas) alAire.insert(c.entrada);
    if (auto it = escenas_.find(po.armada); it != escenas_.end())
        for (const auto& c : it->second.capas) armada.insert(c.entrada);
    Escena e;
    e.nombre = "multiview";
    Lienzo lz = cfg_.lienzo;
    const auto nombre = [&](const std::string& celda, const std::string& texto, int x, int y, int w, int h) {
        const std::string id = "mv:nombre:" + celda;
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
        t.entrada = "tally:" + id;
        t.productor = std::string(tally) + ":video";
        t.x = x;
        t.y = y;
        t.ancho = w;
        t.alto = h;
        t.orden = 1;
        e.capas.push_back(t);
        Capa c;
        c.entrada = "celda:" + id;
        c.productor = productor;
        c.x = x + borde;
        c.y = y + borde;
        c.ancho = w - 2 * borde;
        c.alto = h - 2 * borde;
        c.orden = 2;
        e.capas.push_back(c);
        nombre(id, texto, x + borde, y + borde, w - 2 * borde, h - 2 * borde);
    };
    // Las dos fijas con «:» en el id (una entrada que se llame «preview» no las pisa).
    celda(":preview", "video:preview", "PREVIEW" + (po.armada.empty() ? std::string() : " · " + po.armada), "mv:verde", 0, 0, W / 2, H / 2);
    celda(":programa", "video:programa", "PROGRAMA" + (po.alAire.empty() ? std::string() : " · " + po.alAire), "mv:rojo", W / 2, 0, W / 2, H / 2);
    int i = 0;
    for (const auto& [id, c] : canales_) {
        if (!c.entrada->daVideo() || c.interna || i >= 8) continue;
        const char* tally = alAire.count(id) > 0 ? "mv:rojo" : armada.count(id) > 0 ? "mv:verde" : "mv:gris";
        celda(id, c.entrada->productorVideo(), c.entrada->estado().value("nombre", id), tally, (i % 4) * W / 4,
              H / 2 + (i / 4) * H / 4, W / 4, H / 4);
        ++i;
    }
    e.asignarClaves();
    vMulti_->mostrar(e);
}

std::string Motor::verPedirYa(const std::string& clave, int ancho, int alto) {
    // Siempre en este orden: el lock del motor y después el de las vistas (el hilo
    // del motor hace lo mismo en el tic; al revés se trababan).
    BA_CON_CANDADO(mu_);
    if (!vistas_) return traducir("motor", "el motor no está andando");
    const std::string b = clave.substr(0, clave.rfind('@')); // el nombre puede tener «@»; el canal, no
    if (b.rfind("escena:", 0) == 0 && usarEscena(b.substr(7)).empty())
        return llenar(traducir("motor", "no hay una escena «%1»"), {b.substr(7)});
    const std::string e = vistas_->pedir(clave, ancho, alto);
    actualizarUso();
    return e;
}

void Motor::verSoltarYa(const std::string& clave) {
    BA_CON_CANDADO(mu_);
    if (vistas_) vistas_->soltar(clave);
    const std::string b = clave.substr(0, clave.rfind('@')); // el nombre puede tener «@»; el canal, no
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
    BA_CON_CANDADO(mu_);
    auto e = escenas_.find(nombre);
    if (e == escenas_.end() || !cfg_.video) return {};
    CompEscena& c = vEscenas_[nombre];
    if (!c.comp) {
        c.comp = std::make_unique<Composicion>(*reloj_, lienzoDe(e->second), "escena:" + nombre, Composicion::Opciones::de(e->second));
        c.comp->mostrar(e->second);
    }
    ++c.pedidos;
    return c.comp->productor();
}

void Motor::soltarEscenaYa(const std::string& nombre) {
    BA_CON_CANDADO(mu_);
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

void Motor::esperarDesarme(const std::string& clave) {
    // La anterior con esta clave todavía se está desarmando: se la espera (acá,
    // en el hilo del motor), porque sus productores llevan los mismos nombres.
    std::shared_future<void> antes;
    {
        std::lock_guard<std::mutex> l(desarmandoM_);
        auto d = desarmando_.find(clave);
        if (d != desarmando_.end()) {
            antes = d->second;
            desarmando_.erase(d);
        }
    }
    if (antes.valid()) antes.wait();
}

std::shared_future<void> Motor::desarmarAparte(std::function<void()> f) {
    std::lock_guard<std::mutex> l(desarmesM_);
    desarmes_.erase(std::remove_if(desarmes_.begin(), desarmes_.end(),
                                   [](std::shared_future<void>& f) {
                                       return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                                   }),
                    desarmes_.end());
    auto conNombre = [f = std::move(f)] {
        ba::platform::nombrarHilo("motor-desarmar");
        f();
    };
    std::shared_future<void> sf = std::async(std::launch::async, std::move(conNombre)).share();
    desarmes_.push_back(sf);
    return sf;
}

bool Motor::tbarYa(const std::string& nombre, double t, bool soltar, std::string* error, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr) {
        if (error != nullptr) *error = noHaySalida(salida);
        return false;
    }
    auto it = escenas_.find(nombre);
    if (it == escenas_.end() || !s->programa) {
        if (error != nullptr) *error = llenar(traducir("motor", "no hay una escena «%1»"), {nombre});
        return false;
    }
    const bool quedo = s->programa->manual(it->second, t, soltar);
    if (soltar || t >= 1.0) {
        if (quedo) {
            s->alAire = nombre;
            std::vector<std::string> ids;
            for (const auto& c : it->second.capas)
                if (c.visible) ids.push_back(c.entrada);
            escenaAlAire(ids, 200, salida);
            actualizarMultiview();
        }
        return quedo;
    }
    return false;
}

std::string Motor::tbarHacia(const std::string& salida) const {
    const auto f = instantanea(); // sin copiar la foto: la interfaz lo pregunta seguido
    const std::string id = salida.empty() ? porOmision_ : salida;
    if (!f || !f->contains("salidas") || !(*f)["salidas"].contains(id)) return {};
    return (*f)["salidas"][id].value("tbar_hacia", std::string());
}

void Motor::masterVolumenYa(double lineal, int ms, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    if (Salida* s = laSalida(salida); s != nullptr && s->mezcla) s->mezcla->ponerMaster(lineal, ms);
}

GstFlowReturn Motor::alAudio(GstElement* sink, gpointer d) {
    const auto* t = static_cast<const TomaPrograma*>(d);
    Motor* m = t->motor;
    GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (s == nullptr) return GST_FLOW_OK;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo map;
    if (b != nullptr && gst_buffer_map(b, &map, GST_MAP_READ)) {
        std::lock_guard<std::mutex> l(m->tomaMu_);
        for (auto& [id, o] : m->oyentes_)
            if (o.salida == t->salida) o.fn(reinterpret_cast<const float*>(map.data), map.size / (2 * sizeof(float)));
        gst_buffer_unmap(b, &map);
    }
    gst_sample_unref(s);
    return GST_FLOW_OK;
}

bool Motor::hayOyentes(const std::string& salida) {
    std::lock_guard<std::mutex> lt(tomaMu_);
    for (const auto& [id, o] : oyentes_)
        if (o.salida == salida) return true;
    return false;
}

int Motor::escucharPrograma(std::function<void(const float*, std::size_t)> fn, const std::string& salida) {
    const std::string sid = salida.empty() ? porOmision_ : salida;
    int id = 0;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        id = proximoOyente_++;
        oyentes_[id] = Oyente{sid, std::move(fn)};
    }
    orden("escucharPrograma", [this, sid]() {
        armarTomaAudio(sid);
        return std::string();
    });
    return id;
}

void Motor::armarTomaAudio(const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr || s->toma || !s->mezcla) return; // la que todavía no existe la arma al crearse
    const std::string d = "intersrc producer-name=" + entreComillas(s->mezcla->productor()) +
                          " ! queue max-size-time=" + std::to_string(ajustes::sANs("audio.colaEscuchaS", 2)) + " max-size-buffers=0 max-size-bytes=0 ! "
                          "appsink name=s emit-signals=true sync=true max-buffers=0 drop=false";
    GstElement* p = gst_parse_launch(d.c_str(), nullptr);
    if (p == nullptr) return;
    auto t = std::make_shared<TomaPrograma>();
    t->motor = this;
    t->salida = salida.empty() ? porOmision_ : salida;
    t->pipeline = p;
    GstElement* sk = gst_bin_get_by_name(GST_BIN(p), "s");
    g_signal_connect(sk, "new-sample", G_CALLBACK(&Motor::alAudio), t.get());
    gst_object_unref(sk);
    reloj_->adoptar(p);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(s->mezcla->latenciaSalidaNs() + margenMezclaNs()));
    gst_element_set_state(p, GST_STATE_PLAYING);
    s->toma = std::move(t);
}

void Motor::dejarDeEscuchar(int id) {
    std::string sid;
    {
        std::lock_guard<std::mutex> lt(tomaMu_);
        auto it = oyentes_.find(id);
        if (it == oyentes_.end()) return;
        sid = it->second.salida;
        oyentes_.erase(it); // desde acá no se lo llama más (alAudio llama con tomaMu_)
    }
    if (hayOyentes(sid)) return;
    orden("dejarDeEscuchar", [this, sid]() {
        if (hayOyentes(sid)) return std::string(); // volvió alguien mientras tanto
        Salida* s = laSalida(sid);
        if (s == nullptr || !s->toma) return std::string();
        std::shared_ptr<TomaPrograma> t = std::move(s->toma);
        desarmarAparte([t]() {
            gst_element_set_state(t->pipeline, GST_STATE_NULL);
            gst_object_unref(t->pipeline);
        });
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
    BA_CON_CANDADO(mu_);
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
    t->mute = mandoPorOmision(id).mute;
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
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    if (accion == "ir") {
        it->second.entrada->irA(ms);
        return {};
    }
    return it->second.entrada->controlMedio(accion);
}

std::unique_ptr<Mezcla> Motor::busDeOido(const std::string& nombre, std::string anillo) {
    Mezcla::Opciones op;
    op.medidores = false;
    op.latenciaFijaNs = latenciaOidoNs();
    op.anillo = std::move(anillo);
    return std::make_unique<Mezcla>(*reloj_, nombre, op);
}

bool Motor::entraAlOido(const Entrada& e) {
    const double p95 = e.edadAudioP95Ms();
    return std::isnan(p95) || p95 <= static_cast<double>(latenciaOidoNs()) / 1e6;
}

std::int64_t Motor::latenciaPfl(const Entrada& e) const {
    // Lo que tarda su audio (p95) y un poco: una que llega a tiempo al oído, con lo de oído.
    const double p95 = e.edadAudioP95Ms();
    const std::int64_t propia = std::isnan(p95) ? 0 : static_cast<std::int64_t>((p95 + 5.0) * 1e6);
    return std::max(latenciaOidoNs(), std::min(propia, reloj_->latencia() + margenMezclaNs()));
}

bool Motor::armarRetorno(int n, const std::string& sid, Salida& s) {
    s.retornos.erase(n);
    auto r = retornos_.find(n);
    if (r == retornos_.end() || r->second.modo == "apagado" || cfg_.prefijoAnillos.empty() || !s.mezcla) return true;
    // Un bus de oído (F2-01): el invitado que conversa oye ya, no junto con el video.
    // Con limitador: lo que oye el invitado tampoco satura.
    const bool po = sid == porOmision_;
    Mezcla::Opciones op;
    op.limitador = true;
    op.anticipoLimitadorMs = 1.0;
    op.medidores = false;
    op.latenciaFijaNs = latenciaOidoNs();
    op.anillo = anillo(ba::ipc::sufijoRetorno(po ? std::string() : sid, n));
    auto m = std::make_unique<Mezcla>(*reloj_, "retorno" + std::to_string(n) + (po ? std::string() : ":" + sid), op);
    const bool anda = m->anda();
    for (const auto& [id, c] : canales_) {
        auto k = s.mandos.find(id);
        enBus(*m, id, c, k != s.mandos.end() ? k->second : Mando{}, (c.pistas & 1u) != 0 && r->second.oye(id), 0);
    }
    s.retornos[n] = std::move(m);
    return anda;
}

std::string Motor::retornoYa(int n, const std::string& modo, const std::string& propia) {
    BA_CON_CANDADO(mu_);
    if (n < 1 || n > 64) return traducir("motor", "invitado fuera de rango (1…64)");
    if (modo != "programa" && modo != "mix_minus" && modo != "apagado") return traducir("motor", "modo: programa, mix_minus o apagado");
    if (modo == "apagado") {
        retornos_.erase(n);
        for (auto& [sid, s] : salidas_) s.retornos.erase(n);
        return {};
    }
    Retorno& r = retornos_[n];
    if (r.modo == modo && r.propia == propia) {
        bool todos = true; // ya está, en todas las salidas que andan
        for (const auto& [sid, s] : salidas_) todos = todos && (!s.mezcla || s.retornos.count(n) > 0);
        if (todos) return {};
    }
    r.modo = modo;
    r.propia = propia;
    bool anda = true;
    for (auto& [sid, s] : salidas_) anda = armarRetorno(n, sid, s) && anda;
    return anda ? std::string() : traducir("motor", "el retorno no arrancó");
}

std::string Motor::escucharYa(const std::string& modo, const std::string& id, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    if (!escucha_) return traducir("motor", "el motor no está andando");
    // Los buses de oído de la escucha se arman de nuevo con lo que se pide (otra salida, otra
    // entrada): el que no se usa no queda andando.
    const auto soltarOido = [this](const std::string& salvo) {
        if (salvo != "seleccion") seleccion_.reset();
        if (salvo != "directo") directo_.reset();
        if (salvo != "afl") afl_.reset();
    };
    if (modo == "apagado") {
        escucha_->apagar();
        escuchaModo_ = modo;
        escuchaId_.clear();
        escuchaSalida_.clear();
        soltarOido("");
        return {};
    }
    if (modo == "programa") { // `id`: la salida ("" = la por omisión); espera al estudio (va con el video)
        const Salida* s = laSalida(id);
        if (s == nullptr || !s->mezcla) return noHaySalida(id);
        escuchaModo_ = modo;
        escuchaId_ = id == porOmision_ ? std::string() : id;
        escuchaSalida_.clear();
        soltarOido("");
        escucha_->escuchar(s->mezcla->productor(), s->mezcla->latenciaSalidaNs(), Escucha::Margen::Programa);
        return {};
    }
    if (modo == "pfl") { // antes del fader y del bus: lo que entrega la entrada
        auto it = canales_.find(id);
        if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
        if (!it->second.entrada->daAudio()) return llenar(traducir("motor", "«%1» no tiene audio"), {id});
        escuchaModo_ = modo;
        escuchaId_ = id;
        escuchaSalida_.clear();
        soltarOido("");
        escucha_->escuchar(it->second.entrada->productorAudio(), latenciaPfl(*it->second.entrada), Escucha::Margen::Oido);
        return {};
    }
    if (modo != "directo" && modo != "seleccion" && modo != "afl")
        return traducir("motor", "modo: programa, directo, pfl, afl, seleccion o apagado");
    // Los de oído con el mando de una salida.
    Salida* s = laSalida(salida);
    if (s == nullptr) return noHaySalida(salida);
    const std::string sid = salida == porOmision_ ? std::string() : salida;
    if (modo == "afl") {
        auto it = canales_.find(id);
        if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
        if (!it->second.entrada->daAudio()) return llenar(traducir("motor", "«%1» no tiene audio"), {id});
    }
    const bool rehacer = escuchaModo_ != modo || escuchaSalida_ != sid || (modo == "afl" && escuchaId_ != id);
    escuchaModo_ = modo;
    escuchaId_ = modo == "afl" ? id : std::string();
    escuchaSalida_ = sid;
    soltarOido(modo);
    std::unique_ptr<Mezcla>& bus = modo == "directo" ? directo_ : modo == "seleccion" ? seleccion_ : afl_;
    if (rehacer || !bus) {
        bus = busDeOido(modo);
        for (const auto& [cid, c] : canales_) aplicarEscucha(cid, c, s->mandos[cid]);
    }
    escucha_->escuchar(bus->productor(), bus->latenciaSalidaNs(), Escucha::Margen::Oido);
    return {};
}

void Motor::enSeleccion(const std::string& id, const Canal& c) {
    if (!c.escucha || !c.entrada->daAudio()) {
        seleccion_->quitar(id);
        return;
    }
    // En los auriculares aunque no esté al aire (sin el fundido de escena), con el fader
    // y el mute de la salida que sigue la escucha.
    Mando m;
    if (const Salida* s = salidaDeEscucha())
        if (auto k = s->mandos.find(id); k != s->mandos.end()) m = k->second;
    seleccion_->agregar(id);
    seleccion_->ponerNivel(id, m.nivel);
    seleccion_->ponerMute(id, m.mute);
    seleccion_->ponerEscena(id, 1.0, 0);
}

std::string Motor::ponerEscuchaYa(const std::string& id, bool on) {
    BA_CON_CANDADO(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return llenar(traducir("motor", "no hay una entrada «%1»"), {id});
    it->second.escucha = on;
    bool alguna = false;
    for (const auto& [cid, c] : canales_) alguna = alguna || c.escucha;
    if (!alguna) return escuchaModo_ == "seleccion" ? escuchar("apagado") : std::string();
    if (escuchaModo_ != "seleccion") return escuchar("seleccion");
    enSeleccion(id, it->second);
    return {};
}

void Motor::escuchaDispositivoYa(const std::string& id) {
    BA_CON_CANDADO(mu_);
    if (escucha_) escucha_->ponerDispositivo(id);
}

void Motor::escuchaNivelYa(double lineal) {
    BA_CON_CANDADO(mu_);
    if (escucha_) escucha_->ponerNivel(lineal);
}

bool Motor::ponerLimitadorYa(bool activo, double techoDb, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    return s != nullptr && s->mezcla && s->mezcla->ponerLimitador(activo, techoDb);
}

std::string Motor::ponerLoudnessYa(bool agc, double objetivoLufs, double techoDb, const std::string& salida) {
    BA_CON_CANDADO(mu_);
    Salida* s = laSalida(salida);
    if (s == nullptr || !s->mezcla) return noHaySalida(salida);
    s->agc = agc;
    s->objetivoLufs = objetivoLufs;
    s->techoDb = techoDb;
    if (!s->mezcla->ponerLimitador(true, techoDb) || !s->mezcla->ponerAgc(agc, objetivoLufs))
        return traducir("motor", "el programa no tiene limitador (falta balimitador)");
    // Las pistas 2-6 siguen a la salida por omisión: el mismo techo (si se publican como
    // alternativa, otro idioma, pasan por el mismo códec). Sin AGC: son mezclas aparte.
    if (s == laSalida(porOmision_))
        for (auto& [n, p] : pistas_) p->ponerLimitador(true, techoDb);
    techoPistas_ = techoDb;
    return {};
}

void Motor::ponerTopeEstudio(std::int64_t ms) { reloj_->ponerTope(ms); }

std::string Motor::productorDeBus(const std::string& bus, std::int64_t* latenciaNs, const std::string& salida) {
    BA_CANDADO(mu_);
    Mezcla* m = nullptr;
    if (bus == "programa" || bus.rfind("programa:", 0) == 0) {
        Salida* s = laSalida(bus == "programa" ? salida : bus.substr(9));
        m = s != nullptr ? s->mezcla.get() : nullptr;
    } else if (bus.rfind("pista", 0) == 0 && bus.size() == 6)
        m = pista(bus[5] - '0', true);
    if (m == nullptr) return {};
    if (latenciaNs != nullptr) *latenciaNs = m->latenciaSalidaNs();
    return m->productor();
}

std::shared_ptr<const std::vector<Motor::NivelAudio>> Motor::niveles() const {
    std::lock_guard<std::mutex> l(nivelesM_);
    return niveles_;
}

void Motor::publicarNiveles() {
    auto v = std::make_shared<std::vector<NivelAudio>>();
    v->reserve(canales_.size() + pistas_.size() + (retornos_.size() + 1) * salidas_.size());
    for (const auto& [id, c] : canales_) {
        if (!c.entrada || !c.entrada->daAudio()) continue;
        NivelAudio n;
        n.id = id;
        c.entrada->nivelAudio(n.pico, n.rms);
        v->push_back(std::move(n));
    }
    const auto deBus = [&](const std::string& id, const Mezcla* m) {
        if (m == nullptr) return;
        NivelAudio n;
        n.id = id;
        n.bus = true;
        double p[2], r[2];
        m->nivelMaestro(p, r);
        for (int k = 0; k < 2; ++k) {
            n.pico[k] = static_cast<float>(std::max(-100.0, p[k]));
            n.rms[k] = static_cast<float>(std::max(-100.0, r[k]));
        }
        v->push_back(std::move(n));
    };
    for (const auto& [sid, s] : salidas_) deBus(sid == porOmision_ ? "programa" : "programa:" + sid, s.mezcla.get());
    for (const auto& [num, m] : pistas_) deBus("pista" + std::to_string(num), m.get());
    for (const auto& [id, b] : matriz_) deBus("matriz:" + id, b.mezcla.get());
    for (const auto& [sid, s] : salidas_)
        for (const auto& [num, m] : s.retornos) deBus(m ? m->nombre() : std::string(), m.get());
    std::shared_ptr<const std::vector<NivelAudio>> listo = std::move(v);
    std::lock_guard<std::mutex> l(nivelesM_);
    niveles_ = std::move(listo);
}

std::shared_ptr<const json> Motor::instantanea() const {
    std::lock_guard<std::mutex> l(instantaneaM_);
    return instantanea_;
}

void Motor::publicarInstantanea() {
    auto foto = std::make_shared<const json>(estadoYa()); // del bucle, con el candado
    std::lock_guard<std::mutex> l(instantaneaM_);
    instantanea_ = std::move(foto);
}

std::string Motor::analizarEmpezar(const std::string& entrada) {
    // La composición del programa y la mezcla también: lo que pasa después de las entradas.
    if (entrada == "programa" || entrada == "programa:audio") {
        GstElement* p = nullptr;
        {
            BA_CANDADO(mu_);
            const Salida& po = porOmision(); // por ahora, la salida por omisión
            if (entrada == "programa" && po.programa) p = po.programa->pipelineRef();
            if (entrada == "programa:audio" && po.mezcla) p = po.mezcla->pipelineRef();
        }
        if (p == nullptr) return llenar(traducir("motor", "«%1» no está andando"), {entrada});
        auto s = std::make_unique<Sondas>(p); // se queda con sus elementos
        gst_object_unref(p);
        std::lock_guard<std::mutex> l(sondasM_);
        sondasAparte_[entrada] = std::move(s);
        return {};
    }
    std::shared_ptr<Entrada> e;
    {
        BA_CANDADO(mu_);
        const auto it = canales_.find(entrada);
        if (it == canales_.end() || !it->second.entrada) return llenar(traducir("motor", "no existe la entrada «%1»"), {entrada});
        e = it->second.entrada;
    }
    e->analizarEmpezar();
    return {};
}

json Motor::analizarTerminar(const std::string& entrada) {
    {
        std::unique_ptr<Sondas> s;
        {
            std::lock_guard<std::mutex> l(sondasM_);
            const auto it = sondasAparte_.find(entrada);
            if (it != sondasAparte_.end()) {
                s = std::move(it->second);
                sondasAparte_.erase(it);
            }
        }
        if (s) return s->terminar();
        if (entrada == "programa" || entrada == "programa:audio") return {{"error", llenar(traducir("motor", "no hay un análisis en curso en «%1»"), {entrada})}};
    }
    std::shared_ptr<Entrada> e;
    {
        BA_CANDADO(mu_);
        const auto it = canales_.find(entrada);
        if (it == canales_.end() || !it->second.entrada) return {{"error", llenar(traducir("motor", "no existe la entrada «%1»"), {entrada})}};
        e = it->second.entrada;
    }
    return e->analizarTerminar();
}

json Motor::estado() const {
    BA_CANDADO(mu_);
    return estadoYa();
}

json Motor::estadoYa() const {
    BA_CON_CANDADO(mu_);
    json entradas = json::object();
    for (const auto& [id, c] : canales_) {
        json e = c.entrada->estado();
        const Mando& k = mandoPorOmision(id); // los de cada salida, en «salidas»
        e["global"] = c.global;
        e["entra_al_oido"] = entraAlOido(*c.entrada);
        e["en_escena"] = k.enEscena;
        e["nivel"] = k.nivel;
        e["mute"] = k.mute;
        json ps = json::array();
        for (int n = 1; n <= 6; ++n)
            if ((c.pistas & (1u << (n - 1))) != 0) ps.push_back(n);
        e["pistas"] = ps;
        entradas[id] = std::move(e);
    }
    // Cada retorno con lo que pidió y cómo anda en cada salida («anda»: el de la por omisión).
    json ret = json::object();
    for (const auto& [n, r] : retornos_) {
        json porSalida = json::object();
        for (const auto& [sid, s] : salidas_) {
            auto m = s.retornos.find(n);
            const bool anda = m != s.retornos.end() && m->second && m->second->anda();
            porSalida[sid] = {{"anda", anda},
                              {"canal", ba::ipc::sufijoRetorno(sid == porOmision_ ? std::string() : sid, n)},
                              {"latencia_ms", anda ? static_cast<double>(m->second->latenciaSalidaNs()) / 1e6 : 0.0}};
        }
        ret[std::to_string(n)] = {{"modo", r.modo}, {"propia", r.propia},
                                  {"anda", porSalida[porOmision_].value("anda", false)}, {"salidas", porSalida}};
    }
    // Los buses de oído: cuánto esperan y qué entradas no llegan a tiempo (no se oyen en
    // los retornos ni en la escucha de oído: F2-01).
    json fuera = json::array();
    for (const auto& [id, c] : canales_)
        if (c.entrada->daAudio() && !entraAlOido(*c.entrada)) fuera.push_back(id);
    const json oido = {{"latencia_ms", static_cast<double>(latenciaOidoNs()) / 1e6}, {"no_entran", fuera}};
    json pistas = json::object();
    for (const auto& [n, p] : pistas_) pistas[std::to_string(n)] = p->estado();
    json tarde = json::array();
    for (const auto& [id, c] : canales_)
        if (c.entrada->llegaTarde()) tarde.push_back(id);
    json esc = escucha_ ? escucha_->estado() : json::object();
    esc["modo"] = escuchaModo_;
    if (!escuchaId_.empty()) esc[escuchaModo_ == "programa" ? "salida" : "entrada"] = escuchaId_;
    if (!escuchaSalida_.empty()) esc["salida"] = escuchaSalida_;
    esc["de_oido"] = escuchaModo_ != "programa" && escuchaModo_ != "apagado";
    json medios = json::object();
    for (const auto& [id, c] : canales_)
        if (c.entrada->esMedio()) medios[id] = medioYa(id);
    // Cada salida con lo suyo; la por omisión, además, en los campos de siempre.
    json salidas = json::object();
    for (const auto& [sid, s] : salidas_) {
        json enEscena = json::array(), mandos = json::object();
        for (const auto& [id, k] : s.mandos) {
            mandos[id] = {{"nivel", k.nivel}, {"mute", k.mute}, {"en_escena", k.enEscena}};
            if (k.enEscena) enEscena.push_back(id);
        }
        salidas[sid] = {{"nombre", s.nombre},
                        {"por_omision", sid == porOmision_},
                        {"al_aire", s.alAire},
                        {"armada", s.armada},
                        {"tbar_hacia", s.programa ? s.programa->manualHacia() : std::string()},
                        {"dsk", s.dskEscenas},
                        {"en_escena", enEscena},
                        {"mandos", mandos},
                        {"programa", s.mezcla ? s.mezcla->estado() : json()},
                        {"video", {{"programa", s.programa ? s.programa->estado() : json()},
                                   {"preview", s.preview ? s.preview->estado() : json()}}}};
    }
    const json& po = salidas[porOmision_];
    return {{"andando", andando()},
            {"medios", medios},
            {"salidas", salidas},
            {"tbar_hacia", po["tbar_hacia"]},
            {"estudio",
             {{"latencia_ms", static_cast<double>(reloj_->latencia()) / 1e6},
              {"objetivo_ms", static_cast<double>(reloj_->objetivo()) / 1e6},
              {"la_marca", reloj_->laMarca()},
              {"puede_bajarse", reloj_->puedeBajarse()},
              {"baja_sola", permitirBajar_.load()},
              {"tope_ms", reloj_->topeMs()},
              {"tope_automatico", reloj_->topeAutomatico()},
              {"llegan_tarde", tarde}}},
            {"lienzo",
             {{"ancho", cfg_.lienzo.ancho},
              {"alto", cfg_.lienzo.alto},
              {"fps", static_cast<double>(cfg_.lienzo.fpsN) / std::max(1, cfg_.lienzo.fpsD)},
              // F2-17: la red eléctrica del país del sistema, si el lienzo parpadea con ella y
              // las cadencias que le van (no se cambia nada solo).
              {"red_hz", redElectricaDelSistema()},
              {"avisos", avisosDeLaRed(cfg_.lienzo, redElectricaDelSistema())},
              {"cadencias_de_la_red", [] {
                   json c = json::array();
                   for (const auto& [n, d] : cadenciasDeLaRed(redElectricaDelSistema())) c.push_back(std::to_string(n) + "/" + std::to_string(d));
                   return c;
               }()}}},
            {"programa", po["programa"]},
            {"video",
             {{"al_aire", po["al_aire"]},
              {"armada", po["armada"]},
              {"escenas", [&]() {
                   json n = json::array();
                   for (const auto& [k, e] : escenas_) n.push_back(k);
                   return n;
               }()},
              {"programa", po["video"]["programa"]},
              {"preview", po["video"]["preview"]},
              // Las composiciones de escena que no andan, con su motivo (la app lo muestra en la
              // tarjeta del compositor o de la Mezcla de GStreamer, no solo en el registro).
              {"errores_escenas", [&]() {
                   json m = json::object();
                   for (const auto& [k, v] : vEscenas_)
                       if (v.comp && !v.comp->error().empty()) m[k] = v.comp->error();
                   return m;
               }()}}},
            {"pistas", pistas},
            {"retornos", ret},
            {"oido", oido},
            {"escucha", esc},
            {"matriz", [&]() {
                 json m = json::object();
                 for (const auto& [id, b] : matriz_) {
                     json envios = json::object();
                     for (const auto& [ent, g] : b.envios) envios[ent] = {{"izq", g.first}, {"der", g.second}};
                     m[id] = {{"productor", b.mezcla->productor()},
                              {"pico_db", b.mezcla->picoDb()},
                              {"latencia_ms", b.mezcla->latenciaSalidaNs() / 1'000'000},
                              {"envios", envios},
                              {"dispositivo", b.dispositivo},
                              {"salida", b.salida ? b.salida->estado() : json()}};
                 }
                 return m;
             }()},
            {"vistas", vistas_ ? vistas_->estado() : json::object()},
            {"procesador", topologiaCpu()},
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
// devuelve el resultado de verdad. Quien ya tiene el candado (una orden que llama
// a otra, iniciar armando las salidas) la corre ahí mismo, sin volver a pedirlo: el
// candado es un mutex común (Candado.hpp, F3-14) y este es el único lugar donde se
// anida, a la vista.
// ============================================================================

bool Motor::enHiloDelMotor() const { return hiloId_.load() == std::this_thread::get_id(); }

std::string Motor::orden(const char* que, std::function<std::string()> f) {
    if (mu_.esMio()) return f(); // anidada: ya corre con el candado
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
    if (enHiloInterfaz()) registrar(Nivel::Aviso, traducir("motor", "esperarCola desde el hilo de la interfaz: no tendría que esperar"));
    auto listo = std::make_shared<std::promise<void>>();
    std::future<void> f = listo->get_future();
    // Se suelta después de que avisan las fallas de lo anterior (despuesDeLasOrdenes).
    orden("esperarCola", [this, listo]() {
        esperanCola_.push_back(listo);
        return std::string();
    });
    f.wait();
}

Motor::DespuesDeLasOrdenes Motor::correrOrdenes() {
    DespuesDeLasOrdenes d;
    std::deque<std::pair<const char*, std::function<std::string()>>> ordenes;
    {
        std::lock_guard<std::mutex> l(colaM_);
        ordenes.swap(cola_);
    }
    if (ordenes.empty()) return d;
    for (auto& [que, f] : ordenes) {
        Tramo t(que);
        const std::string e = f();
        if (e.empty()) continue;
        registrar(Nivel::Aviso, llenar(traducir("motor", "orden %1: %2"), {que, e}));
        d.fallas.emplace_back(que, e);
    }
    d.esperan.swap(esperanCola_);
    fotoPedida_.store(true); // que la foto muestre lo que cambió
    return d;
}

void Motor::despuesDeLasOrdenes(DespuesDeLasOrdenes& d) {
    if (!d.fallas.empty()) {
        std::function<void(const std::string&, const std::string&)> fn;
        {
            std::lock_guard<std::mutex> l(colaM_);
            fn = alFallar_;
        }
        if (fn)
            for (const auto& [que, e] : d.fallas) fn(que, e);
    }
    for (auto& p : d.esperan) p->set_value();
}

// --- Lo que llama la interfaz: se encola ---------------------------------------

std::string Motor::crearSalida(const std::string& id, const std::string& nombre) {
    return orden("crearSalida", [=, this]() { return crearSalidaYa(id, nombre); });
}
std::string Motor::borrarSalida(const std::string& id) {
    return orden("borrarSalida", [=, this]() { return borrarSalidaYa(id); });
}

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
std::string Motor::ponerNivel(const std::string& id, double lineal, const std::string& salida) {
    return orden("ponerNivel", [=, this]() { return ponerNivelYa(id, lineal, salida); });
}
std::string Motor::ponerMute(const std::string& id, bool mute, const std::string& salida) {
    return orden("ponerMute", [=, this]() { return ponerMuteYa(id, mute, salida); });
}
std::string Motor::ponerPistas(const std::string& id, const std::vector<int>& pistas) {
    return orden("ponerPistas", [=, this]() { return ponerPistasYa(id, pistas); });
}
void Motor::escenaAlAire(const std::vector<std::string>& ids, int fundidoMs, const std::string& salida) {
    orden("escenaAlAire", [=, this]() {
        escenaAlAireYa(ids, fundidoMs, salida);
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
std::string Motor::armar(const std::string& nombre, const std::string& salida) {
    return orden("armar", [=, this]() { return armarYa(nombre, salida); });
}
std::string Motor::take(const std::string& transicion, int ms, const std::string& salida) {
    return orden("take", [=, this]() { return takeYa(transicion, ms, salida); });
}
std::string Motor::alAire(const std::string& nombre, const std::string& transicion, int ms, const std::string& salida) {
    return orden("alAire", [=, this]() { return alAireYa(nombre, transicion, ms, salida); });
}
std::string Motor::dsk(const json& capas, int fundidoMs, const std::string& salida) {
    return orden("dsk", [=, this]() { return dskYa(capas, fundidoMs, salida); });
}
bool Motor::tbar(const std::string& nombre, double t, bool soltar, std::string* error, const std::string& salida) {
    // Lo que va a pasar se sabe sin esperar (Composicion::manual): termina con
    // t >= 1 o al soltar, y queda la nueva si se soltó pasada la mitad.
    const std::string e = orden("tbar", [=, this]() {
        std::string err;
        tbarYa(nombre, t, soltar, &err, salida);
        return err;
    });
    if (error != nullptr) *error = e;
    pedirFoto();
    return e.empty() && (t >= 1.0 || (soltar && t >= 0.5));
}
std::string Motor::usarEscena(const std::string& nombre) {
    orden("usarEscena", [=, this]() {
        return usarEscenaYa(nombre).empty() ? llenar(traducir("motor", "no hay una escena «%1»"), {nombre}) : std::string();
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
std::string Motor::escuchar(const std::string& modo, const std::string& id, const std::string& salida) {
    return orden("escuchar", [=, this]() { return escucharYa(modo, id, salida); });
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
std::string Motor::matrizBus(const std::string& id, const std::vector<EnvioMatriz>& envios, const std::string& dispositivo) {
    return orden("matrizBus", [=, this]() { return matrizBusYa(id, envios, dispositivo); });
}
std::string Motor::quitarMatrizBus(const std::string& id) {
    return orden("quitarMatrizBus", [=, this]() { return quitarMatrizBusYa(id); });
}

std::string Motor::matrizBusYa(const std::string& id, const std::vector<EnvioMatriz>& envios, const std::string& dispositivo) {
    BA_CON_CANDADO(mu_);
    if (id.empty()) return traducir("motor", "un bus de la Matriz sin id");
    BusMatriz& b = matriz_[id];
    if (!b.mezcla) {
        // Un bus de OÍDO (como los retornos): lo que pasa por la Matriz se oye ya, no espera
        // al video del programa.
        Mezcla::Opciones op;
        op.medidores = false;
        op.latenciaFijaNs = latenciaOidoNs();
        op.panorama = true;
        b.mezcla = std::make_unique<Mezcla>(*reloj_, "matriz-" + id, op);
        if (!b.mezcla->anda()) {
            const std::string e = b.mezcla->error();
            matriz_.erase(id);
            return e;
        }
    }
    // Solo lo que cambió: una rama que se agrega o se va no corta a las demás.
    std::map<std::string, std::pair<double, double>> nuevos;
    for (const auto& e : envios) nuevos[e.entrada] = {e.izq, e.der};
    for (const auto& [ent, g] : b.envios)
        if (nuevos.count(ent) == 0) b.mezcla->quitar(ent);
    for (const auto& [ent, g] : nuevos) {
        const auto it = b.envios.find(ent);
        if (it == b.envios.end()) {
            b.mezcla->agregar(ent);
            // El mute de la entrada (el de la salida por omisión) vale también acá, como valía
            // en la toma que alimentaba la matriz de la app.
            b.mezcla->ponerMute(ent, mandoPorOmision(ent).mute);
        }
        if (it == b.envios.end() || it->second != g) b.mezcla->ponerCanales(ent, g.first, g.second);
    }
    b.envios = std::move(nuevos);
    if (dispositivo.empty()) {
        // Sin placa (una entrada virtual, o una salida sin dispositivo): solo el productor.
        if (b.salida) {
            std::shared_ptr<Escucha> vieja(std::move(b.salida));
            desarmarAparte([vieja]() mutable { vieja.reset(); }); // soltar la placa puede tardar
        }
        b.dispositivo.clear();
        return {};
    }
    if (!b.salida) {
        // Como la escucha: su id estable, la del sistema si no está (y vuelve con el Vigía),
        // se rearma si el sink se cae, sus buffers explícitos (BuffersAudio).
        // Sin respaldo: con la placa ausente, muda hasta que vuelva (no a los parlantes).
        b.salida = std::make_unique<Escucha>(*reloj_, cfg_.sinkEscucha);
        b.salida->sinRespaldo(true);
        b.salida->ponerDispositivo(dispositivo == "default" ? std::string() : dispositivo);
        b.salida->escuchar(b.mezcla->productor(), b.mezcla->latenciaSalidaNs(), Escucha::Margen::Oido);
    } else if (dispositivo != b.dispositivo) {
        b.salida->ponerDispositivo(dispositivo == "default" ? std::string() : dispositivo);
    }
    b.dispositivo = dispositivo;
    return {};
}

std::string Motor::quitarMatrizBusYa(const std::string& id) {
    BA_CON_CANDADO(mu_);
    auto it = matriz_.find(id);
    if (it == matriz_.end()) return {};
    if (it->second.salida) {
        std::shared_ptr<Escucha> vieja(std::move(it->second.salida));
        desarmarAparte([vieja]() mutable { vieja.reset(); });
    }
    matriz_.erase(it);
    return {};
}

void Motor::escuchaNivel(double lineal) {
    orden("escuchaNivel", [=, this]() {
        escuchaNivelYa(lineal);
        return std::string();
    });
}
void Motor::masterVolumen(double lineal, int ms, const std::string& salida) {
    orden("masterVolumen", [=, this]() {
        masterVolumenYa(lineal, ms, salida);
        return std::string();
    });
}
bool Motor::ponerLimitador(bool activo, double techoDb, const std::string& salida) {
    return orden("ponerLimitador", [=, this]() {
               return ponerLimitadorYa(activo, techoDb, salida) ? std::string() : traducir("motor", "no se pudo poner el limitador");
           }).empty();
}
std::string Motor::ponerLoudness(bool agc, double objetivoLufs, double techoDb, const std::string& salida) {
    return orden("ponerLoudness", [=, this]() { return ponerLoudnessYa(agc, objetivoLufs, techoDb, salida); });
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
