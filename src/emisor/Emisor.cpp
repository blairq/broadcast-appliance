// src/emisor/Emisor.cpp — ver el .hpp

#include "Emisor.hpp"

#include "Piezas.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace ba::emisor {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;
using motor::entreComillas;
using motor::Nivel;
using motor::registrar;

namespace {

std::int64_t ahoraUs() { return g_get_monotonic_time(); }
std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

/// Lo que tarda en estar lista para codificar cada rama: el sondeo del canal (4 ms),
/// el cuadro que retiene videorate, la conversión; el audio, lo que espera el
/// audiomixer. Las salidas suman su codificador y su margen por tipo (F2-06).
constexpr std::int64_t kLatEntradaNs = 120'000'000;
constexpr std::int64_t kEsperaMezclaNs = 60'000'000;

std::string lanzar(const std::string& d, GstElement** p) {
    GError* e = nullptr;
    *p = gst_parse_launch(d.c_str(), &e);
    if (e == nullptr) return {};
    std::string m = ba::diag::utf8Valido(e->message);
    g_error_free(e);
    if (*p != nullptr) gst_object_unref(*p);
    *p = nullptr;
    return m;
}

/// Lo pedido de una grabación para comparar: su configuración y las pistas que suma.
json cfgGrabacion(const std::optional<motor::Grabacion::Config>& g, const std::vector<int>& pistas) {
    if (!g) return json();
    json j = aJson(*g);
    j["pistas"] = pistas;
    return j;
}

} // namespace

