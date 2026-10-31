// src/app/ui/ImagenViewModel.cpp — ver el .hpp

#include "ui/ImagenViewModel.hpp"

#include "Motor.hpp"
#include "arranque/AppRuntime.hpp"
#include "control/BusEventos.hpp"
#include "platform/Paths.hpp"
#include "ptz/ControlCamaras.hpp"
#include "red/Red.hpp"
#include "resolucion/ResolucionModel.hpp"
#include "tiempos/TiemposModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/Seguidor.hpp"
#include "video/BancoSuperRes.hpp"
#include "video/Camaras.hpp"
#include "video/Scopes.hpp"
#include "video/SuperRes.hpp"
#include "video/VideoGrafos.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif

#include <QBuffer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlEngine>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

ImagenViewModel::ImagenViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    // El PTZ de cada entrada (y Mejorar imagen): se entera enseguida de las
    // entradas nuevas y de lo que cambia en Configuración.
    camaras_ = std::make_unique<ptz::ControlCamaras>(rt, rt.dataRoot());
    armarTiempos();
    armarResolucion();
    connect(&p_.entradas, &InputsViewModel::changed, camaras_.get(), &ptz::ControlCamaras::sincronizar);
    // El grafo de tarjetas de video de cada entrada (después del PTZ).
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
    video_->setLegalizar(rt.config().legalizar);
    // «Priorizar programa» por calor (F4-05): el primer paso es la superresolución, que
    // pasa a barato (sin turnos activos) y vuelve a lo que tenía al aliviarse (los demás
    // pasos los atienden los modelos, las vistas del motor y la sala).
    p_.recursos.alDegradar("superres", [this, antes = std::make_shared<nlohmann::json>()](bool apagar) {
        if (!video_) return false;
        if (apagar) {
            *antes = video_->programador().value("max_activas", nlohmann::json(3));
            video_->configurarProgramador({{"max_activas", 0}});
        } else {
            video_->configurarProgramador({{"max_activas", antes->is_number() ? *antes : nlohmann::json(3)}});
        }
        return true;
    });
    // La superresolución que no llega: aviso al operador y evento en el bus (T40).
    connect(video_.get(), &video::VideoGrafos::superresTarde, this, [this](const QString& e, double ms, double cuadroMs, bool bypass) {
        emit notify((bypass ? tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3): pasada a barato")
                            : tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3)"))
                        .arg(e)
                        .arg(ms, 0, 'f', 1)
                        .arg(cuadroMs, 0, 'f', 1),
                    "warn");
        p_.bus.publicar("superres/tarde", {{"entrada", e.toStdString()}, {"ms", ms}, {"cuadro_ms", cuadroMs}, {"bypass", bypass}});
    });
    // La superresolución de las entradas de red (motores, modelos, laboratorio).
    superres_ = std::make_unique<video::SuperRes>(rt.dataRoot(), video_.get());
    {
        video::BancoSuperRes::Entorno en;
        en.alAire = [&rt]() { return rt.engine().haySalida(); };
        en.fps = [&rt]() {
            const auto& v = rt.engine().videoConfig();
            return std::make_pair(v.fpsNum, v.fpsDen);
        };
        en.tamanosCamaras = [this]() { return video_->tamanosDeEntradas(); };
        bancoSr_ = std::make_unique<video::BancoSuperRes>(rt.dataRoot(), *superres_, en);
        video_->setEstimadorSr([this](const std::string& pr, int w, int h) { return bancoSr_->estimarMs(pr, w, h); });
    }
    connect(&p_.entradas, &InputsViewModel::changed, video_.get(), &video::VideoGrafos::sincronizar);
    // Los equipos de video de la red: la biblioteca de marcas y la base OUI
    // vienen con el programa (data/red).
    red_ = std::make_unique<red::Red>(ba::platform::bundleRoot() / "data" / "red",
                                      QString::fromStdWString((ba::platform::credentialsDir()).wstring()), video_.get(),
                                      camaras_.get());
    // Seguir una cara con el PTZ (la ventana del PTZ y el asistente).
    seguidor_ = std::make_unique<Seguidor>(p_.personas, camaras_.get());
    // Ir a una posición a pedido (enfocar, dar la palabra, un preset) corta el
    // seguimiento de caras de esa cámara: si no, se pelean.
    camaras_->setDejarDeSeguir([this](const QString& e) {
        if (seguidor_) seguidor_->dejar(e);
    });
    seguidor_->setGrafos(video_.get());
}

ImagenViewModel::~ImagenViewModel() {
    // El estimador apunta al banco, que se va antes que los grafos.
    if (video_) video_->setEstimadorSr(nullptr);
}

