// src/app/ui/SalidasViewModel.cpp — ver el .hpp

#include "ui/SalidasViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/InterfazConfig.hpp"
#include "comun/Duracion.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "emision/SalidasConfig.hpp"
#include "emision/SeguridadAire.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "i18n/I18n.hpp"
#include "ui/ConferenceViewModel.hpp" // JsonListModel
#include "ui/ListModels.hpp" // MixerModel
#include "ui/RecursosViewModel.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// La salida que maneja esta interfaz, en Config/interfaz.json ("" = la por omisión).
constexpr const char* kClaveActiva = "salida_activa";

QVariantMap mapaDe(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

} // namespace

SalidasViewModel::SalidasViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    // El FTB y la placa son del modelo (el dueño los usa con el turno): acá se muestran de
    // la foto (la placa de cada salida).
    seguridadAire_ = std::make_unique<emision::SeguridadAire>(p_.rt);
    // El conmutador de escenas es del modelo (uno por salida) y vive en el dueño del motor:
    // se crea acá y se muda (sin padre: lo borra este área cuando vuelve, al cerrar). Lo que
    // muestra (la escena al aire, la armada, el AFV, la T-bar) llega en la foto.
    conmutadores_ = std::make_unique<escenas::Conmutadores>(p_.rt);
    conmutador_ = &conmutadores_->porOmision();
    p_.ventanilla.adoptar(conmutadores_.get());
    lista_ = std::make_unique<JsonListModel>(QStringList{"id", "nombre", "color", "porOmision", "alAire", "armada",
                                                         "transmitiendo", "grabando", "archivo", "destinos", "programa",
                                                         "codificador"},
                                             this);
    connect(&p_.ventanilla, &VentanillaMotor::cambio, this, &SalidasViewModel::alCambiar);
    // La máquina se pone justa (o se alivia): cambia el aviso de crear otra salida.
    connect(&p_.recursos, &RecursosViewModel::cambio, this, &SalidasViewModel::revisarNueva);
    // Lo de la activa y la por omisión, como está ahora (para avisar solo lo que cambie).
    const FotoMotor& f = p_.ventanilla.foto();
    if (const FotoMotor::Salida* s = f.salida({})) {
        alAireActivaAntes_ = alAirePorOmisionAntes_ = s->alAire;
        armadaActivaAntes_ = armadaPorOmisionAntes_ = s->armada;
        afvActivaAntes_ = s->afv;
        placaAntes_ = s->placa;
    }
}

SalidasViewModel::~SalidasViewModel() = default;

void SalidasViewModel::revisarNueva() {
    if (const QVariantMap n = nueva(); n != nuevaAntes_) {
        nuevaAntes_ = n;
        emit nuevaCambio();
    }
}

bool SalidasViewModel::rehacerFilas(QJsonArray* alGateway) {
    const FotoMotor& f = p_.ventanilla.foto();
    QJsonArray filas;
    for (const FotoMotor::Salida& s : f.salidas) {
        QJsonArray suyos; // los destinos que salen con el programa de esta
        for (const auto& d : s.destinos)
            suyos.append(QJsonObject{{"id", qs(d.value("id", std::string()))},
                                     {"nombre", qs(d.value("nombre", std::string()))},
                                     {"activo", d.value("activo", false)},
                                     {"reconectando", d.value("reconectando", false)},
                                     {"respaldo", d.value("respaldo", false)}});
        const QJsonObject fila{{"id", qs(s.id)},
                               {"nombre", qs(s.nombre)},
                               {"color", qs(s.color)},
                               {"porOmision", s.porOmision},
                               {"alAire", qs(s.alAire)},
                               {"armada", qs(s.armada)},
                               {"archivo", qs(s.archivo)},
                               {"destinos", suyos},
                               // La clave de FrameService de su programa (la por omisión, la de siempre).
                               {"programa", s.porOmision ? QStringLiteral("program") : "program:" + qs(s.id)},
                               {"codificador", qs(s.codificador)},
                               {"transmitiendo", s.transmitiendo},
                               {"grabando", s.grabando}};
        if (alGateway != nullptr)
            alGateway->append(QJsonObject{{"id", fila["id"]}, {"nombre", fila["nombre"]}, {"color", fila["color"]}, {"porOmision", fila["porOmision"]}});
        filas.append(fila);
    }
    const bool cambiaron = filas != lista_->items() || f.tope != tope_;
    tope_ = f.tope;
    if (filas != lista_->items()) lista_->setItems(filas);
    return cambiaron;
}

