// ============================================================================
// src/app/ui/EventoViewModel.cpp — ver EventoViewModel.hpp
// ============================================================================
#include "ui/EventoViewModel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include "AppRuntime.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "BroadcastEngine.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;

int64_t ahoraMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

QVariant deJson(const json& j) {
    if (j.is_string()) return QString::fromStdString(j.get<std::string>());
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return static_cast<int>(j.get<int64_t>());
    if (j.is_number_float()) return j.get<double>();
    if (j.is_object()) {
        QVariantMap m;
        for (auto it = j.begin(); it != j.end(); ++it)
            m[QString::fromStdString(it.key())] = deJson(it.value());
        return m;
    }
    if (j.is_array()) {
        QVariantList l;
        for (const json& x : j)
            l.append(deJson(x));
        return l;
    }
    return {};
}

json aJson(const QVariant& v) {
    switch (static_cast<QMetaType::Type>(v.typeId())) {
        case QMetaType::Bool: return v.toBool();
        case QMetaType::Int:
        case QMetaType::LongLong: return v.toLongLong();
        case QMetaType::Double: return v.toDouble();
        case QMetaType::QVariantMap: {
            json o = json::object();
            const QVariantMap m = v.toMap();
            for (auto it = m.begin(); it != m.end(); ++it)
                o[it.key().toStdString()] = aJson(it.value());
            return o;
        }
        case QMetaType::QVariantList: {
            json a = json::array();
            for (const QVariant& x : v.toList())
                a.push_back(aJson(x));
            return a;
        }
        default: return v.toString().toStdString();
    }
}

/// Un id corto y unico dentro del guion. No se muestra: sirve para que la
/// interfaz pueda seguir a un cue cuando se lo mueve de lugar.
std::string nuevoId(const char* prefijo) {
    static int n = 0;
    return std::string(prefijo) + std::to_string(++n) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
}

/// Lo que cada tipo de cue necesita, para armar el formulario.
struct CampoDeTipo {
    const char* tipo;
    const char* titulo;
    const char* ayuda;
    const char* campos; ///< claves de `datos`, separadas por coma
};

constexpr CampoDeTipo kTipos[] = {
    {"escena", "Escena", "Pone una escena al aire.", "escena"},
    {"musica", "Música", "Arranca o para un tema de Medios/.", "archivo,accion,repetir"},
    {"video", "Video", "Reproduce un video de Medios/ en su propia escena.",
     "archivo,accion,repetir,alAire"},
    {"teleprompter", "Teleprompter", "Carga un discurso y lo deja listo.", "discurso,arrancar"},
    {"orador", "Orador", "Le da la palabra a alguien, con su tiempo.", "participante"},
    {"grafico", "Gráfico", "Saca al aire un gráfico animado en una fuente de escena.",
     "fuente,grafico,accion"},
    {"pantalla", "Pantalla", "Manda contenido a una pantalla.", "pantalla,contenido"},
    {"audio", "Audio", "Silencia una fuente, la activa o le cambia el volumen.", "accion,fuente,db"},
    {"pausa", "Pausa", "Espera los segundos que diga la duración.", ""},
    {"espera", "Espera al operador", "Frena el guion hasta que confirmes.", ""},
    {"nota", "Nota", "No hace nada: es una indicación para vos.", ""},
};

} // namespace

EventoViewModel::EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala,
                                 graficos::Emisor* graficos, bool sinAbrirUltimo, QObject* parent)
    : QObject(parent), rt_(rt), sala_(sala), graficos_(graficos),
      store_(rt.dataRoot() / "Eventos") {
    // El reloj del evento va aparte del guion: cambia cada segundo y no tiene
    // sentido redibujar la lista de cues por eso.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() { emit relojChanged(); });
    // Se abre solo el evento que se guardo ultimo: el operador trabaja en el
    // mismo durante dias, y abrir el programa para encontrarlo vacio es una
    // molestia diaria. No hace falta guardar "el ultimo abierto" en ningun
    // lado: la fecha del archivo ya lo dice.
    const std::string ultimo = sinAbrirUltimo ? std::string() : store_.ultimoTocado();
    if (!ultimo.empty()) abrir(QString::fromStdString(ultimo));
    refrescar();
}

EventoViewModel::~EventoViewModel() = default;

// --- La lista ---------------------------------------------------------------

QVariantList EventoViewModel::eventos() const {
    QVariantList out;
    for (const evento::Ficha& f : store_.listar()) {
        out.append(QVariantMap{{"carpeta", QString::fromStdString(f.carpeta)},
                               {"nombre", QString::fromStdString(f.nombre)},
                               {"fecha", QString::fromStdString(f.fecha)},
                               {"inicio", QString::fromStdString(f.inicio)},
                               {"huso", QString::fromStdString(f.huso)},
                               {"duracionSeg", f.duracionSeg},
                               {"bloques", static_cast<int>(f.bloques)},
                               {"cues", static_cast<int>(f.cues)}});
    }
    return out;
}

void EventoViewModel::refrescar() {
    emit listaChanged();
}

QString EventoViewModel::crear(const QString& nombre) {
    std::string err;
    const std::string carpeta = store_.crear(nombre.toStdString(), &err);
    if (carpeta.empty()) {
        emit notify("No se pudo crear el evento: " + QString::fromStdString(err), "error");
        return {};
    }
    emit listaChanged();
    abrir(QString::fromStdString(carpeta));
    return QString::fromStdString(carpeta);
}

bool EventoViewModel::abrir(const QString& carpeta) {
    if (sucio_) {
        // No se pierde trabajo por abrir otra cosa: se guarda lo que habia.
        guardar();
    }
    bool ok = false;
    std::string err;
    evento::Evento e = store_.cargar(carpeta.toStdString(), &ok, &err);
    if (!ok) {
        emit notify("No se pudo abrir el evento: " + QString::fromStdString(err), "error");
        return false;
    }
    parar();
    e_ = std::move(e);
    abierto_ = carpeta;
    sucio_ = false;
    reparos_.clear();
    bitacora_.clear();
    // El padron viene con el evento. Que no se pueda leer NO impide abrirlo:
    // el guion es lo que hay que poder correr, y quedarse sin evento por un
    // padron roto seria perder el acto entero por la lista de invitados.
    std::string errPadron;
    bool okPadron = false;
    padron_ = evento::cargarPadron(store_.carpetaDe(carpeta.toStdString()), &okPadron, &errPadron);
    if (!okPadron)
        emit notify("El padron de este evento no se pudo leer: " +
                        QString::fromStdString(errPadron),
                    "error");
    emit padronChanged();
    emit guionChanged();
    emit sucioChanged();
    emit reparosChanged();
    emit bitacoraChanged();
    return true;
}