Emisor::Emisor(motor::Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {
    if (cfg_.canalVideo.empty()) cfg_.canalVideo = ba::ipc::canalVideoEmisor(cfg_.id);
    if (cfg_.canalAudio.empty()) cfg_.canalAudio = ba::ipc::canalAudioEmisor(cfg_.id);
    if (cfg_.canalLimpio.empty()) cfg_.canalLimpio = ba::ipc::canalVideoLimpioEmisor(cfg_.id);
    video_.canal = cfg_.canalVideo;
    video_.productor = "video:emisor:" + cfg_.id;
    limpio_.canal = cfg_.canalLimpio;
    limpio_.productor = "video:emisor:" + cfg_.id + ":limpio";
    audio_.canal = cfg_.canalAudio;
    audio_.productor = "bus:emisor:" + cfg_.id;
}

Emisor::~Emisor() {
    parar();
    desarmar(video_.rama, &video_.edad);
    desarmar(limpio_.rama, &limpio_.edad);
    desarmar(audio_.rama);
    for (auto& [n, a] : pistas_) desarmar(a->rama);
}

motor::Fuentes Emisor::fuentes() const {
    motor::Fuentes f;
    f.video = video_.productor;
    f.latVideoNs = kLatEntradaNs;
    f.audios = {audio_.productor};
    f.latAudioNs = kLatEntradaNs;
    return f;
}

motor::Fuentes Emisor::fuentesGrabacion() const {
    motor::Fuentes f = fuentes();
    for (const auto& [n, a] : pistas_) f.audios.push_back(a->productor); // en orden: la 2, la 3…
    return f;
}

motor::Fuentes Emisor::fuentesLimpio() const {
    motor::Fuentes f = fuentes();
    f.video = limpio_.productor;
    return f;
}

std::string Emisor::textoPlaca() const {
    return pedido_.placaTexto.empty() ? traducir("emisor", "Volvemos enseguida") : pedido_.placaTexto;
}

// --- Las entradas -------------------------------------------------------------------

void Emisor::armarVideo(Video& v) {
    const Pedido& p = pedido_;
    const std::string tam = ",width=" + std::to_string(p.ancho) + ",height=" + std::to_string(p.alto) +
                            ",pixel-aspect-ratio=1/1,framerate=" + std::to_string(p.fpsN) + "/" + std::to_string(p.fpsD);
    const std::string caps = "video/x-raw,format=NV12" + tam;
    const std::string texto = textoPlaca();
    // Lo que publica la app, al tamaño y los cuadros del pedido (el canal ya viene en NV12
    // y de ese tamaño: videoconvert y videoscale lo dejan pasar). videorate sin rellenar
    // huecos: cuando la app vuelve, el primer cuadro sale solo (no cientos de copias del
    // último de antes de la caída, ver referencias-mezcla-gstreamer).
    const std::string senal = "bavideoanillo name=anillo canal=" + entreComillas(v.canal) +
                              " ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream"
                              " ! videoconvert ! videoscale ! videorate skip-to-first=true max-duplication-time=" +
                              std::to_string(200 * GST_MSECOND) + " ! " + caps + " ! sel.sink_0";
    // La placa: UN cuadro rotulado (se convierte una vez) que imagefreeze repite en vivo.
    const std::string placa = "videotestsrc num-buffers=1 pattern=solid-color foreground-color=0xff1c1c22 ! video/x-raw,format=BGRA" +
                              tam + " ! textoverlay text=" + entreComillas(texto) + " font-desc=" +
                              entreComillas("Sans Bold " + std::to_string(std::max(16, p.alto / 24))) +
                              " halignment=center valignment=center ! videoconvert ! " + caps +
                              " ! imagefreeze is-live=true ! sel.sink_1";
    const std::string d = "input-selector name=sel sync-streams=false cache-buffers=false ! intersink name=sale producer-name=" +
                          entreComillas(v.productor) + " sync=false event-types=\"<>\" " + senal + " " + placa;
    GstElement* pl = nullptr;
    if (std::string e = lanzar(d, &pl); !e.empty()) {
        v.rama.error = e;
        v.rama.reintentoNs = ahoraNs() + GST_SECOND;
        registrar(Nivel::Aviso, llenar(traducir("emisor", "el video de entrada no se pudo armar: %1"), {e}));
        return;
    }
    // Arranca en la placa hasta el primer cuadro; la sonda de la señal vuelve a ella
    // en ese mismo cuadro (y anota cada uno para el vigilante de tic).
    if (GstElement* sel = gst_bin_get_by_name(GST_BIN(pl), "sel")) {
        GstPad* senalPad = gst_element_get_static_pad(sel, "sink_0");
        GstPad* placaPad = gst_element_get_static_pad(sel, "sink_1");
        if (placaPad != nullptr) g_object_set(sel, "active-pad", placaPad, nullptr);
        v.placa = true;
        if (senalPad != nullptr) {
            gst_pad_add_probe(
                senalPad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad* pad, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                    auto* vv = static_cast<Video*>(d);
                    vv->ultimoUs.store(ahoraUs(), std::memory_order_relaxed);
                    if (vv->placa.exchange(false))
                        if (GstElement* s = gst_pad_get_parent_element(pad)) {
                            g_object_set(s, "active-pad", pad, nullptr); // volvió la app
                            gst_object_unref(s);
                        }
                    return GST_PAD_PROBE_OK;
                },
                &v, nullptr);
            gst_object_unref(senalPad);
        }
        if (placaPad != nullptr) gst_object_unref(placaPad);
        // La edad de lo que sale hacia el codificador (lo que agrega el emisor: T1).
        if (GstPad* sp = gst_element_get_static_pad(sel, "src")) {
            v.edad.poner(sp, reloj_);
            gst_object_unref(sp);
        }
        gst_object_unref(sel);
    }
    motor::sinRelojPropio(pl);
    reloj_.adoptar(pl);
    if (gst_element_set_state(pl, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        v.edad.sacar();
        gst_element_set_state(pl, GST_STATE_NULL);
        gst_object_unref(pl);
        v.rama.error = traducir("emisor", "el video de entrada no arrancó");
        v.rama.reintentoNs = ahoraNs() + GST_SECOND;
        return;
    }
    v.rama.pipeline = pl;
    v.rama.error.clear();
    v.ancho = p.ancho;
    v.alto = p.alto;
    v.fpsN = p.fpsN;
    v.fpsD = p.fpsD;
    v.texto = texto;
}

void Emisor::armarAudio(Audio& a) {
    const std::string caps = "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2";
    // El silencio, siempre en la mezcla (en vivo, a la hora del estudio): con la app
    // caída, el audiomixer sale igual a su hora y el AAC no ve huecos.
    const std::string d = "audiomixer name=mezcla force-live=true start-time-selection=first min-upstream-latency=" +
                          std::to_string(kEsperaMezclaNs) + " ! " + caps + " ! intersink name=sale producer-name=" +
                          entreComillas(a.productor) + " sync=false event-types=\"<>\" " + "baanillo name=anillo canal=" +
                          entreComillas(a.canal) + " ! audioconvert ! audioresample ! " + caps + " ! mezcla. " +
                          "audiotestsrc wave=silence is-live=true samplesperbuffer=480 ! " + caps + " ! mezcla.";
    GstElement* pl = nullptr;
    if (std::string e = lanzar(d, &pl); !e.empty()) {
        a.rama.error = e;
        a.rama.reintentoNs = ahoraNs() + GST_SECOND;
        registrar(Nivel::Aviso, llenar(traducir("emisor", "el audio de entrada no se pudo armar: %1"), {e}));
        return;
    }
    if (GstElement* an = gst_bin_get_by_name(GST_BIN(pl), "anillo")) {
        if (GstPad* sp = gst_element_get_static_pad(an, "src")) {
            gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                              [](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                                  static_cast<Audio*>(d)->ultimoUs.store(ahoraUs(), std::memory_order_relaxed);
                                  return GST_PAD_PROBE_OK;
                              },
                              &a, nullptr);
            gst_object_unref(sp);
        }
        gst_object_unref(an);
    }
    motor::sinRelojPropio(pl);
    reloj_.adoptar(pl);
    motor::fuentesDePruebaEnHora(pl, reloj_.ahora()); // el silencio, a la hora del estudio
    if (gst_element_set_state(pl, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pl, GST_STATE_NULL);
        gst_object_unref(pl);
        a.rama.error = traducir("emisor", "el audio de entrada no arrancó");
        a.rama.reintentoNs = ahoraNs() + GST_SECOND;
        return;
    }
    a.rama.pipeline = pl;
    a.rama.error.clear();
}

