// ============================================================================
// src/app/ui/TarjetasRostros.cpp — ver TarjetasRostros.hpp
// ============================================================================
#include "ui/TarjetasRostros.hpp"

#if BA_HAVE_ROSTROS

#include <algorithm>
#include <cstring>

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QTimer>

#include <gst/video/video.h>
#include <nlohmann/json.hpp>

#include "Entrada.hpp"
#include "arranque/RuntimeMotor.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/VentanillaMotor.hpp"
#include "rostros/QuienEs.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/VideoViewModel.hpp"
#include "video/VideoGrafos.hpp"

namespace ba::app {

namespace {

/// El cuadro de un feed en BGRA contiguo (lo que pide el reconocimiento) y su QImage.
bool cuadro(StudioViewModel& vm, const QString& feed, QImage& img, std::vector<std::uint8_t>& bgra) {
    // (Las cámaras del estudio no pasan por acá: las mira su tarjeta «Rostros».)
    auto* frames = static_cast<FrameService*>(vm.frames());
    std::uint64_t serial = 0;
    if (!frames->latest(feed, img, serial) || img.isNull()) return false;
    img = img.convertToFormat(QImage::Format_ARGB32);
    bgra.resize(static_cast<std::size_t>(img.width()) * img.height() * 4);
    for (int y = 0; y < img.height(); ++y)
        std::memcpy(bgra.data() + static_cast<std::size_t>(y) * img.width() * 4, img.constScanLine(y),
                    static_cast<std::size_t>(img.width()) * 4);
    return true;
}

/// Un GstSample BGRA (el cuadro que analizó la tarjeta) como QImage.
QImage imagenDe(GstSample* s) {
    if (s == nullptr) return {};
    GstVideoInfo vi;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstVideoFrame f;
    if (b == nullptr || !gst_video_info_from_caps(&vi, gst_sample_get_caps(s)) || !gst_video_frame_map(&f, &vi, b, GST_MAP_READ))
        return {};
    const int w = GST_VIDEO_FRAME_WIDTH(&f), h = GST_VIDEO_FRAME_HEIGHT(&f);
    QImage img(w, h, QImage::Format_ARGB32); // BGRA en memoria
    for (int y = 0; y < h; ++y)
        std::memcpy(img.scanLine(y),
                    static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&f, 0)) +
                        static_cast<std::size_t>(y) * GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0),
                    static_cast<std::size_t>(w) * 4);
    gst_video_frame_unmap(&f);
    return img;
}

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

TarjetasRostros::TarjetasRostros(AppRuntime& rt, StudioViewModel& vm, Reconocedor& rec, rostros::QuienEs& quien,
                                 QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm), rec_(rec), quien_(quien) {
    // Lo que ven las tarjetas «Rostros» llega por el bus de cada entrada (en el bucle
    // del motor): se pasa al hilo de la interfaz, que es quien toca la libreta.
    QPointer<TarjetasRostros> yo(this);
    escucha_ = ba::motor::escucharElementos(
        [yo](const std::string& entrada, const std::string& nombre, const std::string& json, GstSample* cuadro) {
            if (nombre != "ba-rostros") return;
            if (cuadro != nullptr) gst_sample_ref(cuadro);
            QMetaObject::invokeMethod(
                qApp,
                [yo, e = QString::fromStdString(entrada), js = json, cuadro]() {
                    if (yo) yo->alRostros(e, js, cuadro);
                    if (cuadro != nullptr) gst_sample_unref(cuadro);
                },
                Qt::QueuedConnection);
        });
}

TarjetasRostros::~TarjetasRostros() { ba::motor::dejarDeEscuchar(escucha_); }

void TarjetasRostros::alRostros(const QString& entrada, const std::string& js, GstSample* cuadro) {
    const QString feed = "entrada:" + entrada;
    const nlohmann::json j = nlohmann::json::parse(js, nullptr, false);
    // A la tarjeta, QUIÉN ES cada cara (rostros/QuienEs): con cada resultado que trae
    // vectores, la espere alguien o no; solo si cambió. El dibujo es de la entrada (motor):
    // se le pide al dueño.
    if (j.is_object())
        if (const auto texto = quien_.etiquetas(feed.toStdString(), j.value("caras", nlohmann::json::array())))
            vm_.ventanilla().pedir("rostros_etiquetas", [&rt = rt_, e = entrada.toStdString(), t = *texto]() {
                rt.engine().controlEntrada(e, {{"rostros_etiquetas", t}});
            });
    auto it = esperas_.find(feed);
    if (it == esperas_.end() || it->second.empty()) return; // nadie la está esperando
    QImage img = imagenDe(cuadro);
    std::vector<Reconocedor::Visto> vistos;
    QString error;
    if (j.is_object()) {
        const nlohmann::json a = j.value("analisis", nlohmann::json::object());
        const double W = a.value("ancho", img.width()), H = a.value("alto", img.height());
        for (const auto& c : j.value("caras", nlohmann::json::array())) {
            Reconocedor::Visto v;
            v.cara.x = static_cast<int>(c.value("x", 0.0) * W);
            v.cara.y = static_cast<int>(c.value("y", 0.0) * H);
            v.cara.ancho = static_cast<int>(c.value("ancho", 0.0) * W);
            v.cara.alto = static_cast<int>(c.value("alto", 0.0) * H);
            v.cara.confianza = c.value("confianza", 0.0F);
            if (c.contains("puntos") && c["puntos"].is_array() && c["puntos"].size() == 10)
                for (int k = 0; k < 5; ++k) {
                    v.cara.puntos[k * 2] = static_cast<float>(c["puntos"][k * 2].get<double>() * W);
                    v.cara.puntos[k * 2 + 1] = static_cast<float>(c["puntos"][k * 2 + 1].get<double>() * H);
                }
            if (c.contains("vector") && c["vector"].is_array()) v.vector = c["vector"].get<std::vector<float>>();
            vistos.push_back(std::move(v));
        }
        error = QString::fromStdString(j.value("error", std::string()));
    } else {
        error = QCoreApplication::translate("PersonasViewModel", "La tarjeta Rostros mandó algo que no se entiende.");
    }
    std::vector<Espera> esperan = std::move(it->second);
    esperas_.erase(it);
    for (auto& e : esperan) e.luego(vistos, img, vistos.empty() ? error : QString());
}

