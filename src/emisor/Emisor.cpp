// src/emisor/Emisor.cpp — ver el .hpp

#include "Emisor.hpp"

#include "Piezas.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"

#include <algorithm>

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

} // namespace

Emisor::Emisor(motor::Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {
    if (cfg_.canalVideo.empty()) cfg_.canalVideo = ba::ipc::canalVideoDeSalida(cfg_.id);
    if (cfg_.canalAudio.empty()) cfg_.canalAudio = ba::ipc::canalAudioEmisor(cfg_.id);
}

Emisor::~Emisor() {
    parar();
    edad_.sacar();
    desarmar(video_);
    desarmar(audio_);
}

motor::Fuentes Emisor::fuentes() const {
    motor::Fuentes f;
    f.video = "video:emisor:" + cfg_.id;
    f.latVideoNs = kLatEntradaNs;
    f.audios = {"bus:emisor:" + cfg_.id};
    f.latAudioNs = kLatEntradaNs;
    return f;
}

// --- La entrada ---------------------------------------------------------------------

void Emisor::armarVideo() {
    const Pedido& p = pedido_;
    const std::string tam = ",width=" + std::to_string(p.ancho) + ",height=" + std::to_string(p.alto) +
                            ",pixel-aspect-ratio=1/1,framerate=" + std::to_string(p.fpsN) + "/" + std::to_string(p.fpsD);
    const std::string caps = "video/x-raw,format=NV12" + tam;
    const std::string texto = p.placaTexto.empty() ? traducir("emisor", "Volvemos enseguida") : p.placaTexto;
    // Lo que publica la app, al tamaño y los cuadros del pedido. videorate sin rellenar
    // huecos: cuando la app vuelve, el primer cuadro sale solo (no cientos de copias del
    // último de antes de la caída, ver referencias-mezcla-gstreamer).
    const std::string senal = "bavideoanillo name=anillo canal=" + entreComillas(cfg_.canalVideo) +
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
                          entreComillas(fuentes().video) + " sync=false event-types=\"<>\" " + senal + " " + placa;
    GstElement* pl = nullptr;
    if (std::string e = lanzar(d, &pl); !e.empty()) {
        video_.error = e;
        video_.reintentoNs = ahoraNs() + GST_SECOND;
        registrar(Nivel::Aviso, llenar(traducir("emisor", "el video de entrada no se pudo armar: %1"), {e}));
        return;
    }
    // Arranca en la placa hasta el primer cuadro; la sonda de la señal vuelve a ella
    // en ese mismo cuadro (y anota cada uno para el vigilante de tic).
    if (GstElement* sel = gst_bin_get_by_name(GST_BIN(pl), "sel")) {
        GstPad* senalPad = gst_element_get_static_pad(sel, "sink_0");
        GstPad* placaPad = gst_element_get_static_pad(sel, "sink_1");
        if (placaPad != nullptr) g_object_set(sel, "active-pad", placaPad, nullptr);
        placaActiva_ = true;
        if (senalPad != nullptr) {
            gst_pad_add_probe(
                senalPad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad* pad, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                    auto* em = static_cast<Emisor*>(d);
                    em->ultimoVideoUs_.store(ahoraUs(), std::memory_order_relaxed);
                    if (em->placaActiva_.exchange(false))
                        if (GstElement* s = gst_pad_get_parent_element(pad)) {
                            g_object_set(s, "active-pad", pad, nullptr); // volvió la app
                            gst_object_unref(s);
                        }
                    return GST_PAD_PROBE_OK;
                },
                this, nullptr);
            gst_object_unref(senalPad);
        }
        if (placaPad != nullptr) gst_object_unref(placaPad);
        // La edad de lo que sale hacia el codificador (lo que agrega el emisor: T1).
        if (GstPad* sp = gst_element_get_static_pad(sel, "src")) {
            edad_.poner(sp, reloj_);
            gst_object_unref(sp);
        }
        gst_object_unref(sel);
    }
    motor::sinRelojPropio(pl);
    reloj_.adoptar(pl);
    if (gst_element_set_state(pl, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        edad_.sacar();
        gst_element_set_state(pl, GST_STATE_NULL);
        gst_object_unref(pl);
        video_.error = traducir("emisor", "el video de entrada no arrancó");
        video_.reintentoNs = ahoraNs() + GST_SECOND;
        return;
    }
    video_.pipeline = pl;
    video_.error.clear();
    anchoArmado_ = p.ancho;
    altoArmado_ = p.alto;
    fpsNArmado_ = p.fpsN;
    fpsDArmado_ = p.fpsD;
    placaArmada_ = texto;
}

void Emisor::armarAudio() {
    const std::string caps = "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2";
    // El silencio, siempre en la mezcla (en vivo, a la hora del estudio): con la app
    // caída, el audiomixer sale igual a su hora y el AAC no ve huecos.
    const std::string d = "audiomixer name=mezcla force-live=true start-time-selection=first min-upstream-latency=" +
                          std::to_string(kEsperaMezclaNs) + " ! " + caps + " ! intersink name=sale producer-name=" +
                          entreComillas(fuentes().audios.front()) + " sync=false event-types=\"<>\" " +
                          "baanillo name=anillo canal=" + entreComillas(cfg_.canalAudio) +
                          " ! audioconvert ! audioresample ! " + caps + " ! mezcla. " +
                          "audiotestsrc wave=silence is-live=true samplesperbuffer=480 ! " + caps + " ! mezcla.";
    GstElement* pl = nullptr;
    if (std::string e = lanzar(d, &pl); !e.empty()) {
        audio_.error = e;
        audio_.reintentoNs = ahoraNs() + GST_SECOND;
        registrar(Nivel::Aviso, llenar(traducir("emisor", "el audio de entrada no se pudo armar: %1"), {e}));
        return;
    }
    if (GstElement* a = gst_bin_get_by_name(GST_BIN(pl), "anillo")) {
        if (GstPad* sp = gst_element_get_static_pad(a, "src")) {
            gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                              [](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                                  static_cast<Emisor*>(d)->ultimoAudioUs_.store(ahoraUs(), std::memory_order_relaxed);
                                  return GST_PAD_PROBE_OK;
                              },
                              this, nullptr);
            gst_object_unref(sp);
        }
        gst_object_unref(a);
    }
    motor::sinRelojPropio(pl);
    reloj_.adoptar(pl);
    motor::fuentesDePruebaEnHora(pl, reloj_.ahora()); // el silencio, a la hora del estudio
    if (gst_element_set_state(pl, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pl, GST_STATE_NULL);
        gst_object_unref(pl);
        audio_.error = traducir("emisor", "el audio de entrada no arrancó");
        audio_.reintentoNs = ahoraNs() + GST_SECOND;
        return;
    }
    audio_.pipeline = pl;
    audio_.error.clear();
}

