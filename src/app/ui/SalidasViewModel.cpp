// src/app/ui/SalidasViewModel.cpp — ver el .hpp

#include "ui/SalidasViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/InterfazConfig.hpp"
#include "comun/Duracion.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "emision/SalidasConfig.hpp"
#include "emision/SeguridadAire.hpp"
#include "escenas/Conmutador.hpp"
#include "i18n/I18n.hpp"
#include "ui/ConferenceViewModel.hpp" // JsonListModel
#include "ui/ListModels.hpp" // MixerModel
#include "ui/RecursosViewModel.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// La salida que maneja esta interfaz, en Config/interfaz.json ("" = la por omisión).
constexpr const char* kClaveActiva = "salida_activa";

} // namespace

SalidasViewModel::SalidasViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    // El FTB y la placa son del modelo: acá se muestran.
    seguridadAire_ = std::make_unique<emision::SeguridadAire>(p_.rt);
    seguridadAire_->alCambiar([this]() { emit placaCambio(); });
    // El conmutador de escenas es del modelo (uno por salida); acá solo se refleja lo
    // que cambia. La interfaz muestra el de la activa; las pantallas de tally de la sala y
    // el monitor «armada» siguen a la por omisión; el de cada salida llega además a la lista.
    conmutadores_ = std::make_unique<escenas::Conmutadores>(p_.rt, this);
    conmutador_ = &conmutadores_->porOmision();
    lista_ = std::make_unique<JsonListModel>(QStringList{"id", "nombre", "color", "porOmision", "alAire", "armada",
                                                         "transmitiendo", "grabando", "archivo", "destinos", "programa",
                                                         "codificador"},
                                             this);
    for (escenas::Conmutador* c : conmutadores_->todos()) seguir(c);
    connect(conmutadores_.get(), &escenas::Conmutadores::creado, this, &SalidasViewModel::seguir);
    connect(conmutadores_.get(), &escenas::Conmutadores::cambio, this, &SalidasViewModel::publicar);
    connect(conmutador_, &escenas::Conmutador::alAireCambio, this, &SalidasViewModel::alAirePorOmisionCambio);
    connect(conmutador_, &escenas::Conmutador::armadaCambio, this, &SalidasViewModel::armadaPorOmisionCambio);
    // La máquina se pone justa (o se alivia): cambia el aviso de crear otra salida.
    connect(&p_.recursos, &RecursosViewModel::cambio, this, &SalidasViewModel::revisarNueva);
}

SalidasViewModel::~SalidasViewModel() = default;

void SalidasViewModel::seguir(escenas::Conmutador* c) {
    connect(c, &escenas::Conmutador::alAireCambio, this, &SalidasViewModel::publicar);
    connect(c, &escenas::Conmutador::armadaCambio, this, &SalidasViewModel::publicar);
    connect(c, &escenas::Conmutador::alAireCambio, this, [this, c](const QString& escena) {
        if (c == &conmutadorActivo()) emit alAireActivaCambio(escena);
    });
    connect(c, &escenas::Conmutador::armadaCambio, this, [this, c](const QString& escena) {
        if (c == &conmutadorActivo()) emit armadaActivaCambio(escena);
    });
    connect(c, &escenas::Conmutador::afvCambio, this, [this, c]() {
        if (c == &conmutadorActivo()) emit afvActivaCambio();
    });
}

void SalidasViewModel::revisarNueva() {
    if (const QVariantMap n = nueva(); n != nuevaAntes_) {
        nuevaAntes_ = n;
        emit nuevaCambio();
    }
}