void SalidasViewModel::publicar() {
    QJsonArray alGateway;
    const bool cambiaron = rehacerFilas(&alGateway);
    // Las claves «program:<id>», sus nombres y sus colores (el monitor); el gateway sabe
    // qué salidas hay para que la sesión remota y el cast elijan; el control web.
    alGatewayAntes_ = alGateway;
    emit listaCambio(alGateway);
    if (cambiaron) emit cambio();
    revisarNueva();
}

void SalidasViewModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Salidas) == 0) return;
    const FotoMotor& f = p_.ventanilla.foto();
    QJsonArray alGateway;
    const bool cambiaron = rehacerFilas(&alGateway);
    if (alGateway != alGatewayAntes_) {
        alGatewayAntes_ = alGateway;
        emit listaCambio(alGateway);
    }
    // La activa se borró: la interfaz vuelve a la por omisión.
    if (!activa_.empty() && f.salida(activa_) == nullptr) {
        activa_.clear();
        p_.interfaz.guardar(kClaveActiva, activa_);
        emit activaElegida();
        emit cambio();
    } else if (cambiaron) {
        emit cambio();
    }
    revisarNueva();
    // Las demás salidas: qué sale de cada una y desde cuándo (los relojes de aire, que no se
    // reinician al cambiar de activa).
    std::map<std::string, AireDeSalida> ahora;
    const auto t = std::chrono::steady_clock::now();
    for (const FotoMotor::Salida& s : f.salidas) {
        if (s.porOmision) continue;
        const auto antes = aireSalidas_.find(s.id);
        AireDeSalida a = antes != aireSalidas_.end() ? antes->second : AireDeSalida{};
        if (s.transmitiendo && !a.transmite) a.transmiteDesde = t;
        if (s.grabando && !a.graba) a.grabaDesde = t;
        a.transmite = s.transmitiendo;
        a.graba = s.grabando;
        a.reconecta = s.reconectando;
        ahora[s.id] = a;
    }
    aireSalidas_ = std::move(ahora);
    // Lo de la activa (escenas, fuentes, el AFV, la placa) y lo de la por omisión (las
    // pantallas de tally de la sala, el monitor «armada»): solo lo que cambió.
    static const FotoMotor::Salida kNinguna;
    const FotoMotor::Salida* activa = f.salida(activa_);
    const FotoMotor::Salida& a = activa != nullptr ? *activa : kNinguna;
    const FotoMotor::Salida* omision = f.salida({});
    const FotoMotor::Salida& o = omision != nullptr ? *omision : kNinguna;
    if (a.alAire != alAireActivaAntes_) emit alAireActivaCambio(qs(alAireActivaAntes_ = a.alAire));
    if (a.armada != armadaActivaAntes_) emit armadaActivaCambio(qs(armadaActivaAntes_ = a.armada));
    if (a.afv != afvActivaAntes_) {
        afvActivaAntes_ = a.afv;
        emit afvActivaCambio();
    }
    if (a.placa != placaAntes_) {
        placaAntes_ = a.placa;
        emit placaCambio();
    }
    if (o.alAire != alAirePorOmisionAntes_) {
        alAirePorOmisionAntes_ = o.alAire;
        emit alAirePorOmisionCambio();
    }
    if (o.armada != armadaPorOmisionAntes_) emit armadaPorOmisionCambio(qs(armadaPorOmisionAntes_ = o.armada));
}

void SalidasViewModel::retomarActiva() {
    if (const nlohmann::json v = p_.interfaz.valor(kClaveActiva); v.is_string() && !v.get<std::string>().empty())
        setActiva(qs(v.get<std::string>()));
}

