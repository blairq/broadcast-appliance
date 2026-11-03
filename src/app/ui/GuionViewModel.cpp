// ============================================================================
// src/app/ui/GuionViewModel.cpp — ver GuionViewModel.hpp
// ============================================================================
#include "ui/GuionViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

#include "arranque/AppRuntime.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/RefEscena.hpp"
#include "evento/EventoStore.hpp"
#include "evento/TiposDeCue.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/Discursos.hpp"
#include "ui/EventoAbierto.hpp"
#include <QCoreApplication>

namespace ba::app {

namespace {

using json = nlohmann::json;

QVariant deJson(const json& j) {
    if (j.is_string()) return QString::fromStdString(j.get<std::string>());
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return static_cast<int>(j.get<int64_t>());
    if (j.is_number_float()) return j.get<double>();
    if (j.is_object()) {
        QVariantMap m;
        for (auto it = j.begin(); it != j.end(); ++it) m[QString::fromStdString(it.key())] = deJson(it.value());
        return m;
    }
    if (j.is_array()) {
        QVariantList l;
        for (const json& x : j) l.append(deJson(x));
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
            for (auto it = m.begin(); it != m.end(); ++it) o[it.key().toStdString()] = aJson(it.value());
            return o;
        }
        case QMetaType::QVariantList: {
            json a = json::array();
            for (const QVariant& x : v.toList()) a.push_back(aJson(x));
            return a;
        }
        default: return v.toString().toStdString();
    }
}

/// Un id corto y único dentro del guion. No se muestra: sirve para que la
/// interfaz pueda seguir a un cue cuando se lo mueve de lugar.
std::string nuevoId(const char* prefijo) {
    static int n = 0;
    return std::string(prefijo) + std::to_string(++n) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
}

QStringList aLista(const std::vector<std::string>& v) {
    QStringList out;
    for (const auto& x : v) out << QString::fromStdString(x);
    return out;
}

QVariantMap cueAMap(const evento::Cue& c, int bloque, int cue) {
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

// La hora prevista de algo que arranca `offsetSeg` después de `inicio` («HH:MM»): «HH:MM»
// (da la vuelta a medianoche), o "" si no hay hora de inicio válida.
QString horaPrevista(const std::string& inicio, int offsetSeg) {
    int h = 0, m = 0;
    char sobra = 0;
    if (offsetSeg < 0 || std::sscanf(inicio.c_str(), "%d:%d%c", &h, &m, &sobra) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
        return {};
    const int total = h * 60 + m + offsetSeg / 60;
    return QString("%1:%2").arg((total / 60) % 24, 2, 10, QChar('0')).arg(total % 60, 2, 10, QChar('0'));
}

/// Lo que conviene confirmar a mano antes de una sesión (no hay cómo medirlo solo).
const std::vector<std::pair<const char*, const char*>>& itemsDeChequeo() {
    static const std::vector<std::pair<const char*, const char*>> v{
        {"ptz", QT_TRANSLATE_NOOP("evento", "PTZ probado con las cámaras reales (presets, que no se muevan al aire)")},
        {"casteo", QT_TRANSLATE_NOOP("evento", "Casteo probado en los televisores del recinto")},
        {"caida", QT_TRANSLATE_NOOP("evento", "Ensayo de caída: cerrar la app al aire y cronometrar cuánto tarda en volver")},
        {"glosario", QT_TRANSLATE_NOOP("evento", "Glosario y nombres revisados (integrantes, localidades)")},
        {"respaldo", QT_TRANSLATE_NOOP("evento", "Máquina de respaldo lista con la escena básica")},
        {"auriculares", QT_TRANSLATE_NOOP("evento", "Los invitados remotos con auriculares (o mix-minus probado)")},
        {"sonido", QT_TRANSLATE_NOOP("evento", "Prueba de sonido con cada micrófono de la sala")},
    };
    return v;
}

QVariantList chequeoDe(const json& guardado) {
    QVariantList out;
    std::set<std::string> vistos;
    const auto uno = [&](const std::string& id, const std::string& texto) {
        const json g = guardado.value(id, json::object());
        out << QVariantMap{{"item", QString::fromStdString(id)},
                           {"texto", QString::fromStdString(g.value("texto", texto))},
                           {"hecho", g.value("hecho", false)},
                           {"nota", QString::fromStdString(g.value("nota", std::string()))},
                           {"cuando", QString::fromStdString(g.value("cuando", std::string()))}};
        vistos.insert(id);
    };
    for (const auto& [id, texto] : itemsDeChequeo()) uno(id, ba::i18n::traducir("evento", texto));
    for (const auto& [id, g] : guardado.items())
        if (!vistos.count(id)) uno(id, g.value("texto", id));
    return out;
}

/// Lo que se lee del disco de la carpeta del evento (en el fondo).
struct Carpetas {
    QStringList audios, videos, discursos, dirs, archivos;
    std::map<QString, QString> textos;
    sala::ListaPrompters prompters;
    json chequeo = json::object();
};

Carpetas leerCarpetas(const std::filesystem::path& media, const QString& discursos, const std::filesystem::path& evento) {
    Carpetas c;
    // Los medios del evento (Media/), con su ruta relativa, por tipo.
    static const QStringList kAudio{"mp3", "wav", "ogg", "oga", "flac", "m4a", "aac", "opus", "aif", "aiff"};
    static const QStringList kVideo{"mp4", "mov", "mkv", "webm", "avi", "m4v", "mxf", "ts", "mpg", "mpeg"};
    const QDir m(QString::fromStdU16String(media.u16string()));
    if (m.exists()) {
        c.dirs << m.absolutePath();
        QDirIterator it(m.absolutePath(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) {
                c.dirs << f;
                continue;
            }
            const QString ext = fi.suffix().toLower();
            if (kAudio.contains(ext)) c.audios << m.relativeFilePath(f);
            else if (kVideo.contains(ext)) c.videos << m.relativeFilePath(f);
        }
        c.audios.sort(Qt::CaseInsensitive);
        c.videos.sort(Qt::CaseInsensitive);
    }
    if (!discursos.isEmpty()) {
        const Discursos docs(discursos);
        for (const QString& carpeta : docs.carpetas()) {
            c.dirs << QDir(discursos).filePath(carpeta);
            for (const QVariant& d : docs.listar(carpeta)) {
                const QString rel = d.toMap().value("rel").toString();
                bool ok = false;
                const QString texto = docs.leer(rel, &ok);
                c.discursos << rel;
                if (ok) c.textos[rel] = texto;
                c.archivos << docs.ruta(rel); // se edita en el lugar: se vigila el archivo
            }
        }
    }
    if (!evento.empty()) {
        c.dirs << QString::fromStdString(evento.string()); // prompters.json y chequeo.json (se escriben con renombre)
        c.prompters.cargar(evento / "prompters.json");
        std::string s;
        if (ba::persist::cola().leer(evento / "chequeo.json", s)) {
            const json j = json::parse(s, nullptr, false);
            if (j.is_object()) c.chequeo = j;
        }
    }
    return c;
}

} // namespace

GuionViewModel::GuionViewModel(EventoAbierto& abierto, AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala,
                               graficos::Emisor* graficos, QObject* parent)
    : QObject(parent), ab_(abierto), rt_(rt), ventanilla_(ventanilla), sala_(sala), graficos_(graficos) {
    fondo_.setMaxThreadCount(1);
    connect(&ab_, &EventoAbierto::guionCambio, this, &GuionViewModel::guionChanged);
    connect(&ab_, &EventoAbierto::sucioCambio, this, &GuionViewModel::sucioChanged);
    connect(&ab_, &EventoAbierto::abiertoCambio, this, [this]() {
        reparos_.clear();
        emit reparosChanged();
        leerCarpetas();
    });
    // Las guías son del operador (para él y el asistente): en su idioma. Una vez.
    escenarios_ = QJsonDocument::fromJson(QByteArray::fromStdString(ba::i18n::leer("guias/escenarios/indice.json"))).array().toVariantList();
    chequeo_ = chequeoDe(chequeoGuardado_);
    // Lo que cambia en las carpetas del evento (un medio nuevo, un discurso editado): se
    // relee, juntando los avisos seguidos.
    juntar_.setSingleShot(true);
    juntar_.setInterval(300);
    connect(&juntar_, &QTimer::timeout, this, &GuionViewModel::leerCarpetas);
    connect(&vigia_, &QFileSystemWatcher::directoryChanged, &juntar_, qOverload<>(&QTimer::start));
    connect(&vigia_, &QFileSystemWatcher::fileChanged, &juntar_, qOverload<>(&QTimer::start));
    // Lo que viene de sus dueños, cuando avisan.
    ventanilla_.escuchar(this, [this](unsigned p) {
        if ((p & (FotoMotor::Escenas | FotoMotor::Sala)) != 0) cambioOpciones();
    });
    if (graficos_ != nullptr) connect(graficos_, &graficos::Emisor::catalogoCambio, this, &GuionViewModel::cambioOpciones);
    // De la sala, los participantes (los oradores para elegir): solo cuando cambian, no con
    // cada foto.
    if (sala_ != nullptr) connect(sala_, &ConferenceViewModel::participantesChanged, this, &GuionViewModel::cambioOpciones);
    leerCarpetas();
}

GuionViewModel::~GuionViewModel() { fondo_.waitForDone(); }

void GuionViewModel::setCarruseles(CarruselViewModel* c) {
    carruseles_ = c;
    if (c != nullptr) connect(c, &CarruselViewModel::cambio, this, &GuionViewModel::cambioOpciones);
    cambioOpciones();
}

void GuionViewModel::cambioOpciones() {
    opcionesSucias_ = true;
    emit opcionesChanged();
}

void GuionViewModel::leerCarpetas() {
    juntar_.stop();
    const int gen = ++genCarpetas_;
    QPointer<GuionViewModel> yo(this);
    fondo_.start([yo, gen, media = rt_.dataRoot() / "Media", discursos = carpetaDeDiscursos(), evento = ab_.dir()]() {
        Carpetas c = ba::app::leerCarpetas(media, discursos, evento);
        QMetaObject::invokeMethod(
            yo,
            [yo, gen, c = std::move(c)]() mutable {
                if (!yo || gen != yo->genCarpetas_) return;
                yo->audios_ = std::move(c.audios);
                yo->videos_ = std::move(c.videos);
                yo->discursos_ = std::move(c.discursos);
                yo->textos_ = std::move(c.textos);
                yo->prompters_ = std::move(c.prompters);
                if (c.chequeo != yo->chequeoGuardado_) {
                    yo->chequeoGuardado_ = std::move(c.chequeo);
                    yo->chequeo_ = chequeoDe(yo->chequeoGuardado_);
                    emit yo->chequeoChanged();
                }
                // Se vigila lo que se leyó (solo lo que cambió de la lista).
                const QStringList d = yo->vigia_.directories(), f = yo->vigia_.files();
                if (!d.isEmpty()) yo->vigia_.removePaths(d);
                if (!f.isEmpty()) yo->vigia_.removePaths(f);
                if (!c.dirs.isEmpty()) yo->vigia_.addPaths(c.dirs);
                if (!c.archivos.isEmpty()) yo->vigia_.addPaths(c.archivos);
                yo->cambioOpciones();
            },
            Qt::QueuedConnection);
    });
}

// --- El guion -------------------------------------------------------------------------------

QString GuionViewModel::abierto() const { return ab_.carpeta(); }
bool GuionViewModel::sucio() const { return ab_.sucio(); }

QVariantMap GuionViewModel::guion() const {
    const evento::Evento& e = ab_.evento();
    QVariantList bs;
    int nro = 0;
    for (std::size_t bi = 0; bi < e.bloques.size(); ++bi) {
        const evento::Bloque& b = e.bloques[bi];
        QVariantList cs;
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            QVariantMap m = cueAMap(b.cues[ci], static_cast<int>(bi), static_cast<int>(ci));
            m["nro"] = ++nro; // numeración corrida, como un rundown de verdad
            m["offsetSeg"] = e.offsetDeCueSeg(bi, ci);
            m["horaPrevista"] = horaPrevista(e.inicio, e.offsetDeCueSeg(bi, ci));
            cs.append(m);
        }
        bs.append(QVariantMap{{"id", QString::fromStdString(b.id)},
                              {"nombre", QString::fromStdString(b.nombre)},
                              {"nota", QString::fromStdString(b.nota)},
                              {"duracionSeg", b.duracionSeg()},
                              {"offsetSeg", e.offsetDeBloqueSeg(bi)},
                              {"horaPrevista", horaPrevista(e.inicio, e.offsetDeBloqueSeg(bi))},
                              {"indice", static_cast<int>(bi)},
                              {"cues", cs}});
    }
    return QVariantMap{{"nombre", QString::fromStdString(e.nombre)},
                       {"fecha", QString::fromStdString(e.fecha)},
                       {"inicio", QString::fromStdString(e.inicio)},
                       {"huso", QString::fromStdString(e.huso)},
                       {"idioma", QString::fromStdString(e.idioma)},
                       {"cuando", cuando()},
                       {"nota", QString::fromStdString(e.nota)},
                       {"invitados", e.invitados},
                       {"preguntas", e.preguntas},
                       {"asamblea", e.asamblea},
                       {"duracionSeg", e.duracionSeg()},
                       {"cues", static_cast<int>(e.cantidadDeCues())},
                       {"bloques", bs}};
}

QVariantList GuionViewModel::tiposDeCue() const {
    QVariantList out;
    for (const evento::TipoDeCue& t : evento::tiposDeCue())
        out.append(QVariantMap{{"tipo", QString::fromUtf8(t.tipo)},
                               {"titulo", QCoreApplication::translate("evento", t.titulo)},
                               {"ayuda", QCoreApplication::translate("evento", t.ayuda)},
                               {"campos", aLista(t.campos)}});
    return out;
}

QVariantList GuionViewModel::idiomas() const {
    // "" = el del operador; después los que hay, con su nombre en su idioma.
    QVariantList out{QVariantMap{
        {"value", QString()},
        {"label", QString::fromStdString(ba::i18n::llenar(ba::i18n::traducir("evento", "El del programa (%1)"),
                                                          {ba::i18n::actual().nombre}))}}};
    for (const auto& p : ba::i18n::idiomas())
        out << QVariantMap{{"value", QString::fromStdString(p.codigo)}, {"label", QString::fromStdString(p.nombre)}};
    return out;
}

void GuionViewModel::editado() { ab_.editado(); }

void GuionViewModel::setNombre(const QString& nombre) {
    ab_.editar().nombre = nombre.toStdString();
    editado();
}
void GuionViewModel::setInicio(const QString& hhmm) {
    ab_.editar().inicio = hhmm.toStdString();
    editado();
}
void GuionViewModel::setFecha(const QString& aaaammdd) {
    ab_.editar().fecha = aaaammdd.toStdString();
    editado();
}
void GuionViewModel::setHuso(const QString& iana) {
    ab_.editar().huso = iana.toStdString();
    editado();
}
void GuionViewModel::setIdioma(const QString& codigo) {
    ab_.editar().idioma = codigo.toStdString();
    ba::i18n::usarEnEvento(ab_.evento().idioma);
    editado();
}
void GuionViewModel::setInvitados(bool si) {
    if (ab_.evento().invitados == si) return;
    ab_.editar().invitados = si;
    editado();
}
void GuionViewModel::setAsamblea(bool si) {
    if (ab_.evento().asamblea == si) return;
    ab_.editar().asamblea = si;
    editado();
}
void GuionViewModel::setPreguntas(bool si) {
    if (ab_.evento().preguntas == si) return;
    ab_.editar().preguntas = si;
    editado();
}

QString GuionViewModel::husoDelAparato() { return QString::fromUtf8(QTimeZone::systemTimeZoneId()); }

QString GuionViewModel::cuando() const { return ab_.cuando(); }

QString GuionViewModel::agregarBloque(const QString& nombre) {
    evento::Bloque b;
    b.id = nuevoId("b");
    b.nombre = nombre.isEmpty() ? QCoreApplication::translate("EventoViewModel", "Bloque nuevo").toStdString() : nombre.toStdString();
    ab_.editar().bloques.push_back(b);
    editado();
    return QString::fromStdString(b.id);
}

void GuionViewModel::renombrarBloque(int bloque, const QString& nombre) {
    auto& bs = ab_.editar().bloques;
    if (bloque < 0 || bloque >= static_cast<int>(bs.size())) return;
    bs[static_cast<std::size_t>(bloque)].nombre = nombre.toStdString();
    editado();
}

void GuionViewModel::quitarBloque(int bloque) {
    auto& bs = ab_.editar().bloques;
    if (bloque < 0 || bloque >= static_cast<int>(bs.size())) return;
    bs.erase(bs.begin() + bloque);
    editado();
}

void GuionViewModel::moverBloque(int desde, int hasta) {
    auto& bs = ab_.editar().bloques;
    const int n = static_cast<int>(bs.size());
    if (desde < 0 || desde >= n || hasta < 0 || hasta >= n || desde == hasta) return;
    evento::Bloque b = bs[static_cast<std::size_t>(desde)];
    bs.erase(bs.begin() + desde);
    bs.insert(bs.begin() + hasta, std::move(b));
    editado();
}

QString GuionViewModel::agregarCue(int bloque, const QString& tipo) {
    auto& bs = ab_.editar().bloques;
    if (bloque < 0 || bloque >= static_cast<int>(bs.size())) return {};
    evento::Cue c;
    c.id = nuevoId("c");
    if (!evento::tipoDeNombre(tipo.toStdString(), c.tipo)) c.tipo = evento::Tipo::Nota;
    bs[static_cast<std::size_t>(bloque)].cues.push_back(c);
    editado();
    return QString::fromStdString(c.id);
}

void GuionViewModel::editarCue(int bloque, int cue, const QVariantMap& campos) {
    evento::Cue* c = ab_.cue(bloque, cue);
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
        const json antes = c->datos;
        const QVariantMap d = campos["datos"].toMap();
        for (auto it = d.begin(); it != d.end(); ++it) {
            const std::string clave = it.key().toStdString();
            if (it.value().toString().isEmpty() && it.value().typeId() == QMetaType::QString) c->datos.erase(clave);
            else c->datos[clave] = aJson(it.value());
        }
        escenas::soltarIdViejo(antes, c->datos); // otra escena: el id de la de antes se va
    }
    c->normalizarDatos();
    editado();
}