void EventoViewModel::cerrar() {
    if (sucio_) guardar();
    parar();
    e_ = {};
    padron_ = {};
    abierto_.clear();
    sucio_ = false;
    reparos_.clear();
    emit padronChanged();
    emit guionChanged();
    emit sucioChanged();
    emit reparosChanged();
}

bool EventoViewModel::borrar(const QString& carpeta) {
    std::string err;
    if (!store_.borrar(carpeta.toStdString(), &err)) {
        emit notify("No se pudo borrar: " + QString::fromStdString(err), "error");
        return false;
    }
    if (abierto_ == carpeta) cerrar();
    emit listaChanged();
    return true;
}

// --- El guion ---------------------------------------------------------------

QVariantMap EventoViewModel::cueAMap(const evento::Cue& c, int bloque, int cue) {
    return QVariantMap{{"id", QString::fromStdString(c.id)},
                       {"tipo", QString::fromStdString(evento::nombreDeTipo(c.tipo))},
                       {"texto", QString::fromStdString(c.texto)},
                       {"datos", deJson(c.datos)},
                       {"duracionSeg", c.duracionSeg},
                       {"automatico", c.automatico},
                       {"falta", QString::fromStdString(c.queLeFalta())},
                       {"bloque", bloque},
                       {"cue", cue}};
}

QVariantMap EventoViewModel::guion() const {
    QVariantList bs;
    int nro = 0;
    for (std::size_t bi = 0; bi < e_.bloques.size(); ++bi) {
        const evento::Bloque& b = e_.bloques[bi];
        QVariantList cs;
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            QVariantMap m = cueAMap(b.cues[ci], static_cast<int>(bi), static_cast<int>(ci));
            m["nro"] = ++nro; // numeracion corrida, como un rundown de verdad
            m["offsetSeg"] = e_.offsetDeCueSeg(bi, ci);
            cs.append(m);
        }
        bs.append(QVariantMap{{"id", QString::fromStdString(b.id)},
                              {"nombre", QString::fromStdString(b.nombre)},
                              {"nota", QString::fromStdString(b.nota)},
                              {"duracionSeg", b.duracionSeg()},
                              {"offsetSeg", e_.offsetDeBloqueSeg(bi)},
                              {"indice", static_cast<int>(bi)},
                              {"cues", cs}});
    }
    return QVariantMap{{"nombre", QString::fromStdString(e_.nombre)},
                       {"fecha", QString::fromStdString(e_.fecha)},
                       {"inicio", QString::fromStdString(e_.inicio)},
                       {"huso", QString::fromStdString(e_.huso)},
                       {"cuando", cuando()},
                       {"nota", QString::fromStdString(e_.nota)},
                       {"duracionSeg", e_.duracionSeg()},
                       {"cues", static_cast<int>(e_.cantidadDeCues())},
                       {"bloques", bs}};
}

QVariantList EventoViewModel::tiposDeCue() const {
    QVariantList out;
    for (const CampoDeTipo& t : kTipos) {
        out.append(QVariantMap{{"tipo", QString::fromUtf8(t.tipo)},
                               {"titulo", QString::fromUtf8(t.titulo)},
                               {"ayuda", QString::fromUtf8(t.ayuda)},
                               {"campos", QString::fromUtf8(t.campos).split(',', Qt::SkipEmptyParts)}});
    }
    return out;
}

QStringList EventoViewModel::graficosDisponibles() const {
    return graficos_ == nullptr ? QStringList{} : graficos_->nombres();
}

QStringList EventoViewModel::fuentesDeGrafico() const {
    QStringList out;
    for (const auto& i : app::graficos::instancias()) out << QString::fromStdString(i.nombre);
    return out;
}

QVariantList EventoViewModel::parametrosDeGrafico(const QString& grafico) const {
    QVariantList out;
    if (graficos_ == nullptr) return out;
    for (const QJsonValue& v : graficos_->ficha(grafico).value("parametros").toArray()) {
        const QJsonObject p = v.toObject();
        out.append(QVariantMap{{"clave", p.value("clave").toString()},
                               {"titulo", p.value("titulo").toString()},
                               {"tipo", p.value("tipo").toString("texto")},
                               {"ayuda", p.value("ayuda").toString()},
                               {"porDefecto", p.value("porDefecto").toVariant()}});
    }
    return out;
}

void EventoViewModel::setValorDeCue(int bloque, int cue, const QString& clave,
                                    const QVariant& valor) {
    evento::Cue* c = cuePtr(bloque, cue);
    if (c == nullptr || clave.isEmpty()) return;
    if (!c->datos.contains("valores") || !c->datos["valores"].is_object())
        c->datos["valores"] = nlohmann::json::object();
    c->datos["valores"][clave.toStdString()] = valor.toString().toStdString();
    marcarSucio();
}

void EventoViewModel::marcarSucio() {
    if (!sucio_) {
        sucio_ = true;
        emit sucioChanged();
    }
    emit guionChanged();
}

const evento::Cue* EventoViewModel::cuePtrConst(int bloque, int cue) const {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return nullptr;
    const auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
    if (cue < 0 || cue >= static_cast<int>(cues.size())) return nullptr;
    return &cues[static_cast<std::size_t>(cue)];
}

evento::Cue* EventoViewModel::cuePtr(int bloque, int cue) {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return nullptr;
    auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
    if (cue < 0 || cue >= static_cast<int>(cues.size())) return nullptr;
    return &cues[static_cast<std::size_t>(cue)];
}

void EventoViewModel::setNombre(const QString& nombre) {
    e_.nombre = nombre.toStdString();
    marcarSucio();
}

void EventoViewModel::setInicio(const QString& hhmm) {
    e_.inicio = hhmm.toStdString();
    marcarSucio();
}

void EventoViewModel::setFecha(const QString& aaaammdd) {
    e_.fecha = aaaammdd.toStdString();
    marcarSucio();
}

void EventoViewModel::setHuso(const QString& iana) {
    e_.huso = iana.toStdString();
    marcarSucio();
}

QString EventoViewModel::husoDelAparato() {
    return QString::fromUtf8(QTimeZone::systemTimeZoneId());
}

