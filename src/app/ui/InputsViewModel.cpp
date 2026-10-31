// ============================================================================
// src/app/ui/InputsViewModel.cpp — ver InputsViewModel.hpp
// ============================================================================

#include "ui/InputsViewModel.hpp"
#include <QCoreApplication>
#include "platform/Paths.hpp"

#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

#include <nlohmann/json.hpp>

#include "arranque/AppRuntime.hpp"
#include "emision/VentanillaMotor.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "emision/InputSpecs.hpp"
#include "Piezas.hpp" // motor::avisoDeCadenciaDeCamara
#include "video/Camaras.hpp"
#if defined(BA_HAVE_NDI)
#include "emision/NdiSource.hpp"
#endif
#include "ui/StudioViewModel.hpp"

namespace ba::app {

using json = nlohmann::json;

InputsViewModel::InputsViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, StudioViewModel& vm, QObject* parent)
    : QObject(parent), rt_(rt), ventanilla_(ventanilla), vm_(vm) {
    // Las miniaturas, 1 vez por segundo mientras la pagina esta a la vista.
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, [this]() {
        ++stamp_;
        emit stampChanged();
    });
    // EL MODELO SE ENTERA SOLO de las entradas que se agregan o se quitan, lo pida
    // quien lo pida: el MCP (audio_input_add, video_input_add), un plugin, el
    // guion del evento o «Agregar como entrada» de Dispositivos. Las entradas (y su
    // tamaño, que llega cuando la cámara arranca) vienen con la foto del motor.
    connect(&ventanilla_, &VentanillaMotor::cambio, this, [this](unsigned partes) {
        if ((partes & (FotoMotor::Video | FotoMotor::Escenas)) != 0) revisarEntradas(partes);
    });
    // Las entradas de audio del equipo no son de ninguna escena: cuando cambian (un aviso
    // «Audio» que no es un fader), que la foto las rearme. Corre en el dueño del motor.
    observador_ = rt_.observar([&rt = rt_, antes = std::make_shared<std::vector<std::string>>()](AppRuntime::Cambio c) {
        if (c != AppRuntime::Cambio::Audio) return;
        auto ahora = rt.engine().globalAudioInputs();
        if (ahora == *antes) return; // un volumen: nada que rearmar
        *antes = std::move(ahora);
        rt.fotoSucia(FotoMotor::Video);
    });
    rebuildInputs();
}

void InputsViewModel::revisarEntradas(unsigned) { rebuildInputs(); }

InputsViewModel::~InputsViewModel() {
    if (observador_ >= 0) rt_.dejarDeObservar(observador_);
    if (!held_.empty())
        ventanilla_.pedir("entradas_mostrar", [&rt = rt_, held = held_]() {
            for (const auto& n : held) rt.engine().holdShowing(n, false);
        });
}

QStringList InputsViewModel::scenes() const {
    QStringList out;
    for (const auto& e : ventanilla_.foto().escenas) out << QString::fromStdString(e.nombre);
    return out;
}

void InputsViewModel::pedirAgregar(const char* que, std::function<std::string(AppRuntime&)> orden, Listo listo) {
    ventanilla_.pedir(
        this, que, [&rt = rt_, orden = std::move(orden)]() { return orden(rt); },
        [this, que = QString::fromLatin1(que), listo = std::move(listo)](const std::string& e) {
            if (listo)
                listo(QString::fromStdString(e));
            else
                emit agregado(que, QString::fromStdString(e));
            if (e.empty()) refresh();
        });
}

bool InputsViewModel::ndiAvailable() const {
#if defined(BA_HAVE_NDI)
    return true;
#else
    return false;
#endif
}

void InputsViewModel::setDispositivos(dispositivos::Dispositivos* d) {
    if (dd_ == d) return;
    if (dd_ != nullptr) disconnect(dd_, nullptr, this, nullptr);
    dd_ = d;
    if (dd_ == nullptr) return;
    // Un nombre nuevo, un equipo que se enchufa o se va: las listas para elegir
    // y las entradas que lo usan se enteran solas (el modelo avisa).
    connect(dd_, &dispositivos::Dispositivos::cambio, this, [this]() {
        rebuildDevices();
        rebuildInputs();
    });
    rebuildDevices();
}

void InputsViewModel::searchNdi() {
    // Busca el detector «ndi» del registro: lo encontrado queda ahí (con su nombre)
    // y la lista de acá se rehace cuando avisa.
    if (ndiSearching_ || dd_ == nullptr) return;
    ndiSearching_ = true;
    emit ndiChanged();
    QPointer<InputsViewModel> self(this);
    dd_->escanear("ndi", [self](QVariantMap) {
        if (!self) return;
        self->ndiSearching_ = false;
        self->rebuildDevices();
        emit self->ndiChanged();
    });
}

