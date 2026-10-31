// src/app/ui/ImagenViewModel.cpp — ver el .hpp

#include "ui/ImagenViewModel.hpp"

#include "Motor.hpp"
#include "Piezas.hpp" // motor::redElectricaDelSistema, cadenciasDeLaRed, avisosDeLaRed
#include "arranque/AppRuntime.hpp"
#include "control/BusEventos.hpp"
#include "emision/VentanillaMotor.hpp"
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
#include "ui/VideoViewModel.hpp"
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
    // El PTZ de cada entrada (y Mejorar imagen): se entera de las entradas nuevas y de lo
    // que cambia en Configuración por la foto del motor.
    camaras_ = std::make_unique<ptz::ControlCamaras>(rt, p_.ventanilla, rt.dataRoot());
    armarTiempos();
    armarResolucion();
    // El grafo de tarjetas de video de cada entrada (después del PTZ). Es del dueño del
    // motor: al final se muda a su hilo (sus relojes corren allá, con el turno) y la
    // interfaz lo ve por su vista.
    video_ = std::make_unique<video::VideoGrafos>(rt, rt.dataRoot());
    videoVm_ = std::make_unique<VideoViewModel>(p_.ventanilla, *video_);
    connect(videoVm_.get(), &VideoViewModel::notify, this, &ImagenViewModel::notify);
    connect(&p_.entradas, &InputsViewModel::changed, video_.get(), &video::VideoGrafos::revisar);
    // «Priorizar programa» por calor (F4-05): el primer paso es la superresolución, que
    // pasa a barato (sin turnos activos) y vuelve a lo que tenía al aliviarse (los demás
    // pasos los atienden los modelos, las vistas del motor y la sala). Se lo pide al dueño.
    p_.recursos.alDegradar("superres", [this, antes = std::make_shared<nlohmann::json>()](bool apagar) {
        if (!videoVm_) return false;
        if (apagar) *antes = videoVm_->programadorJson().value("max_activas", nlohmann::json(3));
        const nlohmann::json tope = apagar ? nlohmann::json(0) : (antes->is_number() ? *antes : nlohmann::json(3));
        videoVm_->pedir("superres_calor", [tope](video::VideoGrafos& g) { return g.configurarProgramador({{"max_activas", tope}}); });
        return true;
    });
    // La superresolución que no llega: aviso al operador y evento en el bus (T40). La
    // avisa VideoGrafos desde el dueño: llega encolada.
    connect(videoVm_.get(), &VideoViewModel::superresTarde, this, [this](const QString& e, double ms, double cuadroMs, bool bypass) {
        emit notify((bypass ? tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3): pasada a barato")
                            : tr("La superresolución de «%1» no llega (%2 ms por cuadro de %3)"))
                        .arg(e)
                        .arg(ms, 0, 'f', 1)
                        .arg(cuadroMs, 0, 'f', 1),
                    "warn");
        p_.bus.publicar("superres/tarde", {{"entrada", e.toStdString()}, {"ms", ms}, {"cuadro_ms", cuadroMs}, {"bypass", bypass}});
    });
    // La superresolución de las entradas de red (motores, modelos, laboratorio).
    superres_ = std::make_unique<video::SuperRes>(rt.dataRoot(), videoVm_.get());
    {
        video::BancoSuperRes::Entorno en;
        en.alAire = [this]() { return p_.ventanilla.foto().estado.haySalida; };
        en.fps = [this]() {
            const auto& a = p_.ventanilla.foto().ajustes;
            return std::make_pair(a.fpsNum, a.fpsDen);
        };
        en.tamanosCamaras = [this]() { return videoVm_->tamanosDeEntradas(); };
        bancoSr_ = std::make_unique<video::BancoSuperRes>(rt.dataRoot(), *superres_, en);
    }
    // Lo que VideoGrafos le pregunta a la superresolución y al banco desde el dueño: el
    // plugin de una tarjeta (lee los archivos del plugin y el registro de GStreamer) y lo que
    // tarda (el banco tiene su candado). Se ponen antes de mudarlo y se sacan al irse.
    video_->setResolverPlugin([sr = superres_.get()](const std::string& id, std::string* error) {
        return sr->resolverPlugin(id, error);
    });
    video_->setEstimadorSr([b = bancoSr_.get()](const std::string& pr, int w, int h) { return b->estimarMs(pr, w, h); });
    // Los equipos de video de la red: la biblioteca de marcas y la base OUI
    // vienen con el programa (data/red).
    red_ = std::make_unique<red::Red>(ba::platform::bundleRoot() / "data" / "red",
                                      QString::fromStdWString((ba::platform::credentialsDir()).wstring()), videoVm_.get(),
                                      camaras_.get());
    // Seguir una cara con el PTZ (la ventana del PTZ y el asistente).
    seguidor_ = std::make_unique<Seguidor>(p_.personas, camaras_.get());
    // Ir a una posición a pedido (enfocar, dar la palabra, un preset) corta el
    // seguimiento de caras de esa cámara: si no, se pelean.
    camaras_->setDejarDeSeguir([this](const QString& e) {
        if (seguidor_) seguidor_->dejar(e);
    });
    seguidor_->setGrafos(videoVm_.get());
    // Armado y con todo puesto: al hilo del dueño.
    p_.ventanilla.adoptar(video_.get());
}