QString EventoViewModel::cuando() const {
    if (e_.fecha.empty() || e_.inicio.empty()) return {};
    if (!evento::fechaValida(e_.fecha) || !evento::horaValida(e_.inicio)) return {};
    const QDate d = QDate::fromString(QString::fromStdString(e_.fecha), Qt::ISODate);
    const QTime h = QTime::fromString(QString::fromStdString(e_.inicio), QStringLiteral("HH:mm"));
    // Sin huso guardado corre el del aparato, que es lo que el operador ve en
    // la pared. Es un valor por defecto razonable, no una adivinanza: el que
    // arma el evento esta en el lugar del evento casi siempre.
    const QTimeZone z = e_.huso.empty() ? QTimeZone::systemTimeZone()
                                        : QTimeZone(QByteArray::fromStdString(e_.huso));
    if (!z.isValid()) return {};
    const QDateTime dt(d, h, z);
    if (!dt.isValid()) return {};
    return dt.toString(Qt::ISODate);
}

QString EventoViewModel::agregarBloque(const QString& nombre) {
    evento::Bloque b;
    b.id = nuevoId("b");
    b.nombre = nombre.isEmpty() ? "Bloque nuevo" : nombre.toStdString();
    e_.bloques.push_back(b);
    marcarSucio();
    return QString::fromStdString(b.id);
}

void EventoViewModel::renombrarBloque(int bloque, const QString& nombre) {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return;
    e_.bloques[static_cast<std::size_t>(bloque)].nombre = nombre.toStdString();
    marcarSucio();
}

void EventoViewModel::quitarBloque(int bloque) {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return;
    e_.bloques.erase(e_.bloques.begin() + bloque);
    marcarSucio();
}

void EventoViewModel::moverBloque(int desde, int hasta) {
    const int n = static_cast<int>(e_.bloques.size());
    if (desde < 0 || desde >= n || hasta < 0 || hasta >= n || desde == hasta) return;
    evento::Bloque b = e_.bloques[static_cast<std::size_t>(desde)];
    e_.bloques.erase(e_.bloques.begin() + desde);
    e_.bloques.insert(e_.bloques.begin() + hasta, std::move(b));
    marcarSucio();
}

QString EventoViewModel::agregarCue(int bloque, const QString& tipo) {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return {};
    evento::Cue c;
    c.id = nuevoId("c");
    if (!evento::tipoDeNombre(tipo.toStdString(), c.tipo)) c.tipo = evento::Tipo::Nota;
    e_.bloques[static_cast<std::size_t>(bloque)].cues.push_back(c);
    marcarSucio();
    return QString::fromStdString(c.id);
}

void EventoViewModel::editarCue(int bloque, int cue, const QVariantMap& campos) {
    evento::Cue* c = cuePtr(bloque, cue);
    if (c == nullptr) return;
    if (campos.contains("texto")) c->texto = campos["texto"].toString().toStdString();
    if (campos.contains("duracionSeg")) c->duracionSeg = std::max(0, campos["duracionSeg"].toInt());
    if (campos.contains("automatico")) c->automatico = campos["automatico"].toBool();
    if (campos.contains("tipo")) {
        evento::Tipo t{};
        if (evento::tipoDeNombre(campos["tipo"].toString().toStdString(), t)) c->tipo = t;
    }
    // `datos` se mezcla, no se reemplaza: la interfaz edita un campo por vez.
    if (campos.contains("datos")) {
        const QVariantMap d = campos["datos"].toMap();
        for (auto it = d.begin(); it != d.end(); ++it) {
            const std::string clave = it.key().toStdString();
            if (it.value().toString().isEmpty() && it.value().typeId() == QMetaType::QString)
                c->datos.erase(clave);
            else
                c->datos[clave] = aJson(it.value());
        }
    }
    marcarSucio();
}

void EventoViewModel::quitarCue(int bloque, int cue) {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return;
    auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
    if (cue < 0 || cue >= static_cast<int>(cues.size())) return;
    cues.erase(cues.begin() + cue);
    marcarSucio();
}

void EventoViewModel::moverCue(int deBloque, int deCue, int aBloque, int aCue) {
    const evento::Cue* origen = cuePtr(deBloque, deCue);
    if (origen == nullptr) return;
    if (aBloque < 0 || aBloque >= static_cast<int>(e_.bloques.size())) return;
    const evento::Cue copia = *origen;
    auto& desde = e_.bloques[static_cast<std::size_t>(deBloque)].cues;
    desde.erase(desde.begin() + deCue);
    auto& hasta = e_.bloques[static_cast<std::size_t>(aBloque)].cues;
    const int destino = std::clamp(aCue, 0, static_cast<int>(hasta.size()));
    hasta.insert(hasta.begin() + destino, copia);
    marcarSucio();
}

bool EventoViewModel::guardar() {
    if (abierto_.isEmpty()) return false;
    std::string err;
    if (!store_.guardar(abierto_.toStdString(), e_, &err)) {
        emit notify("No se pudo guardar el guion: " + QString::fromStdString(err), "error");
        return false;
    }
    sucio_ = false;
    emit sucioChanged();
    emit listaChanged();
    return true;
}

// --- Revision previa --------------------------------------------------------

QVariantList EventoViewModel::reparos() const {
    QVariantList out;
    for (const evento::Reparo& r : reparos_) {
        out.append(QVariantMap{{"bloque", r.bloque},
                               {"cue", r.cue},
                               {"grave", r.grave},
                               {"texto", QString::fromStdString(r.texto)}});
    }
    return out;
}

void EventoViewModel::ensayoAcelerado() {
    if (e_.cantidadDeCues() == 0) {
        emit notify("El guion no tiene ningún cue", "error");
        return;
    }
    // Se camina el recorrido de verdad, no la lista: asi se ve el orden real
    // y quedan a la vista los bloques que nunca se visitan.
    const std::vector<std::string> escenas = rt_.engine().sceneNames();
    evento::Guion recorrido(e_);
    int visitados = 0;
    int problemas = 0;
    int sinEjecutar = 0;
    bitacora_.clear();

    for (evento::Posicion p = recorrido.siguienteDe({}); p.valida(); p = recorrido.siguienteDe(p)) {
        const evento::Cue& c = e_.bloques[static_cast<std::size_t>(p.bloque)].cues[static_cast<std::size_t>(p.cue)];
        ++visitados;
        const QString donde = QString("%1.%2").arg(p.bloque + 1).arg(p.cue + 1);
        const QString que = c.texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c.tipo))
                                            : QString::fromStdString(c.texto);
        const std::string falta = c.queLeFalta();
        if (!falta.empty()) {
            ++problemas;
            anotar(donde + " " + que + " — " + QString::fromStdString(falta), "error");
            continue;
        }
        if (c.tipo == evento::Tipo::Escena) {
            const std::string escena = c.datos.value("escena", std::string());
            if (std::find(escenas.begin(), escenas.end(), escena) == escenas.end()) {
                ++problemas;
                anotar(donde + " " + que + " — no existe la escena \"" +
                           QString::fromStdString(escena) + "\"",
                       "error");
                continue;
            }
        }
        if (c.tipo != evento::Tipo::Escena && c.tipo != evento::Tipo::Orador &&
            c.tipo != evento::Tipo::Nota && c.tipo != evento::Tipo::Espera &&
            c.tipo != evento::Tipo::Grafico && c.tipo != evento::Tipo::Video &&
            c.tipo != evento::Tipo::Pausa) {
            ++sinEjecutar;
            anotar(donde + " " + que + " — el guion lo tiene, pero todavía no sé ejecutarlo", "info");
            continue;
        }
        anotar(donde + " " + que + " — listo", "ok");
    }

    // Bloques que el recorrido nunca pisa: estan escritos pero no pasan.
    int colgados = 0;
    for (const evento::Bloque& b : e_.bloques)
        if (b.cues.empty()) ++colgados;

    QString resumen = QString("Ensayo acelerado: %1 de %2 cues recorridos").arg(visitados).arg(e_.cantidadDeCues());
    if (colgados > 0) resumen += QString(", %1 bloque(s) vacío(s) que no pasan").arg(colgados);
    if (sinEjecutar > 0) resumen += QString(", %1 sin ejecución todavía").arg(sinEjecutar);
    if (problemas > 0) resumen += QString(", %1 con problemas").arg(problemas);
    anotar(resumen, problemas > 0 ? "error" : "ok");
    emit notify(resumen, problemas > 0 ? "error" : "ok");
}