void Emisor::desarmar(Rama& r, motor::MedidorEdad* edad) {
    if (r.pipeline == nullptr) return;
    if (edad != nullptr) edad->sacar();
    gst_element_set_state(r.pipeline, GST_STATE_NULL);
    gst_object_unref(r.pipeline);
    r.pipeline = nullptr;
}

void Emisor::atender(Rama& r, const std::string& que, const std::function<void()>& armar, motor::MedidorEdad* edad) {
    if (r.pipeline == nullptr) {
        if (ahoraNs() >= r.reintentoNs) armar();
        return;
    }
    GstBus* bus = gst_element_get_bus(r.pipeline);
    bool falla = false;
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            r.error = e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("emisor", "error");
            if (e != nullptr) g_error_free(e);
            falla = true;
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    if (!falla) return;
    // El intersink sigue publicando con el mismo nombre al rearmar: los codificadores
    // que lo leen no se enteran (más que del hueco).
    registrar(Nivel::Aviso, llenar(traducir("emisor", "%1 de entrada: %2 (se rearma)"), {que, r.error}));
    desarmar(r, edad);
    r.reintentoNs = ahoraNs() + GST_SECOND;
}

void Emisor::vigilarPlaca(Video& v) {
    if (v.rama.pipeline == nullptr || v.placa.load()) return;
    const std::int64_t sin = ahoraUs() - v.ultimoUs.load();
    if (sin <= static_cast<std::int64_t>(ajustes::entero("emisor.sinSenalMs", 500)) * 1000 || v.placa.exchange(true)) return;
    if (GstElement* sel = gst_bin_get_by_name(GST_BIN(v.rama.pipeline), "sel")) {
        if (GstPad* pp = gst_element_get_static_pad(sel, "sink_1")) {
            g_object_set(sel, "active-pad", pp, nullptr);
            gst_object_unref(pp);
        }
        gst_object_unref(sel);
    }
    ++v.placas;
    registrar(Nivel::Aviso,
              llenar(traducir("emisor", "%1 sin cuadros hace %2 ms: sale la placa"), {v.canal, std::to_string(sin / 1000)}));
}

void Emisor::ajustarVideo(Video& v) {
    // Otro tamaño o cuadros (otro lienzo en la app) u otro texto de la placa: la entrada
    // se rearma (las salidas lo ven como caps nuevas y se rearman solas).
    const Pedido& p = pedido_;
    if (v.rama.pipeline != nullptr &&
        (p.ancho != v.ancho || p.alto != v.alto || p.fpsN != v.fpsN || p.fpsD != v.fpsD || textoPlaca() != v.texto))
        desarmar(v.rama, &v.edad);
    if (v.rama.pipeline == nullptr) armarVideo(v);
}