void SalidasViewModel::publicar() {
    BroadcastEngine& motor = p_.rt.engine();
    const nlohmann::json todo = p_.rt.salidas();
    const nlohmann::json lista = todo.value("salidas", nlohmann::json::array());
    const nlohmann::json destinos = p_.rt.destinos();
    const EngineStatus st = motor.status();
    QJsonArray filas, alGateway;
    for (const auto& s : lista) {
        const std::string id = s.value("id", std::string());
        if (!motor.existeSalida(id)) continue;
        const escenas::Conmutador* c = conmutadores_->de(id);
        QJsonArray suyos; // los destinos que salen con el programa de esta
        for (const auto& d : destinos)
            if (salidas::salidaDeDestino(d) == id)
                suyos.append(QJsonObject{{"id", qs(d.value("id", std::string()))},
                                         {"nombre", qs(d.value("nombre", std::string()))},
                                         {"activo", d.value("activo", false)},
                                         {"reconectando", d.value("reconectando", false)},
                                         {"respaldo", d.value("respaldo", false)}});
        QJsonObject f{{"id", qs(id)},
                      {"nombre", qs(s.value("nombre", id))},
                      {"color", qs(s.value("color", std::string()))},
                      {"porOmision", s.value("por_omision", false)},
                      {"alAire", qs(motor.currentScene(id))},
                      {"armada", c != nullptr ? qs(c->armada()) : QString()},
                      {"archivo", qs(motor.archivoGrabacion(id))},
                      {"destinos", suyos},
                      // La clave de FrameService de su programa (la por omisión, la de siempre).
                      {"programa", s.value("por_omision", false) ? QStringLiteral("program") : "program:" + qs(id)},
                      {"codificador", qs(s.value("codificador", std::string()))}};
        for (const auto& e : st.salidas)
            if (e.id == id) {
                f["transmitiendo"] = e.streaming || e.destinos > 0;
                f["grabando"] = e.recording;
            }
        alGateway.append(QJsonObject{{"id", f["id"]}, {"nombre", f["nombre"]}, {"color", f["color"]}, {"porOmision", f["porOmision"]}});
        filas.append(f);
    }
    const int tope = todo.value("tope", 0);
    const bool cambiaron = filas != lista_->items() || tope != tope_;
    tope_ = tope;
    if (filas != lista_->items()) lista_->setItems(filas);
    // Las claves «program:<id>», sus nombres y sus colores (el monitor); el gateway sabe
    // qué salidas hay para que la sesión remota y el cast elijan; el control web.
    emit listaCambio(alGateway);
    // La activa se borró: la interfaz vuelve a la por omisión.
    if (!activa_.empty() && !motor.existeSalida(activa_)) {
        activa_.clear();
        p_.interfaz.guardar(kClaveActiva, activa_);
        emit activaElegida();
        emit cambio();
        emit placaCambio();
    } else if (cambiaron) {
        emit cambio();
    }
    revisarNueva();
}

void SalidasViewModel::retomarActiva() {
    if (const nlohmann::json v = p_.interfaz.valor(kClaveActiva); v.is_string() && !v.get<std::string>().empty())
        setActiva(qs(v.get<std::string>()));
}

void SalidasViewModel::alTic(const EngineStatus& st) {
    // Las demás salidas: qué sale de cada una y desde cuándo (los relojes de aire, que no
    // se reinician al cambiar de activa).
    std::map<std::string, AireDeSalida> ahora;
    const auto t = std::chrono::steady_clock::now();
    for (const auto& e : st.salidas) {
        if (e.porOmision) continue;
        const auto antes = aireSalidas_.find(e.id);
        AireDeSalida a = antes != aireSalidas_.end() ? antes->second : AireDeSalida{};
        const bool transmite = e.streaming || e.destinos > 0;
        if (transmite && !a.transmite) a.transmiteDesde = t;
        if (e.recording && !a.graba) a.grabaDesde = t;
        a.transmite = transmite;
        a.graba = e.recording;
        a.reconecta = e.reconnecting;
        ahora[e.id] = a;
    }
    aireSalidas_ = std::move(ahora);
}