QVariantMap EventoViewModel::informe() const {
    if (!guionCorriendo_) return {};
    const auto& pasos = guionCorriendo_->recorrido();
    if (pasos.empty()) return {};

    // Se agrupa por bloque respetando el ORDEN EN QUE PASO, no el del guion:
    // si el operador salto o repitio, el informe tiene que contarlo.
    QVariantList bloques;
    QVariantMap bloqueActual;
    QVariantList cuesDelBloque;
    int bloqueIdx = -1;
    int bPrevisto = 0;
    int bReal = 0;
    int totalPrevisto = 0;
    int totalReal = 0;
    int nro = 0;

    const auto cerrarBloque = [&]() {
        if (bloqueIdx < 0) return;
        bloqueActual["cues"] = cuesDelBloque;
        bloqueActual["previstoSeg"] = bPrevisto;
        bloqueActual["realSeg"] = bReal;
        bloqueActual["desvioSeg"] = bReal - bPrevisto;
        bloques.append(bloqueActual);
        cuesDelBloque.clear();
        bPrevisto = 0;
        bReal = 0;
    };

    for (const evento::Guion::Paso& p : pasos) {
        // El ultimo cue puede seguir corriendo: no tiene duracion real todavia.
        if (p.hastaMs == 0) continue;
        if (p.pos.bloque != bloqueIdx) {
            cerrarBloque();
            bloqueIdx = p.pos.bloque;
            const auto bi = static_cast<std::size_t>(bloqueIdx);
            bloqueActual = QVariantMap{
                {"nombre", bi < e_.bloques.size() ? QString::fromStdString(e_.bloques[bi].nombre)
                                                  : QString("(bloque borrado)")}};
        }
        const evento::Cue* c = cuePtrConst(p.pos.bloque, p.pos.cue);
        const int real = p.realSeg();
        cuesDelBloque.append(QVariantMap{
            {"nro", ++nro},
            {"texto", c != nullptr ? QString::fromStdString(c->texto) : QString()},
            {"tipo", c != nullptr ? QString::fromStdString(evento::nombreDeTipo(c->tipo)) : QString()},
            {"previstoSeg", p.previstoSeg},
            {"realSeg", real},
            {"desvioSeg", real - p.previstoSeg}});
        bPrevisto += p.previstoSeg;
        bReal += real;
        totalPrevisto += p.previstoSeg;
        totalReal += real;
    }
    cerrarBloque();

    return QVariantMap{{"evento", QString::fromStdString(e_.nombre)},
                       {"ensayo", ensayo_},
                       {"previstoSeg", totalPrevisto},
                       {"realSeg", totalReal},
                       {"desvioSeg", totalReal - totalPrevisto},
                       {"cues", nro},
                       {"bloques", bloques}};
}

QString EventoViewModel::guardarInforme() {
    const QVariantMap inf = informe();
    if (inf.isEmpty() || abierto_.isEmpty()) {
        emit notify("Todavía no hay nada que informar: el guion no se corrió", "error");
        return {};
    }
    const QDir carpeta(QString::fromStdString(store_.carpetaDe(abierto_.toStdString()).string()));
    const QString nombre =
        "informe-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
    const QString ruta = carpeta.filePath(nombre);
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit notify("No se pudo escribir el informe en " + ruta, "error");
        return {};
    }
    f.write(QJsonDocument(QJsonObject::fromVariantMap(inf)).toJson());
    emit notify("Informe guardado en " + nombre, "ok");
    return ruta;
}

void EventoViewModel::revisar() {
    reparos_ = evento::revisar(e_);

    // Lo que el modelo no puede saber solo: si las escenas existen de verdad.
    std::vector<std::string> escenas;
    escenas = rt_.engine().sceneNames();
    for (std::size_t bi = 0; bi < e_.bloques.size(); ++bi) {
        const evento::Bloque& b = e_.bloques[bi];
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            const evento::Cue& c = b.cues[ci];
            if (c.tipo == evento::Tipo::Grafico && graficos_ != nullptr) {
                const std::string g = c.datos.value("grafico", std::string());
                if (!g.empty() && !graficos_->nombres().contains(QString::fromStdString(g)))
                    reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                        "no existe un grafico llamado \"" + g + "\""});
                // La fuente se marca SIN gravedad: es normal escribir el guion
                // antes de armar las escenas, y se arregla agregando la fuente.
                const std::string f = c.datos.value("fuente", std::string());
                if (!f.empty() && graficos_->fuentePorNombre(QString::fromStdString(f)) == 0)
                    reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), false,
                                        "todavia no hay ninguna fuente de grafico llamada \"" + f +
                                            "\": agregala a la escena donde tiene que verse"});
                continue;
            }
            if (c.tipo != evento::Tipo::Escena) continue;
            const std::string nombre = c.datos.value("escena", std::string());
            if (nombre.empty()) continue; // ya lo dijo la revision del modelo
            if (std::find(escenas.begin(), escenas.end(), nombre) == escenas.end()) {
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                    "no existe una escena llamada \"" + nombre + "\""});
            }
        }
    }
    // Y lo que hay que ir a mirar al disco: los archivos que el guion nombra.
    if (!abierto_.isEmpty()) {
        for (const evento::Reparo& r :
             evento::revisarArchivos(e_, store_.carpetaDe(abierto_.toStdString())))
            reparos_.push_back(r);
    }
    emit reparosChanged();

    int graves = 0;
    for (const evento::Reparo& r : reparos_)
        if (r.grave) ++graves;
    if (reparos_.empty())
        emit notify("La revisión no encontró problemas", "ok");
    else if (graves > 0)
        emit notify(QString("La revisión encontró %1 problema(s) que impiden correr el guion").arg(graves),
                    "error");
    else
        emit notify("La revisión encontró cosas para mirar, ninguna grave", "info");
}