QString InputsViewModel::addNdi(const QString& ndiName, const QString& name, const QString& scene, Listo listo) {
#if defined(BA_HAVE_NDI)
    if (ndiName.isEmpty()) return tr("Elegi una fuente NDI.");
    QString nm = name.trimmed();
    if (nm.isEmpty()) {
        nm = "NDI " + ndiName;
        for (const QVariant& v : ndiFound_)
            if (v.toMap().value("name").toString() == ndiName && v.toMap().value("propio").toBool())
                nm = v.toMap().value("label").toString();
    }
    SourceSpec spec;
    spec.id = ndi::kSourceId;
    spec.name = nm.toStdString();
    spec.settingsJson = json{{"ndi_name", ndiName.toStdString()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    pedirAgregar("ndi", [spec, sc = scene.toStdString()](AppRuntime& rt) {
        if (!rt.addSource(sc.empty() ? rt.engine().currentScene() : sc, spec)) return rt.lastError();
        rt.save();
        return std::string();
    }, std::move(listo));
    return {};
#else
    (void)ndiName;
    (void)name;
    (void)scene;
    (void)listo;
    return tr("Esta version no trae NDI.");
#endif
}

bool InputsViewModel::hardwareAvailable() const { return ventanilla_.foto().configImagen.value("captura", false); }

void InputsViewModel::refresh() {
    // Buscar de nuevo lo de la máquina: al terminar, el registro avisa (cambio) y
    // las listas se rehacen.
    if (dd_ != nullptr) dd_->escanear("local", [](QVariantMap) {});
    rebuildDevices();
    rebuildInputs();
}

void InputsViewModel::rebuildDevices() {
    if (dd_ == nullptr) return;
    // Cada dispositivo para elegir: `name` es el nombre (el del operador si le
    // puso uno; con ese nombre se crea la entrada), `label` lo que se muestra
    // (con «desconectado» si no está), `value` lo que guarda la entrada.
    auto item = [](const QVariant& v) {
        const QVariantMap e = v.toMap();
        const QString nombre = e.value("nombre").toString();
        const bool ausente = e.value("estado").toString() == "ausente";
        return QVariantMap{{"name", nombre},
                           {"label", ausente ? nombre + " (desconectado)" : nombre},
                           {"value", e.value("valor")},
                           {"id", e.value("id")},
                           {"tecnico", e.value("tecnico")},
                           {"propio", e.value("nombre_propio")},
                           {"conectado", e.value("conectado")},
                           {"estado", e.value("estado")},
                           {"config", e.value("config")}};
    };
    QVariantList dev, aud, ndi;
    for (const QVariant& v : dd_->equipos("camara_local"))
        dev << item(v);
    for (const QVariant& v : dd_->equipos("microfono")) {
        // La predeterminada del sistema, primero (es la que sirve si no se sabe cuál).
        if (v.toMap().value("valor").toString() == "default")
            aud.prepend(item(v));
        else
            aud << item(v);
    }
    for (const QVariant& v : dd_->equipos("ndi")) {
        QVariantMap m = item(v);
        // addNdi recibe el nombre con que se anuncia el emisor (el valor); se muestra el del operador.
        m["name"] = m.value("value");
        if (v.toMap().value("estado").toString() == "ausente")
            m["label"] = tr("%1 (no se ve)").arg(v.toMap().value("nombre").toString());
        ndi << m;
    }
    if (dev != devices_ || aud != audioDevices_) {
        devices_ = dev;
        audioDevices_ = aud;
        emit changed();
    }
    if (ndi != ndiFound_) {
        ndiFound_ = ndi;
        emit ndiChanged();
    }
}

QVariantMap InputsViewModel::dispositivoDe(const QString& tipo, const QString& valor) const {
    const QVariantMap d = dd_ != nullptr ? dd_->porValor(tipo, valor) : QVariantMap{};
    if (d.isEmpty()) return {{"dispositivo", QString()}, {"dispositivoId", QString()}, {"estado", QString()}, {"conectado", true}};
    return {{"dispositivo", d.value("nombre")},
            {"dispositivoId", d.value("id")},
            {"estado", d.value("estado")},
            // «sin buscar todavía» no es ausente: no se lo pinta de rojo.
            {"conectado", d.value("estado").toString() != "ausente"}};
}

QString InputsViewModel::audioInputName(const QString& device) const {
    // El dispositivo "default" se llama "Default": con ese nombre el operador
    // no sabe que es. El resto ya trae el nombre del equipo.
    for (const QVariant& d : audioDevices_)
        if (d.toMap().value("value").toString() == device) return d.toMap().value("name").toString();
    if (device == "default") return tr("Entrada de audio predeterminada");
    return tr("Micrófono");
}

QString InputsViewModel::addAudioInput(const QString& device, const QString& name, Listo listo) {
    if (device.isEmpty()) return tr("Elegí una entrada de audio.");
    QString nm = name.trimmed();
    if (nm.isEmpty()) nm = audioInputName(device);
    const SourceSpec spec = inputs::audioInputSpec(device.toStdString(), nm.toStdString());
    // Canal GLOBAL, como el audio de los invitados: suena en todas las escenas
    // (un microfono no pertenece a una escena).
    pedirAgregar("audio", [spec](AppRuntime& rt) {
        if (!rt.addAudioInput(spec)) return rt.lastError();
        rt.save();
        return std::string();
    }, std::move(listo));
    return {};
}

void InputsViewModel::rebuildInputs() {
    // Las fuentes, de la foto del motor (FotoMotor::fuentesVideo), en el orden del motor.
    const FotoMotor& foto = ventanilla_.foto();
    std::vector<std::pair<int, std::string>> fuentes, globales;
    for (const auto& [n, f] : foto.fuentesVideo.items()) {
        if (!f.is_object()) continue;
        if (f.value("orden", -1) >= 0) fuentes.emplace_back(f.value("orden", 0), n);
        if (f.value("global", false)) globales.emplace_back(f.value("orden_global", 0), n);
    }
    std::sort(fuentes.begin(), fuentes.end());
    std::sort(globales.begin(), globales.end());
    QVariantList out;
    for (const auto& [orden, n] : fuentes) {
        const json& f = foto.fuentesVideo[n];
        const std::string type = f.value("tipo", std::string());
        const json s = f.value("ajustes", json::object());
        const bool hw = inputs::esCamara(type, s);
        const bool isNdi = inputs::tipoLogico(type, s) == "ba_ndi_input";
        const bool net = inputs::isNetworkInput(type, s);
        if (!hw && !net) continue;
        const int w = f.value("ancho", 0), h = f.value("alto", 0);
        QStringList inScenes;
        for (const auto& sc : f.value("escenas", json::array()))
            if (sc.is_string()) inScenes << QString::fromStdString(sc.get<std::string>());
        // El dispositivo de la máquina que usa (con el nombre del operador): la
        // cámara por su id, el emisor NDI por su nombre, la cámara IP por su host.
        QVariantMap disp;
        if (hw)
            disp = dispositivoDe("camara_local", QString::fromStdString(inputs::camaraDe(type, s)));
        else if (isNdi)
            disp = dispositivoDe("ndi", QString::fromStdString(s.value("ndi_name", std::string())));
        else
            disp = dispositivoDe("camara_red",
                                 QUrl(QString::fromStdString(s.value("uri", std::string()))).host());
        QVariantMap fila{
            {"name", QString::fromStdString(n)},
            {"kind", hw      ? "hardware"
                     : isNdi ? "ndi"
                             : "red"},
            {"where", QString::fromStdString(
                          hw      ? inputs::camaraDe(type, s)
                          : isNdi ? tr("NDI: %1").arg(QString::fromStdString(s.value("ndi_name", s.value("host", std::string())))).toStdString()
                          : type == "ba_gst_fuente"
                              ? (s.contains("ba_plugin")
                                     ? tr("Plugin: %1").arg(QString::fromStdString(s.value("ba_plugin", std::string()))).toStdString()
                                     : inputs::redactUrl(s.value("uri", s.value("pipeline", std::string()))))
                              : std::string())},
            {"width", w},
            {"height", h},
            {"receiving", w > 0},
            {"state", QString::fromStdString(f.value("medio", std::string()))},
            {"scenes", inScenes}};
        fila.insert(disp);
        // Una cámara a otra cadencia casi igual que el lienzo (29,97 en 30, F2-17): videorate
        // repite o tira un cuadro cada tanto. El modo que eligió el motor dice su cadencia.
        if (hw) {
            const json modo = f.value("modo_camara", json());
            motor::Lienzo l;
            l.fpsN = foto.ajustes.fpsNum;
            l.fpsD = std::max(1, foto.ajustes.fpsDen);
            const std::string aviso = motor::avisoDeCadenciaDeCamara(modo.is_object() ? modo.value("cps", 0.0) : 0.0, l);
            if (!aviso.empty()) fila["aviso"] = QString::fromStdString(aviso);
        }
        // Lo técnico queda a mano (tooltip); a la vista, el nombre del dispositivo.
        fila["tecnico"] = fila.value("where");
        if (!disp.value("dispositivo").toString().isEmpty()) fila["where"] = disp.value("dispositivo");
        out << fila;
    }
    // Entradas de audio del equipo: no son items de escena (van por un canal
    // global), asi que no salen del recorrido de arriba. Se listan igual: para
    // el operador son entradas como cualquier otra.
    QVariantList video = out, audio;
    for (const auto& [orden, n] : globales) {
        const json s = foto.fuentesVideo[n].value("ajustes", json::object());
        const QString mic = QString::fromStdString(s.value("microfono", s.value(inputs::kAudioInProp, std::string())));
        const QVariantMap disp = dispositivoDe("microfono", mic);
        QVariantMap fila{{"name", QString::fromStdString(n)},
                         {"kind", "audio"},
                         {"where", disp.value("dispositivo").toString().isEmpty() ? mic : disp.value("dispositivo").toString()},
                         {"tecnico", mic},
                         {"width", 0},
                         {"height", 0},
                         // Un micrófono ausente (desenchufado, o que todavía no volvió
                         // después de un corte) no manda nada: se lo espera.
                         {"receiving", disp.value("conectado").toBool()},
                         {"state", QString()},
                         {"scenes", QStringList{tr("todas (audio)")}}};
        fila.insert(disp);
        out << fila;
        audio << fila;
    }
    if (video != videoInputs_) {
        videoInputs_ = video;
        emit videoInputsChanged();
    }
    if (audio != audioInputs_) {
        audioInputs_ = audio;
        emit audioInputsChanged();
    }
    if (out != inputs_) {
        inputs_ = out;
        emit changed();
    }
    syncHolds();
}

void InputsViewModel::syncHolds() {
    std::set<std::string> want;
    if (active_)
        for (const QVariant& v : inputs_)
            want.insert(v.toMap().value("name").toString().toStdString());
    // Mantenerlas «mostrándose» (las miniaturas vivas) se le pide al dueño del motor.
    std::vector<std::pair<std::string, bool>> cambios;
    for (const auto& n : held_)
        if (!want.count(n)) cambios.emplace_back(n, false);
    for (const auto& n : want)
        if (!held_.count(n)) cambios.emplace_back(n, true);
    held_ = want;
    if (!cambios.empty())
        ventanilla_.pedir("entradas_mostrar", [&rt = rt_, cambios]() {
            for (const auto& [n, si] : cambios) rt.engine().holdShowing(n, si);
        });
}

void InputsViewModel::setActive(bool on) {
    active_ = on;
    if (on) {
        refresh();
        tick_.start();
    } else {
        tick_.stop();
    }
    syncHolds();
}

QVariantList InputsViewModel::modes(const QString& device) const {
    // Los modos del dispositivo (tamaño, cuadros, formato y quién decodifica), del monitor de
    // GStreamer; primero «Automático» (el que conviene para el lienzo, y por qué).
    QVariantList out;
    std::string porque;
    const auto a = video::modoAutomatico(device.toStdString(), &porque);
    out << QVariantMap{{"label", a.ancho > 0 ? tr("Automático: %1").arg(QString::fromStdString(a.etiqueta())) : tr("Automático")},
                       {"modo", QString()},
                       {"detalle", QString::fromStdString(porque)}};
    for (const auto& m : video::modosDeCamara(device.toStdString())) {
        if (!m.usable()) continue;
        out << QVariantMap{{"label", QString::fromStdString(m.etiqueta())}, {"modo", QString::fromStdString(m.valor())}};
    }
    return out;
}

QString InputsViewModel::addHardware(const QString& device, const QString& name, const QString& scene,
                                     const QVariantMap& mode, Listo listo) {
    if (device.isEmpty()) return tr("Elegi una camara.");
    json opts = json::object();
    if (!mode.value("modo").toString().isEmpty()) opts["modo"] = mode.value("modo").toString().toStdString(); // vacío: automático
    QString nm = name.trimmed();
    for (const QVariant& v : devices_) {
        const QVariantMap d = v.toMap();
        if (d.value("value").toString() != device) continue;
        // Con el nombre que le puso el operador, tal cual; sin nombre propio, «Cámara <la del sistema>».
        if (nm.isEmpty()) nm = d.value("propio").toBool() ? d.value("name").toString() : tr("Cámara %1").arg(d.value("name").toString());
        // El formato preferido de ese dispositivo (Dispositivos y equipos), si no se eligió otro.
        const QString preferido = d.value("config").toMap().value("modo").toString();
        if (!opts.contains("modo") && !preferido.isEmpty()) opts["modo"] = preferido.toStdString();
    }
    const SourceSpec spec = inputs::hardwareSpec(device.toStdString(), nm.toStdString(), opts);
    pedirAgregar("hardware", [spec, sc = scene.toStdString()](AppRuntime& rt) {
        if (!rt.addSource(sc.empty() ? rt.engine().currentScene() : sc, spec)) return rt.lastError();
        rt.save();
        return std::string();
    }, std::move(listo));
    return {};
}

QString InputsViewModel::addNetwork(const QString& url, const QString& name, const QString& scene,
                                    bool lowLatency, Listo listo) {
    const std::string u = url.trimmed().toStdString();
    if (inputs::networkScheme(u).empty())
        return tr("La direccion tiene que empezar con rtsp://, rtmp://, srt://, http(s)://, udp:// o rtp://.");
    const std::string nm =
        name.trimmed().isEmpty() ? tr("Red %1").arg(QString::fromStdString(inputs::redactUrl(u).substr(0, 40))).toStdString()
                                 : name.trimmed().toStdString();
    const SourceSpec spec =
        inputs::networkSpec(u, nm, json{{"latency", lowLatency ? "low" : "normal"}},
                            (ba::platform::bundleRoot() / "data" / "cacert.pem").string());
    pedirAgregar("red", [spec, sc = scene.toStdString()](AppRuntime& rt) {
        if (!rt.addSource(sc.empty() ? rt.engine().currentScene() : sc, spec)) return rt.lastError();
        rt.save();
        return std::string();
    }, std::move(listo));
    return {};
}

void InputsViewModel::remove(const QString& name) {
    const std::string n = name.toStdString();
    const bool estabaViva = held_.erase(n) > 0;
    ventanilla_.pedir(this, "entrada_quitar", [&rt = rt_, n, estabaViva]() {
        if (estabaViva) rt.engine().holdShowing(n, false);
        for (const auto& sc : rt.engine().sceneNames())
            for (const auto& src : rt.engine().sceneSources(sc))
                if (src == n) rt.removeSource(sc, n);
        // Las entradas de audio no estan en ninguna escena: van por su canal global.
        rt.removeAudioInput(n);
        rt.save();
    }, [this]() { refresh(); });
}

QImage InputThumbProvider::requestImage(const QString& id, QSize* size, const QSize& requested) {
    const QString name = QUrl::fromPercentEncoding(id.section('?', 0, 0).toUtf8());
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    QImage img;
    // Esto corre en un hilo de carga de imágenes (ForceAsynchronousImageLoading):
    // qué capturar y de qué tamaño lo averigua el dueño del motor (lee escenas, fuentes
    // y el estado del motor, que son suyos), con una orden por la ventanilla que este
    // hilo espera (no es la interfaz: puede esperar, con tope por si el plano ya paró);
    // esperar el cuadro, que es lo lento, acá.
    struct Preparada {
        std::string error = "sin motor", clave;
        int w = 0, h = 0;
        bool esEscena = false;
        BroadcastEngine* engine = nullptr;
    };
    auto prometida = std::make_shared<std::promise<Preparada>>();
    std::future<Preparada> lista = prometida->get_future();
    const std::string nombre = name.toStdString();
    const int ancho = requested.width() > 0 ? requested.width() : 480;
    ventanilla_.pedir("entrada_miniatura", [&rt = rt_, prometida, nombre, ancho]() {
        Preparada p;
        p.engine = &rt.engine();
        p.error = p.engine->prepararCaptura(nombre, ancho, p.clave, p.w, p.h, p.esEscena);
        prometida->set_value(std::move(p));
    });
    if (lista.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
        Preparada p;
        try {
            p = lista.get();
        } catch (const std::future_error&) {
            // La orden no corrió (el plano ya paró, al cerrar): sin miniatura.
        }
        if (p.error.empty() && p.engine != nullptr && p.engine->capturarVista(p.clave, p.w, p.h, p.esEscena, rgba, w, h).empty())
            img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
    }
    if (img.isNull()) {
        img = QImage(16, 9, QImage::Format_RGBA8888);
        img.fill(Qt::black);
    }
    if (size != nullptr) *size = img.size();
    return img;
}

} // namespace ba::app