void SalidasViewModel::publicarEnBus(control::BusEventos& bus, MixerModel& tiras, control::Dsk& dsk, bool transmitiendo,
                                     bool grabando) {
    BroadcastEngine& motor = p_.rt.engine();
    // Lo del BusEventos es de la por omisión (los temas por salida son de la etapa 4).
    const QString aire = qs(motor.currentScene());
    const QString armada = qs(conmutador_->armada());
    const auto fuentesDe = [&motor](const QString& escena) {
        nlohmann::json out = nlohmann::json::array();
        if (!escena.isEmpty())
            for (const std::string& f : motor.sceneSources(escena.toStdString())) out.push_back(f);
        return out;
    };
    bus.publicar("aire/escena", {{"escena", aire.toStdString()}});
    bus.publicar("aire/armada", {{"escena", armada.toStdString()}});
    nlohmann::json fuentesAire = fuentesDe(aire);
    for (const std::string& f : motor.fuentesDsk())
        if (std::find(fuentesAire.begin(), fuentesAire.end(), f) == fuentesAire.end()) fuentesAire.push_back(f);
    {
        // Para el multiview: el tally de la salida activa (rojo al aire, verde armada) y, con
        // varias salidas, en cuáles está al aire cada fuente (las demás con su color).
        const auto alAireEn = [&motor](const std::string& salida) {
            QStringList out;
            const std::string escena = motor.currentScene(salida);
            if (!escena.empty())
                for (const std::string& f : motor.sceneSources(escena)) out << qs(f);
            for (const std::string& f : motor.fuentesDsk(salida))
                if (!out.contains(qs(f))) out << qs(f);
            return out;
        };
        const QStringList aireQ = alAireEn(activa_);
        QStringList armadaQ;
        for (const auto& f : fuentesDe(qs(conmutadorActivo().armada()))) armadaQ << QString::fromStdString(f.get<std::string>());
        QVariantMap borde, enSalidas;
        for (const QString& f : armadaQ) borde[f] = QStringLiteral("#2ecc71");
        for (const QString& f : aireQ) borde[f] = QStringLiteral("#e53935");
        if (varias())
            for (const auto& v : lista_->items()) {
                const QJsonObject o = v.toObject();
                const QVariantMap marca{{"nombre", o.value("nombre").toString()}, {"color", o.value("color").toString()}};
                for (const QString& f : alAireEn(o.value("id").toString().toStdString())) {
                    enSalidas[f] = enSalidas.value(f).toList() << marca;
                    if (!borde.contains(f)) borde[f] = marca.value("color");
                }
            }
        const QVariantMap t{{"aire", aireQ}, {"armada", armadaQ}, {"borde", borde}, {"salidas", enSalidas}};
        if (t != tally_) {
            tally_ = t;
            emit tallyCambio();
        }
    }
    // Lo que se reproduce al aire en una salida (con las fuentes que muestra) y cuánto le
    // queda, al segundo: [{fuente, restante_s, duracion_s}].
    const std::vector<BroadcastEngine::MediaInfo> sonando = motor.mediaSources();
    const auto mediosEn = [&sonando](const nlohmann::json& fuentes) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& m : sonando) {
            if (m.state != "playing" || m.durationMs <= 0) continue;
            if (std::find(fuentes.begin(), fuentes.end(), m.name) == fuentes.end()) continue;
            out.push_back({{"fuente", m.name},
                           {"restante_s", static_cast<int>((m.durationMs - m.timeMs + 999) / 1000)},
                           {"duracion_s", static_cast<int>(m.durationMs / 1000)}});
        }
        return out;
    };
    {
        const nlohmann::json mediosJ = mediosEn(fuentesAire);
        QVariantList medios;
        for (const auto& m : mediosJ)
            medios.append(QVariantMap{{"fuente", qs(m.value("fuente", std::string()))},
                                      {"restante_s", m.value("restante_s", 0)},
                                      {"duracion_s", m.value("duracion_s", 0)}});
        if (medios != mediosAlAire_) {
            mediosAlAire_ = medios;
            emit mediosCambio();
            bus.publicar("aire/medios", mediosJ);
        }
    }
    bus.publicar("aire/tbar", {{"hacia", motor.manualHacia()}, {"posicion", conmutador_->tbarPosicion()}});
    bus.publicar("aire/tally", {{"programa", aire.toStdString()},
                                {"preview", armada.toStdString()},
                                {"fuentes_programa", fuentesAire},
                                {"fuentes_preview", fuentesDe(armada)},
                                {"transmitiendo", transmitiendo},
                                {"grabando", grabando}});
    // Lo mismo de cada salida (aire/<id>/…, salida/<id>/…): las superficies que
    // manejan otra salida leen los suyos (PLAN-SALIDAS, etapa 4).
    nlohmann::json ss = nlohmann::json::array();
    for (const QJsonValue& v : lista_->items()) {
        const QJsonObject f = v.toObject();
        const std::string id = f.value("id").toString().toStdString();
        const std::string esc = f.value("alAire").toString().toStdString();
        const QString arm = f.value("armada").toString();
        nlohmann::json fa = fuentesDe(qs(esc));
        for (const std::string& d : motor.fuentesDsk(id))
            if (std::find(fa.begin(), fa.end(), d) == fa.end()) fa.push_back(d);
        const escenas::Conmutador* c = conmutadores_->de(id);
        const bool porOmision = f.value("porOmision").toBool();
        // El fader y el mudo de cada fuente en su mezcla (los de la por omisión ya van
        // en audio/nivel/<fuente>).
        nlohmann::json niveles = nlohmann::json::object();
        if (!porOmision)
            for (int i = 0; i < tiras.rowCount(); ++i)
                niveles[tiras.data(tiras.index(i), MixerModel::NameRole).toString().toStdString()] = tiras.nivelEn(i, id);
        ss.push_back({{"id", id},
                      {"medios", mediosEn(fa)},
                      {"dsk", dsk.capas(id)},
                      {"tbar", {{"hacia", motor.manualHacia(id)}, {"posicion", c != nullptr ? c->tbarPosicion() : 0.0}}},
                      {"niveles", niveles},
                      {"nombre", f.value("nombre").toString().toStdString()},
                      {"color", f.value("color").toString().toStdString()},
                      {"por_omision", porOmision},
                      {"escena", esc},
                      {"armada", arm.toStdString()},
                      {"fuentes_programa", fa},
                      {"fuentes_preview", fuentesDe(arm)},
                      {"transmitiendo", f.value("transmitiendo").toBool()},
                      {"grabando", f.value("grabando").toBool()}});
    }
    bus.publicarSalidas(ss);
}

