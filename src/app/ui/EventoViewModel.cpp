// ============================================================================
// src/app/ui/EventoViewModel.cpp — ver EventoViewModel.hpp
// ============================================================================
#include "ui/EventoViewModel.hpp"

#include <algorithm>
#include <chrono>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonObject>

#include "AppRuntime.hpp"
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
    {"musica", "Música", "Arranca o para un tema, con fundido.", "archivo,accion,fundidoSeg"},
    {"video", "Video", "Reproduce un video del evento.", "archivo,alTerminar"},
    {"teleprompter", "Teleprompter", "Carga un discurso y lo deja listo.", "discurso,arrancar"},
    {"orador", "Orador", "Le da la palabra a alguien, con su tiempo.", "participante"},
    {"pantalla", "Pantalla", "Manda contenido a una pantalla.", "pantalla,contenido"},
    {"audio", "Audio", "Silencia, cambia el rack o baja el ambiente.", "accion,fuente"},
    {"pausa", "Pausa", "Espera los segundos que diga la duración.", ""},
    {"espera", "Espera al operador", "Frena el guion hasta que confirmes.", ""},
    {"nota", "Nota", "No hace nada: es una indicación para vos.", ""},
};

} // namespace

EventoViewModel::EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), rt_(rt), sala_(sala), store_(rt.dataRoot() / "Eventos") {
    // El reloj del evento va aparte del guion: cambia cada segundo y no tiene
    // sentido redibujar la lista de cues por eso.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() { emit relojChanged(); });
    // Se abre solo el evento que se guardo ultimo: el operador trabaja en el
    // mismo durante dias, y abrir el programa para encontrarlo vacio es una
    // molestia diaria. No hace falta guardar "el ultimo abierto" en ningun
    // lado: la fecha del archivo ya lo dice.
    const std::string ultimo = store_.ultimoTocado();
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
                               {"inicio", QString::fromStdString(f.inicio)},
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
    abierto_.clear();
    sucio_ = false;
    reparos_.clear();
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
                       {"inicio", QString::fromStdString(e_.inicio)},
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

void EventoViewModel::marcarSucio() {
    if (!sucio_) {
        sucio_ = true;
        emit sucioChanged();
    }
    emit guionChanged();
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

void EventoViewModel::revisar() {
    reparos_ = evento::revisar(e_);

    // Lo que el modelo no puede saber solo: si las escenas existen de verdad.
    std::vector<std::string> escenas;
    escenas = rt_.engine().sceneNames();
    for (std::size_t bi = 0; bi < e_.bloques.size(); ++bi) {
        const evento::Bloque& b = e_.bloques[bi];
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            const evento::Cue& c = b.cues[ci];
            if (c.tipo != evento::Tipo::Escena) continue;
            const std::string nombre = c.datos.value("escena", std::string());
            if (nombre.empty()) continue; // ya lo dijo la revision del modelo
            if (std::find(escenas.begin(), escenas.end(), nombre) == escenas.end()) {
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                    "no existe una escena llamada \"" + nombre + "\""});
            }
        }
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

QString EventoViewModel::discursoPath(const QString& rel) const {
    const QDir carpeta(QString::fromStdString(store_.carpetaDe(abierto_.toStdString()).string()));
    // "Discursos/x.txt" y "x.txt" valen lo mismo: el operador escribe lo que
    // le sale y las dos formas apuntan al mismo lado.
    QString limpio = rel;
    if (limpio.startsWith("Discursos/") || limpio.startsWith("Discursos\\"))
        limpio = limpio.mid(10);
    const QDir discursos(carpeta.filePath("Discursos"));
    const QString abs = QDir::cleanPath(discursos.filePath(limpio));
    // Fuera de Discursos/ no se lee nada: ".." en el guion no sirve para
    // sacarle archivos al que lo abre.
    if (!abs.startsWith(QDir::cleanPath(discursos.absolutePath()) + "/")) return {};
    return abs;
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
        case evento::Tipo::Teleprompter: {
            // El texto vive en Discursos/ de la carpeta del evento, no pegado
            // en la tablet: asi viaja con el evento y el asistente puede
            // ayudar a escribirlo o acortarlo.
            const QString rel = QString::fromStdString(c->datos.value("discurso", std::string()));
            if (sala_ == nullptr || rel.isEmpty()) {
                anotar("No se pudo cargar el discurso: no dice cuál", "error");
                return;
            }
            const QString ruta = discursoPath(rel);
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
        case evento::Tipo::Espera:
            anotar("Espera tu confirmación: " + titulo, "info");
            return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa:
            anotar(titulo, "info");
            return;
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

} // namespace ba::app