void ImagenViewModel::armarTiempos() {
    AppRuntime& rt = p_.rt;
    // Los tiempos: solo fotos que ya se publican (el motor, las salidas, los grafos, los racks).
    tiempos_ = std::make_unique<tiempos::TiemposModel>([this]() {
        tiempos::Fotos f;
        BroadcastEngine& e = p_.rt.engine();
        if (motor::Motor* m = e.motor())
            if (const auto foto = m->instantanea()) f.motor = *foto;
        f.salidas = e.salidasAhora();
        if (video_)
            for (const QVariant& v : video_->estudio().value("nodos").toList()) {
                const QVariantMap n = v.toMap();
                const QString t = n.value("etiqueta").toString();
                f.titulos[n.value("id").toString().toStdString()] = (t.isEmpty() ? n.value("titulo").toString() : t).toStdString();
            }
#if defined(BA_HAVE_CLAP)
        if (audio::RackManager* racks = p_.racks ? p_.racks() : nullptr)
            for (const std::string& s : racks->fuentesConRack())
                if (audio::AudioRack* r = racks->rack(s, false)) f.racks[s] = r->describe();
#endif
        // Los lugares de la sala sin nadie: su entrada recibe la placa del gateway, pero no es nadie.
        for (const std::string& n : e.sourceNames()) {
            const nlohmann::json a = nlohmann::json::parse(e.sourceSettingsJson(n), nullptr, false);
            if (!a.is_object() || !a.contains("slot")) continue;
            const std::string canal = a.value("channel", a.value("anillo", std::string("guest")));
            if (canal != "guest" && canal != "share") continue; // el remoto con la palabra: la sesión remota
            const int slot = a.value("slot", 0);
            if (slot > 0 && p_.sala.participanteEnSlot(slot).isEmpty())
                f.enEspera[n] = tr("el lugar %1 de la conferencia está vacío (llega la placa)").arg(slot).toStdString();
        }
        return f;
    }, rt.dataRoot() / "Config" / "tiempos-analisis.json");
    // El análisis por elemento (las sondas del motor): lo llama el modelo en su hilo.
    tiempos_->setSondas({[&rt](const std::string& e) -> std::string {
                             motor::Motor* m = rt.engine().motor();
                             return m != nullptr ? m->analizarEmpezar(e) : tr("el motor no está andando").toStdString();
                         },
                         [&rt](const std::string& e) -> nlohmann::json {
                             motor::Motor* m = rt.engine().motor();
                             return m != nullptr ? m->analizarTerminar(e)
                                                 : nlohmann::json{{"error", tr("el motor no está andando").toStdString()}};
                         }});
}

void ImagenViewModel::armarResolucion() {
    AppRuntime& rt = p_.rt;
    // El plan de resolución (solo lectura): lo que publican el motor, las escenas y los dispositivos.
    resolucion::ResolucionModel::Fuentes rf;
    rf.motor = [&rt]() -> nlohmann::json {
        if (motor::Motor* m = rt.engine().motor())
            if (const auto foto = m->instantanea()) return *foto;
        return nlohmann::json::object();
    };
    rf.escenas = [&rt]() {
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, resolucion::Tam>>>> out;
        BroadcastEngine& e = rt.engine();
        int lw = 0, lh = 0;
        if (!lienzoActual(lw, lh)) lw = e.videoConfig().baseWidth, lh = e.videoConfig().baseHeight;
        for (const std::string& esc : e.sceneNames()) {
            std::vector<std::pair<std::string, resolucion::Tam>> items;
            for (const auto& [fuente, r] : e.itemsVisibles(esc))
                items.push_back({fuente, {static_cast<int>(std::lround(r.w * lw)), static_cast<int>(std::lround(r.h * lh))}});
            out.push_back({esc, std::move(items)});
        }
        return out;
    };
    rf.camaraDe = [&rt](const std::string& entrada) {
        const nlohmann::json a = nlohmann::json::parse(rt.engine().sourceSettingsJson(entrada), nullptr, false);
        return a.is_object() ? a.value("camara", std::string()) : std::string();
    };
    rf.modosDe = [](const std::string& camara) { return video::modosDeCamara(camara); };
    rf.interior = [this](const std::string& elemento) { return video_ ? video_->gst().interior(elemento) : nlohmann::json::object(); };
    rf.grafo = [this]() {
        std::vector<resolucion::ResolucionModel::NodoGrafo> out;
        if (video_)
            for (const auto& n : video_->grafoVideo().nodos()) out.push_back({n.id, n.tipo, n.etiqueta, n.grupo});
        return out;
    };
    rf.lienzo = []() {
        int w = 0, h = 0;
        if (!lienzoActual(w, h)) w = 1920, h = 1080;
        return resolucion::Tam{w, h};
    };
    rf.fps = []() { return fpsActual(); };
    resolucion_ = std::make_unique<resolucion::ResolucionModel>(std::move(rf));
}

QObject* ImagenViewModel::camarasQml() const { return deCpp(camaras_.get()); }
QObject* ImagenViewModel::videoQml() const { return deCpp(video_.get()); }
QObject* ImagenViewModel::tiemposQml() const { return deCpp(tiempos_.get()); }
QObject* ImagenViewModel::resolucionQml() const { return deCpp(resolucion_.get()); }
QObject* ImagenViewModel::superresQml() const { return deCpp(superres_.get()); }
QObject* ImagenViewModel::bancoSrQml() const { return deCpp(bancoSr_.get()); }
QObject* ImagenViewModel::redQml() const { return deCpp(red_.get()); }
QObject* ImagenViewModel::seguidorQml() const { return deCpp(seguidor_.get()); }