QObject* SalidasViewModel::lista() const { return lista_.get(); }
const QJsonArray& SalidasViewModel::filas() const { return lista_->items(); }

escenas::Conmutador& SalidasViewModel::conmutadorActivo() const {
    escenas::Conmutador* c = activa_.empty() ? nullptr : conmutadores_->de(activa_);
    return c != nullptr ? *c : *conmutador_;
}

QString SalidasViewModel::activa() const {
    return qs(activa_.empty() ? std::string(BroadcastEngine::kSalidaPorOmision) : activa_);
}

void SalidasViewModel::setActiva(const QString& id) {
    // La por omisión se anota "" (todo lo que pregunta «¿es otra?» es un empty()).
    std::string nueva = p_.rt.engine().idDeSalida(id.toStdString());
    if (nueva.empty()) return; // no existe
    if (nueva == BroadcastEngine::kSalidaPorOmision) nueva.clear();
    if (nueva == activa_) return;
    activa_ = nueva;
    // Vuelve con ella al arrancar (Config/interfaz.json: es de esta interfaz, no del aire).
    p_.interfaz.guardar(kClaveActiva, activa_);
    emit activaElegida();
    emit cambio();
    emit placaCambio();
}

QVariantMap SalidasViewModel::activaDatos() const {
    const QString id = activa();
    for (const auto& v : lista_->items())
        if (v.toObject().value("id").toString() == id) return v.toObject().toVariantMap();
    return {};
}

bool SalidasViewModel::varias() const { return lista_->count() > 1; }

QString SalidasViewModel::claveProgramaActiva() const {
    return activa_.empty() ? QStringLiteral("program") : "program:" + qs(activa_);
}

QString SalidasViewModel::claveArmadaActiva() const {
    return activa_.empty() ? QStringLiteral("armada") : "armada:" + qs(activa_);
}

QVariantMap SalidasViewModel::nueva() const {
    // El tope lo pone el operador; que la máquina dé abasto lo dicen los recursos (la
    // tarjeta Sistema, sistema_recursos): justa, se puede igual pero se avisa.
    if (lista_->count() >= tope_) return {{"puede", false}, {"aviso", tr("Ya hay %n salida, que es el tope.", "", tope_)}};
    const QString estado = p_.recursos.estado();
    if (estado == "critico")
        return {{"puede", true},
                {"aviso", tr("La máquina está al límite (%1): otra salida es otro programa, otro preview y otra mezcla.")
                              .arg(p_.recursos.limita())}};
    if (estado == "aviso")
        return {{"puede", true}, {"aviso", tr("La máquina está justa (%1): mirá la tarjeta Sistema antes de sumar otra.").arg(p_.recursos.limita())}};
    return {{"puede", true}, {"aviso", QString()}};
}

QVariantMap SalidasViewModel::aire() const {
    const bool alAireActiva = p_.rt.engine().haySalidaEn(activa_);
    if (activa_.empty()) {
        QVariantMap m = p_.airePorOmision ? p_.airePorOmision() : QVariantMap{};
        m["alAire"] = alAireActiva;
        return m;
    }
    const auto it = aireSalidas_.find(activa_);
    const AireDeSalida a = it != aireSalidas_.end() ? it->second : AireDeSalida{};
    return {{"transmitiendo", a.transmite},
            {"grabando", a.graba},
            {"reconectando", a.reconecta},
            {"alAire", alAireActiva},
            {"tiempoTransmision", a.transmite ? hmsDesde(a.transmiteDesde) : QStringLiteral("00:00:00")},
            {"tiempoGrabacion", a.graba ? hmsDesde(a.grabaDesde) : QStringLiteral("00:00:00")}};
}