// --- Correr -----------------------------------------------------------------

QString EventoViewModel::estado() const {
    if (!guionCorriendo_) return "parado";
    switch (guionCorriendo_->estado()) {
        case evento::Guion::Estado::Corriendo: return "corriendo";
        case evento::Guion::Estado::Pausado: return "pausado";
        case evento::Guion::Estado::Terminado: return "terminado";
        default: return "parado";
    }
}

bool EventoViewModel::enVivo() const {
    if (ensayo_ || !guionCorriendo_) return false;
    const auto e = guionCorriendo_->estado();
    // Pausado tambien cuenta: el evento esta pasando, el operador solo freno
    // el guion un momento.
    return e == evento::Guion::Estado::Corriendo || e == evento::Guion::Estado::Pausado;
}

QVariantMap EventoViewModel::actual() const {
    if (!guionCorriendo_) return {};
    const evento::Posicion p = guionCorriendo_->posicion();
    const evento::Cue* c = guionCorriendo_->cueActual();
    if (c == nullptr) return {};
    QVariantMap m = cueAMap(*c, p.bloque, p.cue);
    m["bloqueNombre"] =
        QString::fromStdString(e_.bloques[static_cast<std::size_t>(p.bloque)].nombre);
    return m;
}

QVariantMap EventoViewModel::siguiente() const {
    if (!guionCorriendo_) return {};
    const evento::Posicion s = guionCorriendo_->siguienteDe(guionCorriendo_->posicion());
    const evento::Cue* c = guionCorriendo_->cueSiguiente();
    if (c == nullptr) return {};
    QVariantMap m = cueAMap(*c, s.bloque, s.cue);
    m["bloqueNombre"] =
        QString::fromStdString(e_.bloques[static_cast<std::size_t>(s.bloque)].nombre);
    return m;
}

QVariantMap EventoViewModel::reloj() const {
    if (!guionCorriendo_) return QVariantMap{{"desvioSeg", 0}, {"enCueSeg", 0}, {"restanteSeg", 0}};
    const int64_t t = ahoraMs();
    // En que numero de cue vamos, para "12 de 34".
    int nro = 0;
    const evento::Posicion p = guionCorriendo_->posicion();
    for (int b = 0; b < static_cast<int>(e_.bloques.size()) && b <= p.bloque; ++b) {
        const int hasta = b == p.bloque ? p.cue + 1
                                        : static_cast<int>(e_.bloques[static_cast<std::size_t>(b)].cues.size());
        nro += hasta;
    }
    return QVariantMap{{"desvioSeg", guionCorriendo_->desvioSeg(t)},
                       {"enCueSeg", guionCorriendo_->enCueSeg(t)},
                       {"restanteSeg", guionCorriendo_->restanteSeg(t)},
                       {"cueNro", p.valida() ? nro : 0},
                       {"cuesTotal", static_cast<int>(e_.cantidadDeCues())}};
}

void EventoViewModel::setEnsayo(bool on) {
    if (ensayo_ == on) return;
    ensayo_ = on;
    emit correChanged();
    anotar(on ? "Ensayo: el guion corre sin salir al aire" : "Ensayo apagado: el guion sale al aire",
           "info");
}

void EventoViewModel::anotar(const QString& texto, const QString& tipo) {
    QVariantMap m{{"hora", QDateTime::currentDateTime().toString("HH:mm:ss")},
                  {"texto", texto},
                  {"tipo", tipo}};
    bitacora_.prepend(m);
    // La bitacora es para mirar de reojo durante el evento, no un registro
    // historico: eso es el recorrido del guion.
    while (bitacora_.size() > 60) bitacora_.removeLast();
    emit bitacoraChanged();
}

void EventoViewModel::arrancar() {
    if (e_.cantidadDeCues() == 0) {
        emit notify("El guion no tiene ningún cue", "error");
        return;
    }
    guionCorriendo_ = std::make_unique<evento::Guion>(e_);
    guionCorriendo_->arrancar(ahoraMs());
    reloj_.start();
    anotar(ensayo_ ? "Arranca el ensayo" : "Arranca el evento", "ok");
    ejecutarActual();
    emit correChanged();
    emit relojChanged();
}

void EventoViewModel::avanzar() {
    if (!guionCorriendo_) {
        arrancar();
        return;
    }
    if (!guionCorriendo_->avanzar(ahoraMs())) {
        reloj_.stop();
        anotar(ensayo_ ? "Terminó el ensayo" : "Terminó el evento", "ok");
        emit correChanged();
        return;
    }
    ejecutarActual();
    emit correChanged();
    emit relojChanged();
}

void EventoViewModel::retroceder() {
    if (!guionCorriendo_ || !guionCorriendo_->retroceder(ahoraMs())) return;
    anotar("Vuelve atrás", "info");
    ejecutarActual();
    emit correChanged();
    emit relojChanged();
}

void EventoViewModel::irA(int bloque, int cue) {
    if (!guionCorriendo_) {
        guionCorriendo_ = std::make_unique<evento::Guion>(e_);
        reloj_.start();
    }
    if (!guionCorriendo_->irA({bloque, cue}, ahoraMs())) return;
    anotar("Salta al cue elegido", "info");
    ejecutarActual();
    emit correChanged();
    emit relojChanged();
}

void EventoViewModel::pausar() {
    if (!guionCorriendo_) return;
    guionCorriendo_->pausar(ahoraMs());
    anotar("Guion en pausa", "info");
    emit correChanged();
}

void EventoViewModel::reanudar() {
    if (!guionCorriendo_) return;
    guionCorriendo_->reanudar(ahoraMs());
    anotar("Sigue el guion", "info");
    emit correChanged();
}

void EventoViewModel::parar() {
    if (!guionCorriendo_) return;
    reloj_.stop();
    guionCorriendo_.reset();
    emit correChanged();
    emit relojChanged();
}