void ImagenViewModel::ajustesAplicados() {
    camaras_->sincronizar(); // Mejorar imagen general: rige ya
    video_->setLegalizar(p_.rt.config().legalizar);
}

QVariantList ImagenViewModel::chequeosDelAparato(bool hayDestino) const {
    QVariantList out;
    const auto agregar = [&out](bool grave, const QString& t) { out << QVariantMap{{"grave", grave}, {"texto", t}}; };
    const QStorageInfo si(QString::fromStdWString((p_.rt.dataRoot() / "Recordings").wstring()));
    const double gb = si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1;
    const int kbps = p_.rt.config().encoder.videoBitrateKbps;
    if (gb >= 0 && kbps > 0) {
        const double horas = gb / (kbps * 1.1 * 3600.0 / 8.0 / 1e6);
        if (horas < 1)
            agregar(true, tr("Disco: quedan %1 GB, menos de una hora de grabación").arg(gb, 0, 'f', 1));
        else if (horas < 3)
            agregar(false, tr("Disco: alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1));
    }
    if (!hayDestino) agregar(false, tr("No hay destino de transmisión configurado (servidor y clave)"));
    for (const QString& e : video_->entradas()) {
        const nlohmann::json st = video_->estadoEntrada(e.toStdString());
        if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
        // Una entrada solo de audio (un tono, un micrófono por pipeline) no tiene
        // imagen nunca: no es «sin señal».
        const nlohmann::json aj = nlohmann::json::parse(p_.rt.engine().sourceSettingsJson(e.toStdString()), nullptr, false);
        if (aj.is_object() && aj.value("pipeline", std::string()).empty() && aj.value("uri", std::string()).empty() &&
            aj.value("camara", std::string()).empty() && !aj.value("pipeline_audio", std::string()).empty())
            continue;
        if (st.value("ancho", 0) <= 0) agregar(false, tr("La entrada «%1» no tiene señal ahora").arg(e));
    }
    return out;
}

QVariantMap ImagenViewModel::scopeUi(const QString& fuente, const QString& tipo, int ancho, int alto) const {
    video::TipoScope t = video::TipoScope::Onda;
    if (!video::tipoScopeDeTexto(tipo, t)) return {{"error", tr("tipo desconocido")}};
    QImage img;
    std::uint64_t serie = 0;
    // El programa: el cuadro que ya se rinde para los monitores (no cuesta otro render).
    if (fuente.isEmpty() && p_.cuadros.latest("program", img, serie) && !img.isNull()) {
    } else {
        // Otra fuente: se pide aparte (capturarAparte) y se usa el último cuadro
        // que llegó; el panel pregunta cada 200 ms, así que el próximo ya lo tiene.
        // La interfaz no espera el cuadro.
        if (!scopeCapturando_) {
            scopeCapturando_ = true;
            QPointer<const ImagenViewModel> yo(this);
            p_.rt.engine().capturarAparte(
                fuente.toStdString(), 640, [yo, fuente](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) {
                    QImage llego;
                    if (e.empty() && w > 0 && h > 0) llego = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
                    QMetaObject::invokeMethod(
                        qApp,
                        [yo, fuente, llego, e]() {
                            if (!yo) return;
                            yo->scopeCapturando_ = false;
                            yo->scopeFuente_ = fuente;
                            yo->scopeCuadro_ = llego;
                            yo->scopeError_ = QString::fromStdString(e);
                        },
                        Qt::QueuedConnection);
                });
        }
        if (scopeFuente_ != fuente || scopeCuadro_.isNull())
            return scopeError_.isEmpty() || scopeFuente_ != fuente ? QVariantMap{{"esperando", true}}
                                                                   : QVariantMap{{"error", scopeError_}};
        img = scopeCuadro_;
    }
    const video::Legalidad l = video::medirLegalidad(img);
    const QImage s = video::dibujarScope(img, t, std::clamp(ancho, 200, 1600), std::clamp(alto, 120, 1200));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    s.save(&buf, "PNG");
    return {{"imagen", "data:image/png;base64," + QString::fromLatin1(png.toBase64())},
            {"legalidad", QVariantMap{{"negros_aplastados_pct", std::round(l.lumaBajaPct * 10) / 10},
                                      {"blancos_quemados_pct", std::round(l.lumaAltaPct * 10) / 10},
                                      {"fuera_de_gama_pct", std::round(l.cromaPct * 10) / 10}}},
            {"legalizador", qs(p_.rt.config().legalizar)}};
}

QString ImagenViewModel::legalizarUi(const QString& modo) {
    const std::string m = modo.toStdString();
    if (m != "no" && m != "pal" && m != "ntsc" && m != "rango") return tr("modo desconocido");
    p_.rt.config().legalizar = m;
    p_.rt.save();
    video_->setLegalizar(m);
    return {};
}

} // namespace ba::app