QVariantMap SalidasViewModel::placa() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(seguridadAire_->estado(activa_).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

nlohmann::json SalidasViewModel::paraSuperficies() const {
    nlohmann::json out = nlohmann::json::array();
    for (const QJsonValue& v : lista_->items()) {
        const QJsonObject f = v.toObject();
        out.push_back({{"id", f.value("id").toString().toStdString()},
                       {"nombre", f.value("nombre").toString().toStdString()},
                       {"color", f.value("color").toString().toStdString()},
                       {"por_omision", f.value("porOmision").toBool()}});
    }
    return out;
}

QString SalidasViewModel::crear(const QString& nombre, const QString& color) {
    // El id sale del nombre (letras y números, sin acentos: va en los nombres de los
    // canales y los archivos) y no se repite; sin nada útil, «salida-<n>».
    QString base;
    for (const QChar ch : nombre.normalized(QString::NormalizationForm_D).toLower()) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) base += ch;
        else if ((ch.isSpace() || ch == '-' || ch == '_') && !base.isEmpty() && !base.endsWith('-')) base += '-';
    }
    while (base.endsWith('-')) base.chop(1);
    if (base.isEmpty()) base = QStringLiteral("salida");
    QString id = base;
    for (int n = 2; p_.rt.engine().existeSalida(id.toStdString()); ++n) id = QString("%1-%2").arg(base).arg(n);
    const std::string e = p_.rt.crearSalida(id.toStdString(), nombre.trimmed().toStdString(), color.toStdString());
    if (!e.empty()) return qs(e);
    publicar();
    setActiva(id);
    return {};
}

QString SalidasViewModel::setTope(int tope) {
    const std::string e = p_.rt.setTopeSalidas(tope);
    publicar();
    return qs(e);
}

QString SalidasViewModel::grabar(const QString& id, bool grabar) {
    const std::string s = p_.rt.engine().idDeSalida(id.toStdString());
    if (s.empty()) return tr("No hay una salida «%1».").arg(id);
    if (!grabar) {
        p_.rt.stopRecording(s);
        return {};
    }
    return p_.rt.startRecording(s) ? QString() : qs(p_.rt.lastError());
}

QString SalidasViewModel::crearSalida(const QString& id, const QString& nombre, const QString& color) {
    return qs(p_.rt.crearSalida(id.toStdString(), nombre.toStdString(), color.toStdString()));
}

QString SalidasViewModel::editar(const QString& id, const QString& nombre, const QString& color) {
    nlohmann::json cambios = nlohmann::json::object();
    if (!nombre.isEmpty()) cambios["nombre"] = nombre.toStdString();
    if (!color.isEmpty()) cambios["color"] = color.toStdString();
    return qs(p_.rt.editarSalida(id.toStdString(), cambios));
}

QString SalidasViewModel::borrar(const QString& id, const QString& susDestinos) {
    return qs(p_.rt.borrarSalida(id.toStdString(), susDestinos.toStdString()));
}

QString SalidasViewModel::setCodificador(const QString& id, const QString& codificador) {
    return qs(p_.rt.editarSalida(id.toStdString(), {{"codificador", codificador.toStdString()}}));
}

QString SalidasViewModel::ftbUi(bool activar, int ms, const QString& audio) {
    // placaCambio llega por alCambiar
    return qs(seguridadAire_->ftb(activar, ms, audio.toStdString(), activa_));
}

QString SalidasViewModel::placaUi(bool activar, const QString& texto, const QString& imagen, int ms) {
    return qs(seguridadAire_->placa(activar, texto.toStdString(), imagen.toStdString(), ms, activa_));
}

QStringList SalidasViewModel::textosPlaca() const {
    // Salen al aire: en el idioma del evento (se arman cada vez que se piden, así que un
    // cambio de idioma se ve en la lista siguiente; una placa ya al aire queda como salió).
    const auto ev = [](const std::string& s) { return QString::fromStdString(s); };
    QStringList l{ev(ba::i18n::traducirEvento("SalidasViewModel", "Volvemos enseguida")),
                  ev(ba::i18n::traducirEvento("SalidasViewModel", "Cuarto intermedio")),
                  ev(ba::i18n::traducirEvento("SalidasViewModel", "Minuto de silencio")),
                  ev(ba::i18n::traducirEvento("SalidasViewModel", "Sesión suspendida")),
                  ev(ba::i18n::traducirEvento("SalidasViewModel", "Dificultades técnicas"))};
    const QString usado = QString::fromStdString(p_.rt.engine().estadoPlaca().value("placa_texto", std::string()));
    if (!usado.isEmpty() && !l.contains(usado)) l.prepend(usado);
    return l;
}

} // namespace ba::app