void Emisor::aplicarEntradas() {
    const Pedido& p = pedido_;
    ajustarVideo(video_);
    if (audio_.rama.pipeline == nullptr) armarAudio(audio_);
    // La señal limpia: su canal se lee solo mientras se la pide.
    limpioPedido_ = p.limpio && (p.limpio->transmision || p.limpio->grabacion);
    if (limpioPedido_)
        ajustarVideo(limpio_);
    else
        desarmar(limpio_.rama, &limpio_.edad);
    // Las pistas 2-6: un anillo por cada una que graba (antes de armar la grabación).
    const std::set<int> quiere = p.grabacion || p.copia ? std::set<int>(p.pistas.begin(), p.pistas.end()) : std::set<int>{};
    for (auto it = pistas_.begin(); it != pistas_.end();) {
        if (quiere.count(it->first) > 0) {
            ++it;
            continue;
        }
        desarmar(it->second->rama);
        it = pistas_.erase(it);
    }
    for (int n : quiere) {
        auto& a = pistas_[n];
        if (a) continue;
        a = std::make_unique<Audio>();
        a->canal = cfg_.canalAudio + ".pista" + std::to_string(n); // = canalAudioPistaEmisor
        a->productor = audio_.productor + ":pista" + std::to_string(n);
        armarAudio(*a);
    }
}

// --- Lo pedido ----------------------------------------------------------------------

void Emisor::aplicar(const Pedido& p) {
    pedido_ = p;
    hayPedido_ = true;
    aplicarEntradas();
    aplicarSalidas();
}