ImagenViewModel::~ImagenViewModel() {
    // El estimador y el resolvedor apuntan al banco y a la superresolución, que se van
    // antes que los grafos (que para entonces volvieron a este hilo: el plano paró).
    if (video_) {
        video_->setEstimadorSr(nullptr);
        video_->setResolverPlugin({});
    }
}

void ImagenViewModel::armarTiempos() {
    AppRuntime& rt = p_.rt;
    // Los tiempos: solo fotos que ya se publican (la instantánea del motor y la de las
    // salidas, que son para cualquier hilo; la vista del grafo y las fuentes, de la foto del
    // motor; los racks).
    tiempos_ = std::make_unique<tiempos::TiemposModel>([this]() {
        tiempos::Fotos f;
        BroadcastEngine& e = p_.motorEntreHilos;
        if (motor::Motor* m = e.motor())
            if (const auto foto = m->instantanea()) f.motor = *foto;
        f.salidas = e.salidasAhora();
        if (videoVm_)
            for (const auto& n : videoVm_->estudioJson().value("nodos", nlohmann::json::array())) {
                const std::string t = n.value("etiqueta", std::string());
                f.titulos[n.value("id", std::string())] = t.empty() ? n.value("titulo", std::string()) : t;
            }
#if defined(BA_HAVE_CLAP)
        if (audio::RackManager* racks = p_.racks ? p_.racks() : nullptr)
            for (const std::string& s : racks->fuentesConRack())
                if (audio::AudioRack* r = racks->rack(s, false)) f.racks[s] = r->describe();
#endif
        // Los lugares de la sala sin nadie: su entrada recibe la placa del gateway, pero no es nadie.
        for (const auto& [n, fu] : p_.ventanilla.foto().fuentesVideo.items()) {
            const nlohmann::json a = fu.is_object() ? fu.value("ajustes", nlohmann::json::object()) : nlohmann::json();
            if (!a.is_object() || !a.contains("slot")) continue;
            const std::string canal = a.value("channel", a.value("anillo", std::string("guest")));
            if (canal != "guest" && canal != "share") continue; // el remoto con la palabra: la sesión remota
            const int slot = a.value("slot", 0);
            if (slot > 0 && p_.sala.participanteEnSlot(slot).isEmpty())
                f.enEspera[n] = tr("el lugar %1 de la conferencia está vacío (llega la placa)").arg(slot).toStdString();
        }
        return f;
    }, rt.dataRoot() / "Config" / "tiempos-analisis.json");
    // El análisis por elemento (las sondas del motor): lo llama el modelo en su hilo (el
    // motor de GStreamer lo hace con el candado de la entrada).
    tiempos_->setSondas({[&me = p_.motorEntreHilos](const std::string& e) -> std::string {
                             motor::Motor* m = me.motor();
                             return m != nullptr ? m->analizarEmpezar(e) : tr("el motor no está andando").toStdString();
                         },
                         [&me = p_.motorEntreHilos](const std::string& e) -> nlohmann::json {
                             motor::Motor* m = me.motor();
                             return m != nullptr ? m->analizarTerminar(e)
                                                 : nlohmann::json{{"error", tr("el motor no está andando").toStdString()}};
                         }});
}