void GuionViewModel::quitarCue(int bloque, int cue) {
    auto& bs = ab_.editar().bloques;
    if (bloque < 0 || bloque >= static_cast<int>(bs.size())) return;
    auto& cues = bs[static_cast<std::size_t>(bloque)].cues;
    if (cue < 0 || cue >= static_cast<int>(cues.size())) return;
    cues.erase(cues.begin() + cue);
    editado();
}

void GuionViewModel::moverCue(int deBloque, int deCue, int aBloque, int aCue) {
    const evento::Cue* origen = ab_.cue(deBloque, deCue);
    auto& bs = ab_.editar().bloques;
    if (origen == nullptr || aBloque < 0 || aBloque >= static_cast<int>(bs.size())) return;
    const evento::Cue copia = *origen;
    auto& desde = bs[static_cast<std::size_t>(deBloque)].cues;
    desde.erase(desde.begin() + deCue);
    auto& hasta = bs[static_cast<std::size_t>(aBloque)].cues;
    const int destino = std::clamp(aCue, 0, static_cast<int>(hasta.size()));
    hasta.insert(hasta.begin() + destino, copia);
    editado();
}

void GuionViewModel::setValorDeCue(int bloque, int cue, const QString& clave, const QVariant& valor) {
    evento::Cue* c = ab_.cue(bloque, cue);
    if (c == nullptr || clave.isEmpty()) return;
    if (!c->datos.contains("valores") || !c->datos["valores"].is_object()) c->datos["valores"] = json::object();
    c->datos["valores"][clave.toStdString()] = valor.toString().toStdString();
    editado();
}

