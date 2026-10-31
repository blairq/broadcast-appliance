// src/app/ui/AlarmasViewModel.cpp — ver el .hpp

#include "ui/AlarmasViewModel.hpp"

#include "agent/AlarmaSonora.hpp"
#include "agent/EntradasAlarmas.hpp"
#include "agent/FlujosTexto.hpp"
#include "arranque/AppRuntime.hpp"
#include "arranque/EnergiaAire.hpp"
#include "emision/TurnoMotor.hpp"
#include "emision/AvisoFueraDeUi.hpp"
#include "i18n/I18n.hpp"
#include "ui/ChatOverlayRenderer.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/RecursosViewModel.hpp"
#include "video/VideoGrafos.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlEngine>

#include <map>
#include <memory>
#include <mutex>

namespace ba::app {

using json = nlohmann::json;

AlarmasViewModel::AlarmasViewModel(Fuentes f, QObject* parent)
    : QObject(parent),
      entradas_(std::make_unique<agent::EntradasAlarmas>(deLaInterfaz(f), directas(f), sinLaInterfaz())),
      sonido_(std::make_unique<agent::AlarmaSonora>(f.rt.dataRoot())) {
    entradas_->refrescar();
    motor_ = std::make_unique<agent::AlertEngine>(entradas_->paraElVigia(),
                                                  (f.rt.dataRoot() / "Config" / "alertas.json").string());
    // Los notificadores, en el hilo del vigía (conexión directa): salen aunque la interfaz
    // no ande. Lo demás, en la interfaz (este objeto vive en ella: la conexión es encolada).
    connect(motor_.get(), &agent::AlertEngine::fired, motor_.get(), [this](const json& ev) { enHiloDelVigia(ev); },
            Qt::DirectConnection);
    connect(motor_.get(), &agent::AlertEngine::fired, this, [this](const json& ev) { enLaInterfaz(ev); });
    // La vista se entera por el modelo.
    connect(motor_.get(), &agent::AlertEngine::alarmasCambio, this, &AlarmasViewModel::releer);
    releer();
    motor_->start(250);
}

AlarmasViewModel::~AlarmasViewModel() = default;

agent::AlertInputs AlarmasViewModel::deLaInterfaz(const Fuentes& f) {
    agent::AlertInputs in;
    AppRuntime& rt = f.rt;
    in.audio = [&rt]() {
        std::vector<agent::AlertInputs::Audio> out;
        for (const std::string& n : rt.engine().audioSources())
            out.push_back({n, rt.engine().sourcePeakDb(n), rt.engine().sourceMuted(n)});
        return out; // la mezcla del programa va directa (abajo)
    };
    in.media = [&rt]() {
        std::vector<agent::AlertInputs::Media> out;
        for (const auto& m : rt.engine().mediaSources())
            out.push_back({m.name, m.state, m.durationMs, m.timeMs});
        return out;
    };
    in.room = [s = &f.sala]() {
        return json::parse(QJsonDocument(s->client().snapshot()).toJson(QJsonDocument::Compact).toStdString(), nullptr,
                           false);
    };
    in.chat = [c = &f.chat]() {
        std::vector<json> out;
        for (const auto& m : c->collect())
            out.push_back({{"key", m.key.toStdString()},
                           {"platform", m.source.toStdString()},
                           {"author", m.author.toStdString()},
                           {"text", m.text.toStdString()}});
        return out;
    };
    // Las salidas de texto (voz a texto con nombre): las últimas frases.
    in.textos = [flujos = f.flujos]() {
        std::vector<json> out;
        agent::FlujosTexto* fl = flujos ? flujos() : nullptr;
        if (fl == nullptr) return out;
        const std::uint64_t ultimo = fl->ultimoSeq();
        for (const auto& x : fl->desde(ultimo > 50 ? ultimo - 50 : 0))
            out.push_back({{"key", std::to_string(x.seq)},
                           {"flujo", x.flujo.toStdString()},
                           {"texto", x.texto.toStdString()},
                           {"origen", x.origen.toStdString()}});
        return out;
    };
    in.roomChat = [s = &f.sala]() {
        return json::parse(
            QJsonDocument(static_cast<JsonListModel*>(s->chat())->items()).toJson(QJsonDocument::Compact).toStdString(),
            nullptr, false);
    };
    return in;
}

agent::AlertInputs AlarmasViewModel::directas(const Fuentes& f) {
    agent::AlertInputs directas;
    AppRuntime& rt = f.rt;
    BroadcastEngine& motor = rt.engine();
    // La mezcla del programa (lo que sale): para el silencio y la saturación de fábrica.
    directas.audio = [&motor]() {
        return std::vector<agent::AlertInputs::Audio>{{"Programa", motor.picoProgramaVigiaDb(), false}};
    };
    // De la foto de las salidas que publica el hilo de salidas del motor (no de status(),
    // que recorre las salidas de la interfaz).
    directas.status = [&rt]() {
        const json fo = rt.engine().fotoSalidas();
        const json t = fo.value("principal", json::object());
        agent::AlertInputs::Status a;
        a.streaming = t.value("andando", false);
        a.droppedFrames = t.value("perdidos", 0);
        a.totalFrames = t.value("cuadros", 0);
        a.reconnecting = t.value("reconectando", false);
        a.recording = fo.value("grabacion", json::object()).value("activa", false);
        a.grabacionPedida = !rt.recordStopWanted();
        const json salidas = fo.value("salidas", json::object()); // con nombre: UB si no
        for (const auto& [id, x] : salidas.items())
            if (x.value("grabacion", json::object()).value("caida", false)) a.grabacionesCaidas.push_back(id);
        return a;
    };
    directas.recursos = [recursos = f.recursos]() {
        RecursosViewModel* r = recursos ? recursos() : nullptr;
        return r != nullptr ? r->muestreador().niveles() : json::object();
    };
    directas.persistencia = [&rt]() { return rt.avisosPersistencia(); };
    // El programa de cada salida en imagen, con si sale (transmite o graba): cada una suena
    // por su lado. Arriba, el de la por omisión.
    directas.programaVideo = [&rt, &motor]() {
        const auto v = motor.programaVideo();
        json out{{"valido", v.valido}, {"luma", v.luma}, {"quieto_s", v.quietoSeg}, {"salidas", json::array()}};
        const json fo = motor.fotoSalidas().value("salidas", json::object());
        for (const json& s : rt.salidasPublicadas()) {
            const std::string id = s.value("id", std::string());
            const auto pv = motor.programaVideo(id);
            const json f = fo.value(id, json::object());
            const bool sale = f.value("principal", json::object()).value("andando", false) ||
                              f.value("grabacion", json::object()).value("activa", false);
            out["salidas"].push_back({{"id", id},
                                      {"nombre", s.value("nombre", id)},
                                      {"por_omision", s.value("por_omision", false)},
                                      {"valido", pv.valido},
                                      {"luma", pv.luma},
                                      {"quieto_s", pv.quietoSeg},
                                      {"sale", sale}});
        }
        return out;
    };
    // Señal de cada entrada de video (las de GStreamer: cuadros que llegan), directa del
    // grafo de video, que es del dueño del motor como este hilo: la alarma sale con la
    // interfaz trabada. Se consulta como mucho una vez por segundo, con el turno del motor
    // y sin esperar: si la interfaz lo tiene, queda la de la vez anterior.
    directas.senales = [&rt, video = f.video, cache = std::make_shared<std::pair<qint64, std::vector<std::pair<std::string, bool>>>>(),
                  // Histéresis: la que quedó sin señal vuelve a «con señal» recién
                  // después de 10 s seguidos con cuadros. Una cámara que se corta y
                  // vuelve cada pocos segundos disparaba la alarma otra vez a cada
                  // rato (y la franja roja reaparecía apenas se la reconocía).
                  vuelta = std::make_shared<std::map<std::string, std::pair<bool, qint64>>>(),
                  desde = QDateTime::currentMSecsSinceEpoch()]() {
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        // Recién arrancada, las entradas todavía se están conectando (una cámara de red
        // tarda unos segundos): no hay alarmas de «sin señal» los primeros 15 s.
        if (ahora - desde < 15000) return std::vector<std::pair<std::string, bool>>{};
        video::VideoGrafos* v = video ? video() : nullptr;
        if (v != nullptr && ahora - cache->first > 1000 && rt.engine().turno().tomarHasta(50)) {
            const TurnoMotor& turno = rt.engine().turno();
            cache->first = ahora;
            cache->second.clear();
            for (const QString& e : v->entradas()) {
                const json st = v->estadoEntrada(e.toStdString());
                if (!st.contains("gstreamer")) continue; // sin el dato de GStreamer: sin cuadros que mirar
                const json g = st.value("gstreamer", json::object());
                if (!g.is_object()) continue;
                // Las de los invitados y el remoto: sin nadie conectado no hay cuadros, y
                // no es una falla (para eso está «invitado caído»).
                const json aj = st.value("ajustes", json::object());
                if (aj.is_object() && (aj.value("video_invitado", false) ||
                                       aj.value("pipeline", std::string()).rfind("bavideoanillo", 0) == 0))
                    continue;
                const bool activa = st.value("activa", true);
                // Una que todavía no entregó nada (una cámara de red conectándose): hasta
                // los 45 s del arranque no es «sin señal»; la que tenía y la perdió, sí.
                const bool nuncaLlego = g.value("sin_datos_s", 0.0) < 0.0;
                bool con = !activa || video::VideoGrafos::tieneSenal(st) || (nuncaLlego && ahora - desde < 45000);
                // La primera vez que se la ve no «vuelve» de nada: arranca dada por buena
                // (si no, a los 15 s del arranque todas pasaban 10 s «sin señal»).
                auto& [reportada, conDesde] =
                    vuelta->try_emplace(e.toStdString(), std::pair<bool, qint64>{true, 0}).first->second;
                if (!con) {
                    reportada = false;
                    conDesde = 0;
                } else if (!reportada && activa) {
                    if (conDesde == 0) conDesde = ahora;
                    if (ahora - conDesde < 10000) con = false; // todavía no se la da por vuelta
                    else reportada = true;
                } else {
                    reportada = true;
                }
                cache->second.emplace_back(e.toStdString(), con);
            }
            turno.soltar();
        }
        return cache->second;
    };
    // La superresolución que no llega (el programador, también del dueño).
    directas.superres = [&rt, video = f.video, cache = std::make_shared<json>(json::object())]() {
        video::VideoGrafos* v = video ? video() : nullptr;
        if (v != nullptr && rt.engine().turno().tomarHasta(50)) {
            *cache = v->programador().value("entradas", json::object());
            rt.engine().turno().soltar();
        }
        return *cache;
    };
    directas.loudness = [&motor]() { return motor.loudnessPrograma().json(); };
    // El corte de luz: medido acá (una consulta al sistema, una vez cada 5 s), no en la
    // vuelta de EnergiaAire, que es de la interfaz.
    directas.energia = [e = &f.energia, cache = std::make_shared<std::pair<qint64, json>>()]() {
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        if (cache->first == 0 || ahora - cache->first >= 5000) *cache = {ahora, e->estadoAhora()};
        return cache->second;
    };
    // El disco de todo lo que se graba (por volumen; la copia en el mismo disco). Mira
    // el espacio libre de cada carpeta: una vez cada 5 s alcanza. Lee lo que graba el
    // motor con su turno, y sin esperar: si la interfaz lo tiene (trabada ADENTRO del
    // motor), queda el de la vez anterior y se prueba en la próxima vuelta.
    // La caché arranca como objeto vacío: si la primera vuelta no consigue el turno, el
    // vigía lee {} («no se sabe»), nunca null (value() sobre null tira y tumbaba el control).
    directas.disco = [&rt, cache = std::make_shared<std::pair<qint64, json>>(0, json::object())]() {
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        if ((cache->first == 0 || ahora - cache->first >= 5000) && rt.engine().turno().tomarHasta(50)) {
            *cache = {ahora, rt.discoGrabacion()};
            rt.engine().turno().soltar();
        }
        return cache->second;
    };
    return directas;
}

agent::AlertInputs AlarmasViewModel::sinLaInterfaz() {
    agent::AlertInputs in;
    // La sala y su chat, preguntados a B (ConferenceClient::pedirSinInterfaz) cuando la
    // foto de la interfaz se quedó quieta: una vez por segundo como mucho, y la misma
    // respuesta para las dos.
    struct Sala {
        std::mutex mu;
        qint64 cuando = 0;
        QJsonObject foto;
    };
    auto sala = std::make_shared<Sala>();
    const auto leer = [sala]() {
        const std::lock_guard<std::mutex> l(sala->mu);
        const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
        if (ahora - sala->cuando >= 1000) {
            sala->cuando = ahora;
            sala->foto = ConferenceClient::pedirSinInterfaz("/api/v1/room", 800).value_or(QJsonObject());
        }
        return sala->foto;
    };
    in.room = [leer]() {
        return json::parse(QJsonDocument(leer()).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    };
    in.roomChat = [leer]() {
        QJsonArray chat;
        for (const QJsonValue& v : leer().value("chat").toArray()) chat.append(ConferenceViewModel::mensajeDelChat(v.toObject()));
        return json::parse(QJsonDocument(chat).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    };
    return in;
}

void AlarmasViewModel::refrescarFoto() { entradas_->refrescar(); }

QObject* AlarmasViewModel::sonido() const {
    // Sin padre y devuelto a QML: que no se lo quede su recolector (se borraba dos veces al
    // cerrar). Es de acá.
    QQmlEngine::setObjectOwnership(sonido_.get(), QQmlEngine::CppOwnership);
    return sonido_.get();
}

void AlarmasViewModel::releer() {
    const json a = motor_->activas();
    bool critica = false;
    for (const auto& e : a)
        if (e.value("sonido", std::string()) == "critico" && !e.value("silenciada", false)) critica = true;
    activas_ = QJsonDocument::fromJson(QByteArray::fromStdString(a.dump(-1, ' ', false, json::error_handler_t::replace)))
                   .array()
                   .toVariantList();
    critica_ = critica;
    sonido_->criticaActiva(critica_);
    emit cambio();
}

int AlarmasViewModel::reconocer(qint64 id) { return motor_->reconocer(static_cast<std::uint64_t>(id)); }

bool AlarmasViewModel::silenciar(const QString& disparador, bool si) {
    return !motor_->update(disparador.toStdString(), json{{"silenciada", si}}).contains("error");
}

void AlarmasViewModel::enHiloDelVigia(const json& ev) {
    AvisoFueraDeUi* aviso = aviso_.load();
    if (aviso == nullptr) return;
    AvisoFueraDeUi::Alarma a;
    const json& rid = ev["ruleId"];
    a.id = "alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump());
    a.titulo = "⚠ " + ev.value("ruleName", ba::i18n::traducir("alertas", "Disparador"));
    a.texto = ev.value("llmText", std::string());
    a.controlWeb = false; // el control web la recibe por el bus (enLaInterfaz)
    a.notificarTodos = false;
    for (const auto& n : ev.value("notificar", json::array()))
        if (n.is_string()) a.notificar.push_back(n.get<std::string>());
    if (a.notificar.empty()) return;
    QPointer<AlarmasViewModel> vista(this);
    a.resultado = [vista](const std::string& id, bool ok, const std::string& d) {
        if (ok || !vista) return;
        QMetaObject::invokeMethod(vista, [vista, id, d]() {
            if (vista)
                emit vista->notify(tr("El aviso por «%1» no salió: %2").arg(QString::fromStdString(id), QString::fromStdString(d)),
                                   "warn");
        });
    };
    aviso->avisar(std::move(a));
}

void AlarmasViewModel::enLaInterfaz(const json& ev) {
    if (publicar_) {
        const json& rid = ev["ruleId"];
        publicar_("alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump(-1, ' ', false, json::error_handler_t::replace)),
                  {{"nombre", ev.value("ruleName", std::string())},
                   {"tipo", ev.value("type", std::string())},
                   {"texto", ev.value("llmText", std::string())},
                   {"reconocer", ev.value("reconocer", false)},
                   {"id", ev.value("id", 0ULL)}});
    }
    for (const auto& n : ev.value("notify", json::array()))
        if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
    // Una alarma silenciada sigue en la lista (con su color), pero no suena.
    if (!ev.value("silenciada", false)) sonido_->sonar(QString::fromStdString(ev.value("sonido", std::string("ninguno"))));
}

} // namespace ba::app