QString EventoViewModel::dentroDelEvento(const QString& sub, const QString& rel) const {
    if (abierto_.isEmpty() || rel.isEmpty()) return {};
    const QDir carpeta(QString::fromStdString(store_.carpetaDe(abierto_.toStdString()).string()));
    // "Medios/x.wav" y "x.wav" valen lo mismo: el operador escribe lo que le
    // sale y las dos formas apuntan al mismo lado.
    QString limpio = rel;
    if (limpio.startsWith(sub + "/") || limpio.startsWith(sub + "\\"))
        limpio = limpio.mid(sub.size() + 1);
    const QDir dir(carpeta.filePath(sub));
    const QString abs = QDir::cleanPath(dir.filePath(limpio));
    // Fuera de esa subcarpeta no se lee nada: un ".." en un guion que vino de
    // otro lado no tiene por que sacarle archivos al que lo abre.
    if (!abs.startsWith(QDir::cleanPath(dir.absolutePath()) + "/")) return {};
    return abs;
}

QString EventoViewModel::discursoPathPriv(const QString& rel) const {
    return dentroDelEvento("Discursos", rel);
}

QString EventoViewModel::carpetaDeDiscursos() const {
    if (abierto_.isEmpty()) return {};
    return QString::fromStdString((store_.carpetaDe(abierto_.toStdString()) / "Discursos").string());
}

bool EventoViewModel::prepararEscenaDeVideo() {
    const auto escenas = rt_.engine().sceneNames();
    const bool existe = std::find(escenas.begin(), escenas.end(), kEscenaVideo) != escenas.end();
    if (!existe) {
        if (!rt_.addScene(kEscenaVideo)) {
            anotar("No se pudo crear la escena del video: " + QString::fromStdString(rt_.lastError()),
                   "error");
            return false;
        }
        // Protegida: si alguien la borra, todos los cues de video del guion
        // dejan de funcionar y no es obvio por qué.
        rt_.setSceneProtected(kEscenaVideo, true);
        anotar("Creé la escena «" + QString::fromStdString(kEscenaVideo) + "» para los videos", "info");
    }
    // La fuente de video: se crea una vez y después solo se le cambia el
    // archivo. Ocupa el cuadro entero, que es lo que uno espera de un video.
    const auto fuentes = rt_.engine().sceneSources(kEscenaVideo);
    if (std::find(fuentes.begin(), fuentes.end(), kVideo) == fuentes.end()) {
        SourceSpec spec;
        spec.id = "ffmpeg_source";
        spec.name = kVideo;
        spec.rect = CanvasRect{0.0, 0.0, 1.0, 1.0};
        spec.settingsJson = R"({"is_local_file":true,"clear_on_media_end":true})";
        if (!rt_.addSource(kEscenaVideo, spec)) {
            anotar("No se pudo crear la fuente de video: " + QString::fromStdString(rt_.lastError()),
                   "error");
            return false;
        }
    }
    return true;
}

QString EventoViewModel::medioPath(const QString& rel) const {
    return dentroDelEvento("Medios", rel);
}