bool GuionViewModel::guardar() {
    if (!ab_.hay()) return false;
    if (const std::string e = ab_.guardar(); !e.empty()) {
        emit notify(QCoreApplication::translate("EventoViewModel", "No se pudo guardar el guion: %1").arg(QString::fromStdString(e)), "error");
        return false;
    }
    emit guardado();
    return true;
}

// --- La revisión previa y el chequeo ---------------------------------------------------------

QVariantList GuionViewModel::reparos() const {
    QVariantList out;
    for (const evento::Reparo& r : reparos_)
        out.append(QVariantMap{{"bloque", r.bloque}, {"cue", r.cue}, {"grave", r.grave}, {"texto", QString::fromStdString(r.texto)}});
    return out;
}

void GuionViewModel::limpiarReparos() {
    reparos_.clear();
    emit reparosChanged();
}

int GuionViewModel::graves() const {
    return static_cast<int>(std::count_if(reparos_.begin(), reparos_.end(), [](const evento::Reparo& r) { return r.grave; }));
}

void GuionViewModel::revisar() {
    const evento::Evento& e = ab_.evento();
    reparos_ = evento::revisar(e);
    // Lo que el modelo no puede saber solo: si las escenas existen de verdad.
    const std::vector<std::string> escenas = ventanilla_.foto().nombresDeEscenas();
    const auto qs = [](const std::string& s) { return QString::fromStdString(s); };
    for (std::size_t bi = 0; bi < e.bloques.size(); ++bi) {
        const evento::Bloque& b = e.bloques[bi];
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            const evento::Cue& c = b.cues[ci];
            if (c.tipo == evento::Tipo::Grafico && graficos_ != nullptr) {
                const std::string g = c.datos.value("grafico", std::string());
                if (!g.empty() && !graficos_->nombres().contains(QString::fromStdString(g)))
                    reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                        QCoreApplication::translate("EventoViewModel", "no existe un gráfico llamado \"%1\"").arg(qs(g)).toStdString()});
                // La fuente se marca SIN gravedad: es normal escribir el guion
                // antes de armar las escenas, y se arregla agregando la fuente.
                const std::string f = c.datos.value("fuente", std::string());
                if (!f.empty() && graficos_->fuentePorNombre(QString::fromStdString(f)) == 0)
                    reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), false,
                                        QCoreApplication::translate("EventoViewModel", "todavía no hay ninguna fuente de gráfico llamada \"%1\": agregala a la "
                                           "escena donde tiene que verse")
                                            .arg(qs(f))
                                            .toStdString()});
                continue;
            }
            if (c.tipo != evento::Tipo::Escena) continue;
            const std::string nombre = c.datos.value("escena", std::string());
            if (nombre.empty()) continue; // ya lo dijo la revisión del modelo
            if (std::find(escenas.begin(), escenas.end(), nombre) == escenas.end())
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                    QCoreApplication::translate("EventoViewModel", "no existe una escena llamada \"%1\"").arg(qs(nombre)).toStdString()});
        }
    }
    // Y lo que hay que ir a mirar al disco: los archivos que el guion nombra (se revisa a
    // pedido, no al dibujar).
    if (ab_.hay())
        for (const evento::Reparo& r : evento::revisarArchivos(e, ab_.dir(), rt_.dataRoot() / "Media")) reparos_.push_back(r);
    // Lo del aparato: disco, destino de la transmisión, entradas con señal,
    // pantallas vivas (sin bloque: es de todo el evento).
    if (chequeosAparato_)
        for (const QVariant& v : chequeosAparato_()) {
            const QVariantMap m = v.toMap();
            reparos_.push_back({-1, -1, m.value("grave").toBool(), m.value("texto").toString().toStdString()});
        }
    // Los cues de un tipo que el programa no ejecuta (la misma lista que el ensayo).
    for (std::size_t bi = 0; bi < e.bloques.size(); ++bi)
        for (std::size_t ci = 0; ci < e.bloques[bi].cues.size(); ++ci)
            if (!evento::ejecutable(e.bloques[bi].cues[ci].tipo))
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), false,
                                    QCoreApplication::translate("EventoViewModel", "el programa todavía no sabe ejecutar este tipo de cue").toStdString()});
    emit reparosChanged();

    const int g = graves();
    if (reparos_.empty()) emit notify(QCoreApplication::translate("EventoViewModel", "La revisión no encontró problemas"), "ok");
    else if (g > 0) emit notify(QCoreApplication::translate("EventoViewModel", "La revisión encontró %n problema que impide correr el guion", "", g), "error");
    else emit notify(QCoreApplication::translate("EventoViewModel", "La revisión encontró cosas para mirar, ninguna grave"), "info");
}