void TarjetasRostros::pedirAnalisis(const QString& feed, bool descartable, Analisis luego) {
    if (feed.startsWith("entrada:")) {
        // UNA CÁMARA DEL ESTUDIO: la mira su tarjeta «Rostros» (el elemento barostros, en
        // la cadena de la entrada) y contesta por el bus con las caras, su vector y el
        // cuadro: se espera su próximo resultado. Si todavía no la tiene, se la pone (como
        // si la página la mirara).
        feedMirado_ = feed;
        miradoMs_ = ahoraMs();
        sincronizar();
        auto& lista = esperas_[feed];
        if (descartable && !lista.empty()) return; // la ronda vuelve sola
        const int id = proximaEspera_++;
        lista.push_back({id, std::move(luego)});
        QTimer::singleShot(3000, this, [this, feed, id]() {
            auto it = esperas_.find(feed);
            if (it == esperas_.end()) return;
            auto& l = it->second;
            const auto e = std::find_if(l.begin(), l.end(), [id](const Espera& x) { return x.id == id; });
            if (e == l.end()) return;
            Analisis f = std::move(e->luego);
            l.erase(e);
            f({}, QImage(), QCoreApplication::translate("PersonasViewModel", "La tarjeta Rostros de «%1» no contestó (¿hay imagen?).").arg(etiquetaDe(feed)));
        });
        return;
    }
    // El cuadro se toma ACA: FrameService es de la interfaz y no se comparte. Lo que se
    // manda al otro hilo es una copia de los píxeles, que es barata al lado de lo que
    // cuesta mirarlos.
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (!cuadro(vm_, feed, *img, bgra)) return luego({}, QImage(), QCoreApplication::translate("PersonasViewModel", "No hay imagen en «%1».").arg(etiquetaDe(feed)));
    const int w = img->width();
    const int h = img->height();
    rec_.pedir(std::move(bgra), w, h, descartable,
               [luego = std::move(luego), img](std::vector<Reconocedor::Visto> vistos, const QString& error) {
                   luego(std::move(vistos), *img, error);
               });
}

void TarjetasRostros::mirando(const QString& feed) {
    // Una cámara del estudio: que tenga su tarjeta mientras se la mira.
    if (feed.startsWith("entrada:")) {
        const bool otra = feed != feedMirado_;
        feedMirado_ = feed;
        miradoMs_ = ahoraMs();
        if (otra) sincronizar();
    } else if (feedMirado_.startsWith("entrada:")) {
        feedMirado_.clear();
        sincronizar();
    }
}

void TarjetasRostros::dejarDeMirar() {
    if (!feedMirado_.startsWith("entrada:")) return;
    feedMirado_.clear();
    sincronizar();
}

void TarjetasRostros::usarCamara(const QString& entrada, bool usar) {
    const bool antes = seguidas_.count(entrada) > 0;
    if (usar == antes) return;
    if (usar) seguidas_.insert(entrada);
    else seguidas_.erase(entrada);
    sincronizar();
}

void TarjetasRostros::sincronizar() {
    // Las cámaras del estudio que Personas usa llevan su tarjeta «Rostros» en el grafo de
    // video: se pone al elegirlas y se saca al dejar de usarlas.
    auto* vg = vm_.videoVista();
    if (vg == nullptr) return;
    std::map<std::string, std::string> quiero;
    if (feedMirado_.startsWith("entrada:") && ahoraMs() - miradoMs_ < 15000) quiero[feedMirado_.mid(8).toStdString()] = "mirar";
    if (usos_)
        for (const auto& [e, para] : usos_()) quiero[e] = para;
    for (const QString& e : seguidas_) quiero[e.toStdString()] = "seguir";
    // Las pone el grafo, en el dueño del motor; si no se puede, avisa.
    vg->pedir("personas_rostros", [quiero](video::VideoGrafos& g) { return g.configurarRostros(quiero); },
              [yo = QPointer<TarjetasRostros>(this)](const std::string& e) {
                  if (yo && !e.empty()) emit yo->error(QString::fromStdString(e));
              });
}

QString TarjetasRostros::etiquetaDe(const QString& feed) const {
    if (feed.startsWith("entrada:")) return QCoreApplication::translate("PersonasViewModel", "Cámara · %1").arg(feed.mid(8));
    return static_cast<FrameService*>(vm_.frames())->label(feed);
}

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