void Emisor::desarmar(Rama& r) {
    if (r.pipeline == nullptr) return;
    if (&r == &video_) edad_.sacar();
    gst_element_set_state(r.pipeline, GST_STATE_NULL);
    gst_object_unref(r.pipeline);
    r.pipeline = nullptr;
}

void Emisor::atender(Rama& r, bool video) {
    if (r.pipeline == nullptr) {
        if (!hayPedido_ || ahoraNs() < r.reintentoNs) return;
        if (video)
            armarVideo();
        else
            armarAudio();
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
    registrar(Nivel::Aviso, llenar(traducir("emisor", "%1 de entrada: %2 (se rearma)"),
                                   {video ? traducir("emisor", "el video") : traducir("emisor", "el audio"), r.error}));
    desarmar(r);
    r.reintentoNs = ahoraNs() + GST_SECOND;
}

// --- Lo pedido ----------------------------------------------------------------------

void Emisor::aplicar(const Pedido& p) {
    const bool otroCuadro = !hayPedido_ || p.ancho != anchoArmado_ || p.alto != altoArmado_ || p.fpsN != fpsNArmado_ ||
                            p.fpsD != fpsDArmado_ ||
                            (p.placaTexto.empty() ? traducir("emisor", "Volvemos enseguida") : p.placaTexto) != placaArmada_;
    pedido_ = p;
    hayPedido_ = true;
    // Otro tamaño o cuadros (otro lienzo en la app) u otro texto de la placa: la entrada
    // se rearma (las salidas lo ven como caps nuevas y se rearman solas).
    if (otroCuadro && video_.pipeline != nullptr) desarmar(video_);
    if (video_.pipeline == nullptr) armarVideo();
    if (audio_.pipeline == nullptr) armarAudio();
    aplicarSalidas();
}

void Emisor::aplicarSalidas() {
    const Pedido& p = pedido_;
    errorSalidas_.clear();
    bool falta = false;
    // La transmisión: con la misma configuración, la que ya anda sigue (aunque esté reconectando).
    const json txQuiere = p.transmision ? aJson(*p.transmision) : json();
    if (tx_ && txCfg_ != txQuiere) {
        tx_->parar();
        tx_.reset();
        txCfg_ = json();
    }
    if (!tx_ && !txQuiere.is_null()) {
        auto tx = std::make_unique<motor::Transmision>(reloj_, *p.transmision);
        if (std::string e = tx->iniciar(fuentes()); !e.empty()) {
            errorSalidas_ = llenar(traducir("emisor", "la transmisión no arrancó: %1"), {e});
            falta = true;
        } else {
            tx_ = std::move(tx);
            txCfg_ = txQuiere;
        }
    }
    // La grabación y su copia: con la misma configuración, sigue en el mismo archivo.
    // Una nueva no pisa nada de lo que haya en la carpeta (nombreLibre).
    const auto grabar = [&](const std::optional<motor::Grabacion::Config>& quiere, std::unique_ptr<motor::Grabacion>& g,
                            json& cfg, const std::string& que) {
        const json q = quiere ? aJson(*quiere) : json();
        if (g && cfg != q) {
            g->parar();
            g.reset();
            cfg = json();
        }
        if (g || q.is_null()) return;
        motor::Grabacion::Config c = *quiere;
        c.nombre = nombreLibre(c.carpeta, c.nombre);
        auto nueva = std::make_unique<motor::Grabacion>(reloj_, c);
        if (std::string e = nueva->iniciar(fuentes()); !e.empty()) {
            errorSalidas_ = llenar(traducir("emisor", "%1 no arrancó: %2"), {que, e});
            falta = true;
            return;
        }
        g = std::move(nueva);
        cfg = q;
    };
    grabar(p.grabacion, grab_, grabCfg_, traducir("emisor", "la grabación"));
    grabar(p.copia, copia_, copiaCfg_, traducir("emisor", "la copia"));
    if (!errorSalidas_.empty()) registrar(Nivel::Aviso, errorSalidas_);
    // Lo que no arrancó se vuelve a probar (tic).
    reintentoSalidasNs_ = falta ? ahoraNs() + 2 * GST_SECOND : 0;
}

void Emisor::tic() {
    atender(video_, true);
    atender(audio_, false);
    // El vigilante de la placa: la app no publica hace `emisor.sinSenalMs`.
    if (video_.pipeline != nullptr && !placaActiva_.load()) {
        const std::int64_t sin = ahoraUs() - ultimoVideoUs_.load();
        if (sin > static_cast<std::int64_t>(ajustes::entero("emisor.sinSenalMs", 500)) * 1000 && !placaActiva_.exchange(true)) {
            if (GstElement* sel = gst_bin_get_by_name(GST_BIN(video_.pipeline), "sel")) {
                if (GstPad* pp = gst_element_get_static_pad(sel, "sink_1")) {
                    g_object_set(sel, "active-pad", pp, nullptr);
                    gst_object_unref(pp);
                }
                gst_object_unref(sel);
            }
            ++placas_;
            registrar(Nivel::Aviso, llenar(traducir("emisor", "%1 sin cuadros hace %2 ms: sale la placa"),
                                           {cfg_.canalVideo, std::to_string(sin / 1000)}));
        }
    }
    if (tx_) tx_->tic();
    if (grab_) grab_->tic();
    if (copia_) copia_->tic();
    if (reintentoSalidasNs_ != 0 && ahoraNs() >= reintentoSalidasNs_) aplicarSalidas();
}

void Emisor::parar() {
    if (tx_) tx_->parar();
    tx_.reset();
    txCfg_ = json();
    for (auto* g : {&grab_, &copia_})
        if (*g) {
            (*g)->parar();
            g->reset();
        }
    grabCfg_ = copiaCfg_ = json();
    reintentoSalidasNs_ = 0;
}

json Emisor::estado() const {
    const std::int64_t ahora = ahoraUs();
    const auto hace = [ahora](std::int64_t us) { return us == 0 ? json() : json((ahora - us) / 1000); };
    json j{{"id", cfg_.id},
           {"seq", hayPedido_ ? pedido_.seq : 0},
           {"placa", placaActiva_.load()},
           {"placas", placas_},
           {"video_hace_ms", hace(ultimoVideoUs_.load())},
           {"audio_hace_ms", hace(ultimoAudioUs_.load())},
           {"edad_entrada", edad_.json()},
           {"error", !errorSalidas_.empty() ? errorSalidas_ : !video_.error.empty() ? video_.error : audio_.error},
           {"transmision", tx_ ? tx_->estado() : json()},
           {"grabacion", grab_ ? grab_->estado() : json()},
           {"copia", copia_ ? copia_->estado() : json()}};
    return j;
}

} // namespace ba::emisor