bool GuionViewModel::marcarChequeo(const QString& item, bool hecho, const QString& nota, const QString& texto) {
    if (!ab_.hay() || item.trimmed().isEmpty()) return false;
    json j = chequeoGuardado_;
    const std::string id = item.trimmed().toStdString();
    json& g = j[id];
    if (!g.is_object()) g = json::object();
    bool conocido = false;
    for (const auto& [k, t] : itemsDeChequeo())
        if (id == k) conocido = true;
    if (!conocido && !texto.trimmed().isEmpty()) g["texto"] = texto.trimmed().toStdString();
    else if (!conocido && !g.contains("texto")) g["texto"] = id;
    g["hecho"] = hecho;
    g["nota"] = nota.toStdString();
    g["cuando"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString();
    // Lo escribe el hilo escritor; la caché ya lo tiene.
    ba::persist::cola().escribir(ab_.dir() / "chequeo.json", j.dump(1, ' ', false, json::error_handler_t::replace));
    chequeoGuardado_ = std::move(j);
    chequeo_ = chequeoDe(chequeoGuardado_);
    emit chequeoChanged();
    return true;
}

// --- Para armar un cue ------------------------------------------------------------------------

QStringList GuionViewModel::graficosDisponibles() const { return graficos_ == nullptr ? QStringList{} : graficos_->nombres(); }

QStringList GuionViewModel::fuentesDeGrafico() const {
    QStringList out;
    for (const auto& i : app::graficos::instancias()) out << QString::fromStdString(i.nombre);
    return out;
}

QVariantList GuionViewModel::parametrosDeGrafico(const QString& grafico) const {
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

QVariantMap GuionViewModel::opciones() const {
    if (!opcionesSucias_) return opciones_;
    QVariantMap m;
    const auto lista = [](std::initializer_list<std::pair<QString, QString>> l) {
        QVariantList out;
        for (const auto& [v, t] : l) out.append(QVariantMap{{"valor", v}, {"texto", t.isEmpty() ? v : t}});
        return out;
    };
    const auto deNombres = [](const QStringList& l) {
        QVariantList out;
        for (const QString& v : l) out.append(QVariantMap{{"valor", v}, {"texto", v}});
        return out;
    };
    const QVariantList arrancarParar = lista({{"arrancar", QCoreApplication::translate("EventoViewModel", "Arrancar")}, {"parar", QCoreApplication::translate("EventoViewModel", "Parar")}});
    m["musica/accion"] = arrancarParar;
    m["video/accion"] = arrancarParar;
    m["grafico/accion"] = lista({{"mostrar", QCoreApplication::translate("EventoViewModel", "Mostrar")},
                                 {"ocultar", QCoreApplication::translate("EventoViewModel", "Sacar")},
                                 {"prender", QCoreApplication::translate("EventoViewModel", "Prender el rótulo automático")},
                                 {"apagar", QCoreApplication::translate("EventoViewModel", "Apagar el rótulo automático")}});
    m["audio/accion"] = lista({{"silenciar", QCoreApplication::translate("EventoViewModel", "Silenciar")}, {"activar", QCoreApplication::translate("EventoViewModel", "Activar")}, {"volumen", QCoreApplication::translate("EventoViewModel", "Cambiar el volumen")}});
    QStringList escenas;
    for (const std::string& e : ventanilla_.foto().nombresDeEscenas()) escenas << QString::fromStdString(e);
    m["escena/escena"] = deNombres(escenas);
    if (carruseles_ != nullptr) {
        const QStringList cs = carruseles_->nombres();
        m["carrusel/carrusel"] = deNombres(cs);
        // Las del carrusel elegido en el cue, con su título: «3 · El derrumbe micénico».
        for (const QString& c : cs) {
            QVariantList ls;
            for (const QVariant& v : carruseles_->laminasDe(c)) {
                const QVariantMap l = v.toMap();
                ls.append(QVariantMap{{"valor", QString::number(l.value("numero").toInt())},
                                      {"texto", QCoreApplication::translate("EventoViewModel", "%1 · %2").arg(l.value("numero").toInt()).arg(l.value("titulo").toString())}});
            }
            m["carrusel/lamina:" + c] = ls;
        }
    }
    m["musica/archivo"] = deNombres(audios_);
    m["video/archivo"] = deNombres(videos_);
    m["teleprompter/discurso"] = deNombres(discursos_);
    {
        // Quien está en la sala ahora (los que llegan después se pueden escribir).
        QVariantList ps;
        if (sala_ != nullptr)
            for (const QJsonValue& v : sala_->client().snapshot().value("participants").toArray()) {
                const QJsonObject p = v.toObject();
                const QString id = p.value("id").toString();
                if (!id.isEmpty())
                    ps.append(QVariantMap{{"valor", id}, {"texto", p.value("name").toString().isEmpty() ? id : p.value("name").toString()}});
            }
        m["orador/participante"] = ps;
    }
    m["grafico/seVa"] = lista({{"", QCoreApplication::translate("EventoViewModel", "Lo que diga el gráfico")},
                               {"al-avanzar", QCoreApplication::translate("EventoViewModel", "Al avanzar a otra cue")},
                               {"sola", QCoreApplication::translate("EventoViewModel", "Sola (a los «cuándo se va» ms)")},
                               {"queda", QCoreApplication::translate("EventoViewModel", "Queda hasta que la saquen")}});
    const QStringList graficos = graficosDisponibles();
    m["grafico/grafico"] = deNombres(graficos);
    for (const QString& g : graficos) m["parametros:" + g] = parametrosDeGrafico(g);
    m["grafico/fuente"] = deNombres(fuentesDeGrafico());
    {
        QStringList audio;
        for (const std::string& f : ventanilla_.foto().sala.audio) audio << QString::fromStdString(f);
        m["audio/fuente"] = deNombres(audio);
    }
    {
        // Los teleprompters del evento (cada uno con su página): el principal primero.
        QVariantList ps;
        for (const auto& p : prompters_.lista())
            ps.append(QVariantMap{{"valor", QString::fromStdString(p.value("id", std::string()))},
                                  {"texto", QString::fromStdString(p.value("nombre", std::string()))}});
        m["teleprompter/prompter"] = ps;
    }
    m["pantalla/contenido"] = lista({{"programa", QCoreApplication::translate("EventoViewModel", "El programa (lo que sale al aire)")},
                                     {"reloj", QCoreApplication::translate("EventoViewModel", "Un reloj")},
                                     {"prompter", QCoreApplication::translate("EventoViewModel", "El teleprompter")},
                                     {"tally", QCoreApplication::translate("EventoViewModel", "Quién está al aire")},
                                     {"votacion", QCoreApplication::translate("EventoViewModel", "La votación")}});
    opciones_ = m;
    opcionesSucias_ = false;
    return opciones_;
}

QVariantList GuionViewModel::opcionesDeCampo(const QString& tipo, const QString& campo, const QVariantMap& datos) const {
    const QVariantMap m = opciones();
    if (campo == "escena") return m.value("escena/escena").toList();
    if (campo == "lamina") return m.value("carrusel/lamina:" + datos.value("carrusel").toString()).toList();
    return m.value(tipo + "/" + campo).toList();
}

QString GuionViewModel::enlaceDeCue(const QString& tipo, const QVariantMap& datos) const {
    // El servidor de la sala, en esta máquina (la pantalla del orador solo lee: sin clave).
    // Cada teleprompter tiene su página: el principal en /prompter/, los demás en /prompter/<id>/.
    if (tipo == "teleprompter") {
        const QString id = datos.value("prompter").toString();
        const QString carpeta =
            id.isEmpty() || id == sala::ListaPrompters::kPrincipal || !prompters_.existe(id.toStdString()) ? QString() : id + "/";
        return "http://127.0.0.1:" + QString::number(ventanilla_.foto().sala.puerto) + "/prompter/" + carpeta;
    }
    return {};
}

QVariantMap GuionViewModel::enlacesPrompter() const {
    QVariantMap m{{QString(), enlaceDeCue("teleprompter", {})}};
    for (const auto& p : prompters_.lista()) {
        const QString id = QString::fromStdString(p.value("id", std::string()));
        m[id] = enlaceDeCue("teleprompter", {{"prompter", id}});
    }
    return m;
}

QString GuionViewModel::pedidoDeEscenario(const QString& id, const QString& notas, const QString& plantilla) const {
    QString titulo = id;
    for (const QVariant& v : escenarios_)
        if (v.toMap().value("id").toString() == id) titulo = v.toMap().value("titulo").toString();
    QString p = QCoreApplication::translate(
                    "asistente",
                    "Quiero preparar un evento nuevo: «%1». Seguí la guía de ese tipo de evento: evento_escenario "
                    "{\"id\": \"%2\"} (trae el protocolo común y lo propio de este tipo). Empezá por las preguntas, "
                    "de a pocas y con opciones; no armes nada hasta que te confirme el plan. ")
                    .arg(titulo, id);
    if (plantilla.isEmpty())
        p += QCoreApplication::translate("asistente", "Arrancalo DESDE CERO (un evento aparte, con evento_nuevo). ");
    else
        p += QCoreApplication::translate(
                 "asistente",
                 "Crealo desde su plantilla (evento_nuevo con desde «plantilla:%1»): ya trae escenas, gráficos, "
                 "guion con los textos para completar marcados ‹así›, discursos de ejemplo y lo propio del tipo; ajustalo con "
                 "lo que te cuente (reemplazá cada ‹…›, sacá lo que no se use, agregá lo que falte). ")
                 .arg(plantilla);
    p += QCoreApplication::translate(
        "asistente",
        "Verificá todo antes de mandar invitaciones, sugerime el ensayo y al terminar pasame el enlace de la página web "
        "del evento.");
    if (!notas.trimmed().isEmpty()) p += QCoreApplication::translate("asistente", " Lo que ya sé: %1").arg(notas.trimmed());
    return p;
}

// --- Las carpetas del evento -----------------------------------------------------------------

QString GuionViewModel::carpetaDeDiscursos() const {
    return ab_.hay() ? QString::fromStdString(ab_.sub("Discursos").string()) : QString();
}

QString GuionViewModel::rutaDeDiscurso(const QString& rel) const { return ab_.dentro("Discursos", rel); }

QString GuionViewModel::textoDeDiscurso(const QString& rel, bool* ok) const {
    // Los guiones nombran el discurso como lo guarda Discursos ("carpeta/nombre.txt").
    QString limpio = rel;
    if (limpio.startsWith("Discursos/")) limpio = limpio.mid(10);
    const auto it = textos_.find(limpio);
    if (ok != nullptr) *ok = it != textos_.end();
    return it == textos_.end() ? QString() : it->second;
}

QString GuionViewModel::medioPath(const QString& rel) const {
    // Los medios del evento son su biblioteca (Media/, la de la página Medios: cada
    // evento tiene la suya). Los guiones de antes nombraban Medios/ de la carpeta del
    // guion: si no está en Media/, se busca ahí.
    if (rel.isEmpty()) return {};
    QString limpio = rel;
    for (const QString& pre : {QStringLiteral("Media/"), QStringLiteral("Medios/"), QStringLiteral("Media\\"), QStringLiteral("Medios\\")})
        if (limpio.startsWith(pre)) limpio = limpio.mid(pre.size());
    const QDir media(QString::fromStdString((rt_.dataRoot() / "Media").string()));
    const QString abs = QDir::cleanPath(media.filePath(limpio));
    // Fuera de la biblioteca no se lee nada (un «..» en un guion que vino de otro lado).
    if (abs.startsWith(QDir::cleanPath(media.absolutePath()) + "/") && QFileInfo::exists(abs)) return abs;
    return ab_.dentro("Medios", rel);
}

} // namespace ba::app