void ImagenViewModel::armarResolucion() {
    // El plan de resolución (solo lectura): lo que publican el motor (su instantánea), la
    // foto del motor (las escenas, las fuentes, la vista del grafo) y los dispositivos.
    resolucion::ResolucionModel::Fuentes rf;
    rf.motor = [&me = p_.motorEntreHilos]() -> nlohmann::json {
        if (motor::Motor* m = me.motor())
            if (const auto foto = m->instantanea()) return *foto;
        return nlohmann::json::object();
    };
    rf.escenas = [this]() {
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, resolucion::Tam>>>> out;
        const FotoMotor& f = p_.ventanilla.foto();
        int lw = 0, lh = 0;
        if (!lienzoActual(lw, lh)) lw = f.ajustes.baseAncho, lh = f.ajustes.baseAlto;
        for (const FotoMotor::Escena& esc : f.escenas) {
            std::vector<std::pair<std::string, resolucion::Tam>> items;
            for (const FotoMotor::Fuente& fu : esc.fuentes)
                if (fu.visible)
                    items.push_back({fu.nombre,
                                     {static_cast<int>(std::lround(fu.lugar.w * lw)), static_cast<int>(std::lround(fu.lugar.h * lh))}});
            out.push_back({esc.nombre, std::move(items)});
        }
        return out;
    };
    rf.camaraDe = [this](const std::string& entrada) {
        const nlohmann::json fu = p_.ventanilla.foto().fuentesVideo.value(entrada, nlohmann::json::object());
        const nlohmann::json a = fu.is_object() ? fu.value("ajustes", nlohmann::json::object()) : nlohmann::json();
        return a.is_object() ? a.value("camara", std::string()) : std::string();
    };
    rf.modosDe = [](const std::string& camara) { return video::modosDeCamara(camara); };
    rf.interior = [this](const std::string& elemento) {
        return videoVm_ ? videoVm_->gst()->interior(elemento) : nlohmann::json::object();
    };
    rf.grafo = [this]() {
        std::vector<resolucion::ResolucionModel::NodoGrafo> out;
        if (videoVm_)
            for (const auto& n : videoVm_->nodos())
                out.push_back({n.value("id", std::string()), n.value("tipo", std::string()), n.value("etiqueta", std::string()),
                               n.value("grupo", std::string())});
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
QObject* ImagenViewModel::videoQml() const { return deCpp(videoVm_.get()); }
QObject* ImagenViewModel::tiemposQml() const { return deCpp(tiempos_.get()); }
QObject* ImagenViewModel::resolucionQml() const { return deCpp(resolucion_.get()); }
QObject* ImagenViewModel::superresQml() const { return deCpp(superres_.get()); }
QObject* ImagenViewModel::bancoSrQml() const { return deCpp(bancoSr_.get()); }
QObject* ImagenViewModel::redQml() const { return deCpp(red_.get()); }
QObject* ImagenViewModel::seguidorQml() const { return deCpp(seguidor_.get()); }

void ImagenViewModel::ajustesAplicados() {
    // Mejorar imagen general y el legalizador rigen ya: las cámaras releen la foto cuando
    // llega con los ajustes nuevos, y el legalizador lo relee el dueño (y rearma).
    videoVm_->pedir("video_legalizar", [](video::VideoGrafos& g) {
        const std::string antes = g.legalizar();
        g.releerLegalizar();
        if (g.legalizar() != antes) g.reaplicarEntradas();
        return std::string();
    });
    emit redElectricaChanged(); // el lienzo pudo cambiar
}

QVariantMap ImagenViewModel::redElectrica() const {
    const int hz = motor::redElectricaDelSistema();
    const FotoMotor::AjustesVideo& v = p_.ventanilla.foto().ajustes;
    motor::Lienzo l;
    l.fpsN = v.fpsNum;
    l.fpsD = std::max(1, v.fpsDen);
    const std::vector<std::string> avisos = motor::avisosDeLaRed(l, hz);
    QVariantList cadencias;
    for (const auto& [n, d] : motor::cadenciasDeLaRed(hz))
        cadencias << QVariantMap{{"valor", qs(textoDeFps(n, d))}, {"texto", qs(fpsLegible(n, d))}};
    return {{"hz", hz}, {"aviso", avisos.empty() ? QString() : qs(avisos.front())}, {"cadencias", cadencias}};
}

QVariantList ImagenViewModel::chequeosDelAparato(bool hayDestino) const {
    QVariantList out;
    const auto agregar = [&out](bool grave, const QString& t) { out << QVariantMap{{"grave", grave}, {"texto", t}}; };
    const QStorageInfo si(QString::fromStdWString((p_.rt.dataRoot() / "Recordings").wstring()));
    const double gb = si.isValid() ? static_cast<double>(si.bytesAvailable()) / 1e9 : -1;
    const int kbps = p_.ventanilla.foto().configImagen.value("video_kbps", 0);
    if (gb >= 0 && kbps > 0) {
        const double horas = gb / (kbps * 1.1 * 3600.0 / 8.0 / 1e6);
        if (horas < 1)
            agregar(true, tr("Disco: quedan %1 GB, menos de una hora de grabación").arg(gb, 0, 'f', 1));
        else if (horas < 3)
            agregar(false, tr("Disco: alcanza para ~%1 h de grabación").arg(horas, 0, 'f', 1));
    }
    if (!hayDestino) agregar(false, tr("No hay destino de transmisión configurado (servidor y clave)"));
    for (const QString& e : videoVm_->entradas()) {
        const nlohmann::json st = videoVm_->estadoEntrada(e.toStdString());
        if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
        // Una entrada solo de audio (un tono, un micrófono por pipeline) no tiene
        // imagen nunca: no es «sin señal».
        const nlohmann::json aj = st.value("ajustes", nlohmann::json::object());
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
            // La captura la arranca el dueño del motor; el cuadro vuelve a este hilo.
            QPointer<const ImagenViewModel> yo(this);
            p_.ventanilla.pedir("scope_cuadro", [&rt = p_.rt, yo, fuente]() {
                rt.engine().capturarAparte(
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
            {"legalizador", qs(p_.ventanilla.foto().configImagen.value("legalizar", std::string("no")))}};
}

QString ImagenViewModel::legalizarUi(const QString& modo) {
    const std::string m = modo.toStdString();
    if (m != "no" && m != "pal" && m != "ntsc" && m != "rango") return tr("modo desconocido");
    // La configuración y el motor son del dueño: allá se guarda y se rearma.
    videoVm_->pedir("video_legalizar", [&rt = p_.rt, m](video::VideoGrafos& g) {
        rt.config().legalizar = m;
        rt.save();
        rt.fotoSucia(FotoMotor::Ajustes);
        g.setLegalizar(m);
        return std::string();
    });
    return {};
}

} // namespace ba::app