void EventoViewModel::ejecutarActual() {
    const evento::Cue* c = guionCorriendo_ ? guionCorriendo_->cueActual() : nullptr;
    if (c == nullptr) return;

    const QString titulo = c->texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c->tipo))
                                            : QString::fromStdString(c->texto);
    if (ensayo_) {
        // El ensayo recorre el guion igual pero no toca nada: es para ver
        // tiempos y orden antes del evento.
        anotar("(ensayo) " + titulo, "info");
        return;
    }

    switch (c->tipo) {
        case evento::Tipo::Escena: {
            const std::string escena = c->datos.value("escena", std::string());
            if (escena.empty() || !rt_.engine().setCurrentScene(escena)) {
                anotar("No se pudo poner la escena \"" + QString::fromStdString(escena) + "\"", "error");
                emit notify("El cue no se pudo ejecutar: no existe la escena \"" +
                                QString::fromStdString(escena) + "\"",
                            "error");
                return;
            }
            anotar("Al aire: " + QString::fromStdString(escena), "ok");
            return;
        }
        case evento::Tipo::Orador: {
            const QString quien = QString::fromStdString(c->datos.value("participante", std::string()));
            if (sala_ == nullptr || quien.isEmpty()) {
                anotar("No se pudo dar la palabra: no dice a quién", "error");
                return;
            }
            if (c->duracionSeg > 0) sala_->speakerTime(quien, c->duracionSeg);
            sala_->speakerOnAir(quien, true);
            anotar("Habla " + quien, "ok");
            return;
        }
        case evento::Tipo::Musica: {
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            // El nombre es fijo: asi un tema nuevo reemplaza al anterior en vez
            // de encimarse, que es lo peor que puede pasar en vivo.
            const std::string kMusica = "Música del evento";
            if (accion == "parar") {
                rt_.engine().stopMedia(kMusica);
                anotar("Música: para", "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar("No encontré la música \"" + rel + "\" en Medios/", "error");
                emit notify("El cue no se pudo ejecutar: falta Medios/" + rel, "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            if (!rt_.engine().playMedia(kMusica, ruta.toStdString(), repetir)) {
                anotar("No se pudo reproducir " + rel + ": " +
                           QString::fromStdString(rt_.engine().lastError()),
                       "error");
                return;
            }
            anotar("Música: " + rel + (repetir ? " (en bucle)" : ""), "ok");
            return;
        }
        case evento::Tipo::Teleprompter: {
            // El texto vive en Discursos/ de la carpeta del evento, no pegado
            // en la tablet: asi viaja con el evento y el asistente puede
            // ayudar a escribirlo o acortarlo.
            const QString rel = QString::fromStdString(c->datos.value("discurso", std::string()));
            if (sala_ == nullptr || rel.isEmpty()) {
                anotar("No se pudo cargar el discurso: no dice cuál", "error");
                return;
            }
            const QString ruta = discursoPathPriv(rel);
            QFile f(ruta);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                anotar("No se pudo abrir el discurso: " + ruta, "error");
                emit notify("No encontré el discurso \"" + rel + "\" en la carpeta del evento", "error");
                return;
            }
            const QString texto = QString::fromUtf8(f.readAll());
            QJsonObject cmd{{"titulo", titulo}, {"texto", texto}};
            // `arrancar` en los datos del cue: por defecto el texto queda
            // LISTO pero parado, que es lo que se quiere casi siempre (el
            // orador todavia esta caminando hacia el atril).
            if (c->datos.value("arrancar", false)) cmd["corriendo"] = true;
            sala_->client().post("/api/v1/prompter", cmd);
            anotar("Teleprompter: " + rel, "ok");
            return;
        }
        case evento::Tipo::Grafico: {
            // El gráfico sale en una FUENTE de escena, que el guion nombra. Si
            // esa fuente no está en ninguna escena, el gráfico no lo ve nadie:
            // se dice, en vez de dar el cue por hecho.
            const std::string accion = c->datos.value("accion", std::string("mostrar"));
            // Prender o apagar un rotulo que se mantiene solo: «durante las
            // entrevistas, zocalo automatico; en el cierre, apagado».
            if (accion == "prender" || accion == "apagar") {
                if (vinculos_ == nullptr) {
                    anotar("No hay motor de gráficos para los vínculos", "error");
                    return;
                }
                const QString id = QString::fromStdString(c->datos.value("vinculo", std::string()));
                if (!vinculos_->activar(id, accion == "prender")) {
                    anotar("No existe el vínculo \"" + id + "\"", "error");
                    emit notify("El cue no se pudo ejecutar: no existe el vínculo \"" + id + "\"",
                                "error");
                    return;
                }
                anotar((accion == "prender" ? "Prende el rótulo automático " : "Apaga el rótulo automático ") + id,
                       "ok");
                return;
            }
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (graficos_ == nullptr) {
                anotar("No hay renderizador de gráficos en este bundle", "error");
                return;
            }
            const std::uint64_t id = graficos_->fuentePorNombre(fuente);
            if (id == 0) {
                anotar("No hay ninguna fuente de gráfico llamada \"" + fuente + "\" en las escenas",
                       "error");
                emit notify("El cue no se pudo ejecutar: falta la fuente de gráfico \"" + fuente + "\"",
                            "error");
                return;
            }
            if (accion == "ocultar") {
                const QString err = graficos_->ocultar(id);
                if (!err.isEmpty()) {
                    anotar("No se pudo sacar el gráfico: " + err, "error");
                    return;
                }
                anotar("Saca el gráfico de " + fuente, "ok");
                return;
            }
            const QString grafico = QString::fromStdString(c->datos.value("grafico", std::string()));
            QJsonObject valores;
            if (c->datos.contains("valores"))
                valores = QJsonDocument::fromJson(
                              QByteArray::fromStdString(c->datos["valores"].dump()))
                              .object();
            const int dur = c->datos.value("duracionMs", 0);
            const bool repetir = c->datos.value("repetir", false);
            const QString err = graficos_->mostrar(grafico, valores, id, dur, repetir);
            if (!err.isEmpty()) {
                anotar("No se pudo sacar al aire el gráfico \"" + grafico + "\": " + err, "error");
                emit notify("El cue de gráfico falló: " + err, "error");
                return;
            }
            anotar("Al aire el gráfico " + grafico + " en " + fuente, "ok");
            return;
        }
        case evento::Tipo::Espera:
            anotar("Espera tu confirmación: " + titulo, "info");
            return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa:
            anotar(titulo, "info");
            return;
        case evento::Tipo::Audio: {
            const std::string accion = c->datos.value("accion", std::string());
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (fuente.isEmpty()) {
                anotar("El cue de audio no dice sobre que fuente actuar", "error");
                return;
            }
            const std::string f = fuente.toStdString();
            if (accion == "silenciar" || accion == "activar") {
                const bool mudo = accion == "silenciar";
                if (!rt_.engine().setSourceMuted(f, mudo)) {
                    anotar("No existe la fuente de audio \"" + fuente + "\"", "error");
                    emit notify("El cue no se pudo ejecutar: no existe la fuente \"" + fuente + "\"",
                                "error");
                    return;
                }
                anotar((mudo ? "Silencia " : "Activa ") + fuente, "ok");
                return;
            }
            if (accion == "volumen") {
                // En dB, que es como piensa el operador. 0 = sin cambio.
                const double db = c->datos.value("db", 0.0);
                if (!rt_.engine().setSourceVolume(f, static_cast<float>(std::pow(10.0, db / 20.0)))) {
                    anotar("No existe la fuente de audio \"" + fuente + "\"", "error");
                    return;
                }
                anotar(fuente + " a " + QString::number(db, 'f', 1) + " dB", "ok");
                return;
            }
            anotar("No sé qué es la acción de audio \"" + QString::fromStdString(accion) +
                       "\" (silenciar, activar o volumen)",
                   "error");
            return;
        }
        case evento::Tipo::Video: {
            // Un video tiene que VERSE, o sea estar en una escena, y las
            // escenas las arma el operador: meterle una fuente a la escena que
            // está al aire le desarmaría lo que compuso.
            //
            // Por eso el video del guion tiene SU escena, una sola, que se crea
            // sola la primera vez y queda PROTEGIDA: es la que el guion llena y
            // pone al aire. El operador puede componerla como quiera —ponerle
            // un fondo, un logo— y lo único que el guion le cambia es el
            // archivo de la fuente de video.
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            if (accion == "parar") {
                // Sin archivo, la fuente queda en negro. No se la saca de la
                // escena: recrearla en cada video costaria mas y se veria.
                rt_.updateSourceSettings(kVideo, R"({"local_file":"","is_local_file":true})");
                anotar("Video: para", "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar("No encontré el video \"" + rel + "\" en Medios/", "error");
                emit notify("El cue no se pudo ejecutar: falta Medios/" + rel, "error");
                return;
            }
            if (!prepararEscenaDeVideo()) return;
            const bool repetir = c->datos.value("repetir", false);
            // Se le cambia el ARCHIVO a la fuente que ya esta en la escena, no
            // se crea una nueva: `playMedia` sirve para la musica, que va por
            // un canal global de audio y no se ve.
            const QJsonObject ajustes{{"local_file", ruta},
                                      {"is_local_file", true},
                                      {"looping", repetir},
                                      {"restart_on_activate", false},
                                      {"close_when_inactive", false},
                                      {"clear_on_media_end", true},
                                      {"hw_decode", true}};
            if (!rt_.updateSourceSettings(
                    kVideo,
                    QJsonDocument(ajustes).toJson(QJsonDocument::Compact).toStdString())) {
                anotar("No se pudo cargar " + rel + ": " +
                           QString::fromStdString(rt_.lastError()),
                       "error");
                return;
            }
            // Y se rebobina: si el archivo es el mismo que ya estaba, libobs no
            // tiene motivo para volver a empezar y el cue no mostraria nada.
            rt_.engine().restartMedia(kVideo);
            // Al aire recién DESPUÉS de que el archivo esté cargado: al revés
            // se ve un cuadro en negro mientras abre.
            if (c->datos.value("alAire", true)) {
                escenaPrevia_ = QString::fromStdString(rt_.engine().currentScene());
                if (!rt_.engine().setCurrentScene(kEscenaVideo)) {
                    anotar("No se pudo poner al aire la escena del video", "error");
                    return;
                }
            }
            anotar("Video: " + rel + (repetir ? " (en bucle)" : ""), "ok");
            return;
        }
        default:
            // Honestidad: el tipo existe en el guion pero todavia no se
            // ejecuta. Se anota y el guion sigue, en vez de hacer como que
            // paso algo.
            anotar("Todavía no sé ejecutar un cue de tipo \"" +
                       QString::fromStdString(evento::nombreDeTipo(c->tipo)) + "\": " + titulo,
                   "error");
            return;
    }
}