void Emisor::aplicarSalidas() {
    const Pedido& p = pedido_;
    errorSalidas_.clear();
    bool falta = false;
    // Cada salida: con la misma configuración, la que ya anda sigue (aunque esté
    // reconectando o rearmándose); con otra, se para y se arma la nueva; lo que no se
    // pide, se para. Lo que no arranca se vuelve a probar (tic).
    const auto conciliar = [&](auto& a, const json& quiere, const std::string& que, auto crear) {
        if (a.salida && a.cfg != quiere) {
            a.salida->parar();
            a.salida.reset();
            a.cfg = json();
        }
        if (a.salida || quiere.is_null()) return;
        std::string e;
        auto nueva = crear(e);
        if (!e.empty()) {
            errorSalidas_ = llenar(traducir("emisor", "%1 no arrancó: %2"), {que, e});
            falta = true;
            return;
        }
        a.salida = std::move(nueva);
        a.cfg = quiere;
    };
    const auto transmitir = [&](const motor::Transmision::Config& c, const motor::Fuentes& f) {
        return [&, c, f](std::string& e) {
            auto t = std::make_unique<motor::Transmision>(reloj_, c);
            e = t->iniciar(f);
            return t;
        };
    };
    // Una nueva no pisa nada de lo que haya en la carpeta (nombreLibre).
    const auto grabar = [&](const motor::Grabacion::Config& c, const motor::Fuentes& f) {
        return [&, c, f](std::string& e) {
            motor::Grabacion::Config cc = c;
            cc.nombre = nombreLibre(cc.carpeta, cc.nombre);
            auto g = std::make_unique<motor::Grabacion>(reloj_, cc);
            e = g->iniciar(f);
            return g;
        };
    };

    // La transmisión principal y los destinos extra.
    conciliar(tx_, p.transmision ? aJson(*p.transmision) : json(), traducir("emisor", "la transmisión"),
              transmitir(p.transmision.value_or(motor::Transmision::Config{}), fuentes()));
    for (auto it = extras_.begin(); it != extras_.end();) {
        if (p.extras.count(it->first) > 0) {
            ++it;
            continue;
        }
        if (it->second.salida) it->second.salida->parar();
        it = extras_.erase(it);
    }
    for (const auto& [id, c] : p.extras)
        conciliar(extras_[id], aJson(c), llenar(traducir("emisor", "el destino «%1»"), {id}), transmitir(c, fuentes()));
    // La grabación y su copia (con las pistas 2-6 pedidas): con la misma configuración,
    // sigue en el mismo archivo.
    conciliar(grab_, cfgGrabacion(p.grabacion, p.pistas), traducir("emisor", "la grabación"),
              grabar(p.grabacion.value_or(motor::Grabacion::Config{}), fuentesGrabacion()));
    conciliar(copia_, cfgGrabacion(p.copia, p.pistas), traducir("emisor", "la copia"),
              grabar(p.copia.value_or(motor::Grabacion::Config{}), fuentesGrabacion()));
    // La señal limpia: su video, el audio del programa.
    const bool limpioTx = p.limpio && p.limpio->transmision, limpioGrab = p.limpio && p.limpio->grabacion;
    conciliar(limpioTx_, limpioTx ? aJson(*p.limpio->transmision) : json(), traducir("emisor", "la señal limpia"),
              transmitir(limpioTx ? *p.limpio->transmision : motor::Transmision::Config{}, fuentesLimpio()));
    conciliar(limpioGrab_, limpioGrab ? aJson(*p.limpio->grabacion) : json(), traducir("emisor", "la señal limpia"),
              grabar(limpioGrab ? *p.limpio->grabacion : motor::Grabacion::Config{}, fuentesLimpio()));
    // La repetición (lo de guardar no cuenta para rearmarla).
    const json repQuiere = p.repeticion ? json{{"segundos", p.repeticion->segundos},
                                               {"carpeta", p.repeticion->carpeta},
                                               {"kbps", p.repeticion->kbps}}
                                        : json();
    if (rep_.rep && rep_.cfg != repQuiere) pararRepeticion();
    if (!rep_.rep && p.repeticion) {
        std::error_code ec;
        std::filesystem::create_directories(p.repeticion->carpeta, ec);
        auto r = std::make_shared<motor::Repeticion>(reloj_, (std::filesystem::path(p.repeticion->carpeta) / ".tramos").string(),
                                                     p.repeticion->segundos, p.repeticion->kbps);
        if (std::string e = r->iniciar(fuentes()); !e.empty()) {
            errorSalidas_ = llenar(traducir("emisor", "%1 no arrancó: %2"), {traducir("emisor", "la repetición"), e});
            falta = true;
        } else {
            rep_.rep = std::move(r);
            rep_.cfg = repQuiere;
            // Lo que se pidió guardar antes de esta repetición (otra corrida) ya pasó.
            rep_.guardadoSeq = p.repeticion->guardarSeq;
        }
    }
    // Guardar: el pedido sube `guardarSeq` (uno a la vez; el que llega mientras se guarda
    // espera a la vuelta siguiente).
    if (rep_.rep && p.repeticion && p.repeticion->guardarSeq > rep_.guardadoSeq && !rep_.ocupada->load()) {
        if (rep_.guardando.joinable()) rep_.guardando.join();
        rep_.guardadoSeq = p.repeticion->guardarSeq;
        rep_.ocupada->store(true);
        rep_.guardando = std::thread([rep = rep_.rep, destino = p.repeticion->guardarEn, ocupada = rep_.ocupada, mu = rep_.mu,
                                      ultimo = rep_.ultimo, error = rep_.error]() {
            const std::string e = rep->guardar(destino);
            {
                std::lock_guard<std::mutex> l(*mu);
                if (e.empty()) *ultimo = destino;
                *error = e;
            }
            if (!e.empty()) registrar(Nivel::Aviso, llenar(traducir("emisor", "la repetición no se guardó: %1"), {e}));
            ocupada->store(false);
        });
    } else if (rep_.rep && p.repeticion && p.repeticion->guardarSeq > rep_.guardadoSeq) {
        falta = true; // está guardando: el pedido se atiende a la vuelta
    }
    if (!errorSalidas_.empty()) registrar(Nivel::Aviso, errorSalidas_);
    reintentoSalidasNs_ = falta ? ahoraNs() + 2 * GST_SECOND : 0;
}

void Emisor::pararRepeticion() {
    if (rep_.guardando.joinable()) rep_.guardando.join(); // un clip a medio juntar se termina
    rep_.rep.reset();
    rep_.cfg = json();
}