void SalidasViewModel::publicarEnBus(control::BusEventos& bus, MixerModel& tiras, control::Dsk& dsk, bool transmitiendo,
                                     bool grabando) {
    const FotoMotor& foto = p_.ventanilla.foto();
    static const FotoMotor::Salida kNinguna;
    const FotoMotor::Salida* po = foto.salida({});
    const FotoMotor::Salida& omision = po != nullptr ? *po : kNinguna;
    // Lo del BusEventos es de la por omisión (los temas por salida son de la etapa 4).
    const QString aire = qs(omision.alAire);
    const QString armada = qs(omision.armada);
    const auto fuentesDe = [&foto](const QString& escena) {
        nlohmann::json out = nlohmann::json::array();
        if (!escena.isEmpty())
            for (const std::string& f : foto.fuentesDe(escena.toStdString())) out.push_back(f);
        return out;
    };
    bus.publicar("aire/escena", {{"escena", aire.toStdString()}});
    bus.publicar("aire/armada", {{"escena", armada.toStdString()}});
    nlohmann::json fuentesAire = fuentesDe(aire);
    for (const std::string& f : omision.dsk)
        if (std::find(fuentesAire.begin(), fuentesAire.end(), f) == fuentesAire.end()) fuentesAire.push_back(f);
    {
        // Para el multiview: el tally de la salida activa (rojo al aire, verde armada) y, con
        // varias salidas, en cuáles está al aire cada fuente (las demás con su color).
        const auto alAireEn = [&foto](const std::string& salida) {
            QStringList out;
            const FotoMotor::Salida* s = foto.salida(salida);
            if (s == nullptr) return out;
            for (const std::string& f : foto.fuentesDe(s->alAire)) out << qs(f);
            for (const std::string& f : s->dsk)
                if (!out.contains(qs(f))) out << qs(f);
            return out;
        };
        const QStringList aireQ = alAireEn(activa_);
        QStringList armadaQ;
        if (const FotoMotor::Salida* s = foto.salida(activa_))
            for (const std::string& f : foto.fuentesDe(s->armada)) armadaQ << qs(f);
        QVariantMap borde, enSalidas;
        for (const QString& f : armadaQ) borde[f] = QStringLiteral("#2ecc71");
        for (const QString& f : aireQ) borde[f] = QStringLiteral("#e53935");
        if (varias())
            for (const FotoMotor::Salida& s : foto.salidas) {
                const QVariantMap marca{{"nombre", qs(s.nombre)}, {"color", qs(s.color)}};
                for (const QString& f : alAireEn(s.id)) {
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
    const auto mediosEn = [&foto](const nlohmann::json& fuentes) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& m : foto.medios) {
            const std::int64_t dur = m.value("duracion_ms", std::int64_t{0}), t = m.value("tiempo_ms", std::int64_t{0});
            if (m.value("estado", std::string()) != "playing" || dur <= 0) continue;
            if (std::find(fuentes.begin(), fuentes.end(), m.value("fuente", std::string())) == fuentes.end()) continue;
            out.push_back({{"fuente", m.value("fuente", std::string())},
                           {"restante_s", static_cast<int>((dur - t + 999) / 1000)},
                           {"duracion_s", static_cast<int>(dur / 1000)}});
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
    bus.publicar("aire/tbar", {{"hacia", omision.manualHacia}, {"posicion", omision.tbar}});
    bus.publicar("aire/tally", {{"programa", aire.toStdString()},
                                {"preview", armada.toStdString()},
                                {"fuentes_programa", fuentesAire},
                                {"fuentes_preview", fuentesDe(armada)},
                                {"transmitiendo", transmitiendo},
                                {"grabando", grabando}});
    // Lo mismo de cada salida (aire/<id>/…, salida/<id>/…): las superficies que
    // manejan otra salida leen los suyos (PLAN-SALIDAS, etapa 4).
    nlohmann::json ss = nlohmann::json::array();
    for (const FotoMotor::Salida& s : foto.salidas) {
        nlohmann::json fa = fuentesDe(qs(s.alAire));
        for (const std::string& d : s.dsk)
            if (std::find(fa.begin(), fa.end(), d) == fa.end()) fa.push_back(d);
        // El fader y el mudo de cada fuente en su mezcla (los de la por omisión ya van
        // en audio/nivel/<fuente>).
        nlohmann::json niveles = nlohmann::json::object();
        if (!s.porOmision)
            for (int i = 0; i < tiras.rowCount(); ++i)
                niveles[tiras.data(tiras.index(i), MixerModel::NameRole).toString().toStdString()] = tiras.nivelEn(i, s.id);
        ss.push_back({{"id", s.id},
                      {"medios", mediosEn(fa)},
                      {"dsk", dsk.capas(s.id)},
                      {"tbar", {{"hacia", s.manualHacia}, {"posicion", s.tbar}}},
                      {"niveles", niveles},
                      {"nombre", s.nombre},
                      {"color", s.color},
                      {"por_omision", s.porOmision},
                      {"escena", s.alAire},
                      {"armada", s.armada},
                      {"fuentes_programa", fa},
                      {"fuentes_preview", fuentesDe(qs(s.armada))},
                      {"transmitiendo", s.transmitiendo},
                      {"grabando", s.grabando}});
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
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(id.toStdString());
    if (s == nullptr) return; // no existe
    const std::string nueva = s->porOmision ? std::string() : s->id;
    if (nueva == activa_) return;
    activa_ = nueva;
    // Vuelve con ella al arrancar (Config/interfaz.json: es de esta interfaz, no del aire).
    p_.interfaz.guardar(kClaveActiva, activa_);
    alAireActivaAntes_ = s->alAire;
    armadaActivaAntes_ = s->armada;
    afvActivaAntes_ = s->afv;
    placaAntes_ = s->placa;
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
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(activa_);
    const bool alAireActiva = s != nullptr && s->sale;
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
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(activa_);
    return s != nullptr ? mapaDe(s->placa) : QVariantMap{};
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

void SalidasViewModel::pedir(const char* que, std::function<std::string(AppRuntime&)> orden, std::function<void()> alVolver) {
    p_.ventanilla.pedir(
        this, que, [&rt = p_.rt, orden = std::move(orden)]() { return orden(rt); },
        [this, alVolver = std::move(alVolver)](const std::string& e) {
            if (!e.empty()) return emit notify(qs(e), "error");
            if (alVolver) alVolver();
        });
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
    for (int n = 2; p_.ventanilla.foto().salida(id.toStdString()) != nullptr; ++n) id = QString("%1-%2").arg(base).arg(n);
    // Al volver (ya en la foto), queda como la activa.
    pedir(
        "salida_crear",
        [i = id.toStdString(), n = nombre.trimmed().toStdString(), c = color.toStdString()](AppRuntime& rt) {
            return rt.crearSalida(i, n, c);
        },
        [this, id]() { setActiva(id); });
    return {};
}

QString SalidasViewModel::setTope(int tope) {
    pedir("salidas_tope", [tope](AppRuntime& rt) { return rt.setTopeSalidas(tope); });
    return {};
}

QString SalidasViewModel::grabar(const QString& id, bool grabar) {
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(id.toStdString());
    if (s == nullptr) return tr("No hay una salida «%1».").arg(id);
    pedir("salida_grabar", [salida = s->id, grabar](AppRuntime& rt) {
        if (!grabar) {
            rt.stopRecording(salida);
            return std::string();
        }
        return rt.startRecording(salida) ? std::string() : rt.lastError();
    });
    return {};
}

QString SalidasViewModel::crearSalida(const QString& id, const QString& nombre, const QString& color) {
    pedir("salida_crear", [i = id.toStdString(), n = nombre.toStdString(), c = color.toStdString()](AppRuntime& rt) {
        return rt.crearSalida(i, n, c);
    });
    return {};
}

QString SalidasViewModel::editar(const QString& id, const QString& nombre, const QString& color) {
    nlohmann::json cambios = nlohmann::json::object();
    if (!nombre.isEmpty()) cambios["nombre"] = nombre.toStdString();
    if (!color.isEmpty()) cambios["color"] = color.toStdString();
    pedir("salida_editar", [i = id.toStdString(), cambios](AppRuntime& rt) { return rt.editarSalida(i, cambios); });
    return {};
}

QString SalidasViewModel::borrar(const QString& id, const QString& susDestinos) {
    pedir("salida_borrar", [i = id.toStdString(), d = susDestinos.toStdString()](AppRuntime& rt) { return rt.borrarSalida(i, d); });
    return {};
}

QString SalidasViewModel::setCodificador(const QString& id, const QString& codificador) {
    pedir("salida_codificador", [i = id.toStdString(), c = codificador.toStdString()](AppRuntime& rt) {
        return rt.editarSalida(i, {{"codificador", c}});
    });
    return {};
}

QString SalidasViewModel::ftbUi(bool activar, int ms, const QString& audio) {
    // placaCambio llega con la foto (la placa de la activa).
    pedir("aire_ftb", [&sa = *seguridadAire_, activar, ms, a = audio.toStdString(), salida = activa_](AppRuntime&) {
        return sa.ftb(activar, ms, a, salida);
    });
    return {};
}

QString SalidasViewModel::placaUi(bool activar, const QString& texto, const QString& imagen, int ms) {
    pedir("aire_placa",
          [&sa = *seguridadAire_, activar, t = texto.toStdString(), i = imagen.toStdString(), ms, salida = activa_](AppRuntime&) {
              return sa.placa(activar, t, i, ms, salida);
          });
    return {};
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
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida({});
    const QString usado = s != nullptr ? QString::fromStdString(s->placa.value("placa_texto", std::string())) : QString();
    if (!usado.isEmpty() && !l.contains(usado)) l.prepend(usado);
    return l;
}

} // namespace ba::app