bool EventoViewModel::setAutoAceptar(std::vector<evento::Invitado> lista) {
    if (abierto_.isEmpty()) return false;
    e_.autoAceptar = std::move(lista);
    return guardar();
}

// --- El padron ---------------------------------------------------------------
// Cada cambio se guarda EN EL MOMENTO. El guion tiene "sin guardar" porque se
// lo escribe con tiempo y uno quiere poder deshacer; el padron se toca en la
// puerta, con cola, y ahi nadie se va a acordar de apretar guardar. La escritura
// es barata y atomica (ver guardarPadron).

namespace {

/// Pasa un mapa de QML/MCP a un asistente, y de paso dice QUE claves vinieron:
/// no es lo mismo "poner la nota en vacio" que "no tocar la nota".
evento::Asistente desdeMapa(const QVariantMap& m, std::vector<std::string>& queCampos) {
    evento::Asistente a;
    const auto vino = [&m, &queCampos](const char* c) {
        if (!m.contains(c)) return false;
        queCampos.emplace_back(c);
        return true;
    };
    if (vino("personaId")) a.personaId = m.value("personaId").toString().toStdString();
    if (vino("nombre")) a.nombre = m.value("nombre").toString().toStdString();
    if (vino("contacto")) a.contacto = m.value("contacto").toString().toStdString();
    if (vino("nota")) a.nota = m.value("nota").toString().toStdString();
    if (vino("dejarPasarSolo")) a.dejarPasarSolo = m.value("dejarPasarSolo").toBool();
    if (vino("vota")) a.vota = m.value("vota").toBool();
    // Una palabra que no se entiende no se ignora en silencio: se saca de la
    // lista de campos y queda el valor que habia. Si no, "papel: moderdor" con
    // un dedazo convertiria a un orador en publico sin que nadie lo note.
    if (vino("papel") && !evento::papelDeNombre(m.value("papel").toString().toStdString(), a.papel))
        queCampos.pop_back();
    if (vino("canal") && !evento::canalDeNombre(m.value("canal").toString().toStdString(), a.canal))
        queCampos.pop_back();
    if (vino("estado") && !evento::estadoDeNombre(m.value("estado").toString().toStdString(), a.estado))
        queCampos.pop_back();
    return a;
}

QVariantMap aMapa(const evento::Asistente& a) {
    return QVariantMap{{"id", QString::fromStdString(a.id)},
                       {"personaId", QString::fromStdString(a.personaId)},
                       {"nombre", QString::fromStdString(a.nombre)},
                       {"papel", QString::fromUtf8(evento::nombreDePapel(a.papel))},
                       {"canal", QString::fromUtf8(evento::nombreDeCanal(a.canal))},
                       {"estado", QString::fromUtf8(evento::nombreDeEstado(a.estado))},
                       {"contacto", QString::fromStdString(a.contacto)},
                       {"nota", QString::fromStdString(a.nota)},
                       {"dejarPasarSolo", a.dejarPasarSolo},
                       {"vota", a.vota},
                       {"llegoMs", static_cast<qint64>(a.llegoMs)},
                       {"porDonde", QString::fromStdString(a.porDonde)}};
}

} // namespace

QVariantList EventoViewModel::padron() const {
    QVariantList out;
    for (const evento::Asistente& a : padron_.gente()) out.append(aMapa(a));
    return out;
}

QVariantMap EventoViewModel::resumenPadron() const {
    const evento::Resumen r = padron_.resumen();
    return QVariantMap{{"total", r.total},
                       {"presentes", r.presentes},
                       {"presenciales", r.presenciales},
                       {"enLinea", r.enLinea},
                       {"confirmados", r.confirmados},
                       {"faltan", r.faltan},
                       {"conVoto", r.conVoto},
                       {"votoPresente", r.votoPresente}};
}

bool EventoViewModel::guardarPadronAhora() {
    if (abierto_.isEmpty()) return false;
    std::string err;
    if (!evento::guardarPadron(store_.carpetaDe(abierto_.toStdString()), padron_, &err)) {
        emit notify("No se pudo guardar el padron: " + QString::fromStdString(err), "error");
        return false;
    }
    emit padronChanged();
    return true;
}

QString EventoViewModel::anotar(const QVariantMap& datos) {
    if (abierto_.isEmpty()) return {};
    std::vector<std::string> queCampos;
    const evento::Asistente a = desdeMapa(datos, queCampos);
    if (a.nombre.empty()) {
        emit notify("Para anotar a alguien hace falta al menos su nombre.", "error");
        return {};
    }
    const QString id = QString::fromStdString(padron_.agregar(a));
    guardarPadronAhora();
    return id;
}

bool EventoViewModel::cambiarAsistente(const QString& id, const QVariantMap& datos) {
    if (abierto_.isEmpty()) return false;
    std::vector<std::string> queCampos;
    const evento::Asistente a = desdeMapa(datos, queCampos);
    if (!padron_.cambiar(id.toStdString(), a, queCampos)) return false;
    return guardarPadronAhora();
}

bool EventoViewModel::quitarAsistente(const QString& id) {
    if (abierto_.isEmpty()) return false;
    if (!padron_.quitar(id.toStdString())) return false;
    return guardarPadronAhora();
}

bool EventoViewModel::acreditar(const QString& id, const QString& porDonde) {
    if (abierto_.isEmpty()) return false;
    const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    if (!padron_.acreditar(id.toStdString(), porDonde.toStdString(),
                           static_cast<std::int64_t>(ahora)))
        return false;
    return guardarPadronAhora();
}

bool EventoViewModel::despedir(const QString& id) {
    if (abierto_.isEmpty()) return false;
    if (!padron_.despedir(id.toStdString())) return false;
    return guardarPadronAhora();
}

QString EventoViewModel::buscarEnPadron(const QString& personaId, const QString& nombre) const {
    // La ficha de rostro manda sobre el nombre: el nombre lo escribe cualquiera
    // y hay dos Gonzalez en cualquier lista de cuarenta.
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr) a = padron_.porNombre(nombre.toStdString());
    return a == nullptr ? QString() : QString::fromStdString(a->id);
}

} // namespace ba::app