void Emisor::tic() {
    if (hayPedido_) {
        atender(video_.rama, traducir("emisor", "el video"), [this]() { armarVideo(video_); }, &video_.edad);
        if (limpioPedido_)
            atender(limpio_.rama, traducir("emisor", "la señal limpia"), [this]() { armarVideo(limpio_); }, &limpio_.edad);
        atender(audio_.rama, traducir("emisor", "el audio"), [this]() { armarAudio(audio_); });
        for (auto& [n, a] : pistas_)
            atender(a->rama, llenar(traducir("emisor", "la pista %1"), {std::to_string(n)}), [this, ap = a.get()]() { armarAudio(*ap); });
    }
    // El vigilante de la placa: la app no publica hace `emisor.sinSenalMs`.
    vigilarPlaca(video_);
    if (limpioPedido_) vigilarPlaca(limpio_);
    if (tx_.salida) tx_.salida->tic();
    for (auto& [id, x] : extras_)
        if (x.salida) x.salida->tic();
    if (grab_.salida) grab_.salida->tic();
    if (copia_.salida) copia_.salida->tic();
    if (limpioTx_.salida) limpioTx_.salida->tic();
    if (limpioGrab_.salida) limpioGrab_.salida->tic();
    if (rep_.rep) rep_.rep->tic();
    if (reintentoSalidasNs_ != 0 && ahoraNs() >= reintentoSalidasNs_) aplicarSalidas();
}

std::vector<std::string> Emisor::parar() {
    std::vector<std::string> archivos;
    const auto pararTx = [](Armada<motor::Transmision>& a) {
        if (a.salida) a.salida->parar();
        a.salida.reset();
        a.cfg = json();
    };
    const auto pararGrab = [](Armada<motor::Grabacion>& a, std::vector<std::string>* archivos) {
        if (!a.salida) return;
        a.salida->parar();
        if (archivos != nullptr) *archivos = a.salida->archivos();
        a.salida.reset();
        a.cfg = json();
    };
    pararTx(tx_);
    for (auto& [id, x] : extras_) pararTx(x);
    extras_.clear();
    pararTx(limpioTx_);
    pararGrab(grab_, &archivos);
    pararGrab(copia_, nullptr);
    pararGrab(limpioGrab_, nullptr);
    pararRepeticion();
    reintentoSalidasNs_ = 0;
    return archivos;
}

json Emisor::estado() const {
    const std::int64_t ahora = ahoraUs();
    const auto hace = [ahora](std::int64_t us) { return us == 0 ? json() : json((ahora - us) / 1000); };
    const auto de = [](const auto& a) { return a.salida ? a.salida->estado() : json(); };
    const std::string errorEntrada = !video_.rama.error.empty() ? video_.rama.error : audio_.rama.error;
    json j{{"id", cfg_.id},
           {"seq", hayPedido_ ? pedido_.seq : 0},
           {"placa", video_.placa.load()},
           {"placas", video_.placas},
           {"video_hace_ms", hace(video_.ultimoUs.load())},
           {"audio_hace_ms", hace(audio_.ultimoUs.load())},
           {"edad_entrada", video_.edad.json()},
           {"error", !errorSalidas_.empty() ? errorSalidas_ : errorEntrada},
           {"transmision", de(tx_)},
           {"grabacion", de(grab_)},
           {"copia", de(copia_)}};
    json ex = json::object();
    for (const auto& [id, x] : extras_)
        if (x.salida) ex[id] = x.salida->estado();
    j["extras"] = std::move(ex);
    json pistas = json::array();
    for (const auto& [n, a] : pistas_) pistas.push_back(n);
    j["pistas"] = std::move(pistas);
    if (rep_.rep) {
        std::lock_guard<std::mutex> l(*rep_.mu);
        j["replay"] = {{"activa", rep_.rep->andando()}, {"segundos", rep_.rep->segundos()}, {"ultimo", *rep_.ultimo},
                       {"guardando", rep_.ocupada->load()}, {"guardado_seq", rep_.guardadoSeq}, {"error", *rep_.error}};
    } else {
        j["replay"] = json();
    }
    j["limpio"] = limpioPedido_ ? json{{"placa", limpio_.placa.load()}, {"transmision", de(limpioTx_)}, {"grabacion", de(limpioGrab_)}}
                                : json();
    return j;
}

} // namespace ba::emisor
