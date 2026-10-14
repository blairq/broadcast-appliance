// ============================================================================
// src/app/ui/EventoViewModel.cpp — ver EventoViewModel.hpp
// ============================================================================
#include "ui/EventoViewModel.hpp"

#include "evento/Documentos.hpp"

#include <cstdio>
#include "sala/ControlSala.hpp"
#include <QPointer>
#include "ajustes/Ajustes.hpp"

#include <set>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cmath>

#include <QDateTime>
#include <QPointer>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "emision/BroadcastEngine.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/Discursos.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/OradoresSala.hpp"
#include "sala/Teleprompter.hpp"
#include "evento/EventoWeb.hpp"
#include "platform/Paths.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;

int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

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
    {"musica", "Música", "Arranca o para un tema de los medios del evento.", "archivo,accion,repetir"},
    {"video", "Video", "Reproduce un video de los medios del evento en su propia escena.", "archivo,accion,repetir,alAire"},
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

EventoViewModel::EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala, graficos::Emisor* graficos,
                                 bool sinAbrirUltimo, QObject* parent)
    : QObject(parent), rt_(rt), sala_(sala), graficos_(graficos), store_(rt.dataRoot() / "Eventos") {
    // El reloj del evento va aparte del guion: cambia cada segundo y no tiene
    // sentido redibujar la lista de cues por eso.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() { emit relojChanged(); });
    // Las plantillas de la página web del evento (Web/evento/plantilla del programa): ANTES
    // de abrir el último evento, que al abrirse actualiza sus páginas con ellas.
    evento::setCarpetaPlantillas(ba::platform::bundleRoot() / "Web" / "evento" / "plantilla");
    // Se abre solo el evento que se guardo ultimo: el operador trabaja en el
    // mismo durante dias, y abrir el programa para encontrarlo vacio es una
    // molestia diaria. No hace falta guardar "el ultimo abierto" en ningun
    // lado: la fecha del archivo ya lo dice.
    const std::string ultimo = sinAbrirUltimo ? std::string() : store_.ultimoTocado();
    if (!ultimo.empty()) abrir(QString::fromStdString(ultimo));
    // EL EVENTO EN CURSO: el que quedó anotado sigue (un corte de luz no lo
    // corta); si no hay ninguno y está prendido el automático, el del día. Cada
    // minuto se mira de nuevo: a la medianoche el automático pasa al de hoy.
    {
        std::ifstream f(rt.dataRoot() / "Config" / "historico.json");
        const json j = f ? json::parse(f, nullptr, false) : json();
        if (j.is_object()) automatico_ = j.value("evento_automatico", true);
    }
    asegurarEnCurso();
    vigiaEnCurso_.setInterval(ba::ajustes::sAMs("evento.vigiaS", 60));
    connect(&vigiaEnCurso_, &QTimer::timeout, this, &EventoViewModel::asegurarEnCurso);
    vigiaEnCurso_.start();
    // La página web del evento en vivo: a la sala cuando cambia algo y cada 10 s.
    webTimer_.setInterval(ba::ajustes::sAMs("evento.webReenvioS", 10));
    connect(&webTimer_, &QTimer::timeout, this, [this]() { webUltimo_.clear(); publicarWeb(); });
    webTimer_.start();
    connect(this, &EventoViewModel::correChanged, this, &EventoViewModel::publicarWeb);
    connect(this, &EventoViewModel::guionChanged, this, [this]() { emit webChanged(); publicarWeb(); });
    refrescar();
}

// --- La página web del evento en vivo -------------------------------------------

QString EventoViewModel::carpetaWebAbs() const {
    if (abierto_.isEmpty()) return {};
    return QString::fromStdWString(evento::carpetaWeb(store_.carpetaDe(abierto_.toStdString())).wstring());
}

QVariantMap EventoViewModel::web() const {
    if (abierto_.isEmpty()) return {{"hay", false}};
    const std::filesystem::path dir = carpetaWebAbs().toStdWString();
    std::error_code ec;
    const bool hay = std::filesystem::exists(dir / "portada.html", ec);
    QVariantList paginas;
    paginas << QVariantMap{{"pagina", "portada.html"}, {"nombre", "Portada (antes de empezar)"},
                           {"existe", std::filesystem::exists(dir / "portada.html", ec)}};
    for (std::size_t i = 0; i < e_.bloques.size(); ++i) {
        const std::string p = evento::paginaDeBloque(e_.bloques[i], i);
        paginas << QVariantMap{{"pagina", QString::fromStdString(p)},
                               {"nombre", QString::fromStdString(e_.bloques[i].nombre)},
                               {"existe", std::filesystem::exists(dir / p, ec)}};
    }
    paginas << QVariantMap{{"pagina", "cierre.html"}, {"nombre", "Cierre (al terminar)"},
                           {"existe", std::filesystem::exists(dir / "cierre.html", ec)}};
    const evento::SitioWeb s = evento::SitioWeb::leer(dir, e_);
    return {{"hay", hay},
            {"carpeta", carpetaWebAbs()},
            {"paginas", paginas},
            {"sitio", QJsonDocument::fromJson(QByteArray::fromStdString(s.aJson().dump())).object().toVariantMap()},
            {"aviso", avisoWeb()}};
}

QVariantMap EventoViewModel::generarWeb(bool sobrescribir) {
    if (abierto_.isEmpty()) return {{"error", "No hay ningún evento abierto."}};
    const evento::ResultadoWeb r = evento::generar(e_, carpetaWebAbs().toStdWString(), sobrescribir);
    QStringList esc, sal;
    for (const auto& x : r.escritas) esc << QString::fromStdString(x);
    for (const auto& x : r.salteadas) sal << QString::fromStdString(x);
    ++webVersion_;
    webUltimo_.clear();
    emit webChanged();
    publicarWeb();
    if (!r.error.empty()) return {{"error", QString::fromStdString(r.error)}, {"escritas", esc}, {"salteadas", sal}};
    anotar("Página web del evento: " + QString::number(esc.size()) + " archivos generados", "evento");
    return {{"escritas", esc}, {"salteadas", sal}, {"carpeta", carpetaWebAbs()}};
}

QString EventoViewModel::setWebSitio(const QVariantMap& campos) {
    if (abierto_.isEmpty()) return "No hay ningún evento abierto.";
    const std::filesystem::path dir = carpetaWebAbs().toStdWString();
    evento::SitioWeb s = evento::SitioWeb::leer(dir, e_);
    if (campos.contains("titulo")) s.titulo = campos.value("titulo").toString().toStdString();
    if (campos.contains("bajada")) s.bajada = campos.value("bajada").toString().toStdString();
    if (campos.contains("color")) s.color = campos.value("color").toString().toStdString();
    if (campos.contains("fondo")) s.fondo = campos.value("fondo").toString().toStdString();
    if (campos.contains("barra")) s.barra = campos.value("barra").toBool();
    if (campos.contains("direccion_al_iniciar")) s.direccionAlIniciar = campos.value("direccion_al_iniciar").toBool();
    if (campos.contains("participacion")) s.participacion = campos.value("participacion").toBool();
    if (campos.contains("vivos")) {
        const QVariantMap v = campos.value("vivos").toMap();
        for (auto it = v.begin(); it != v.end(); ++it) {
            const std::string k = it.key().toStdString();
            if (k != "youtube" && k != "twitch" && k != "facebook") continue;
            const std::string val = it.value().toString().trimmed().toStdString();
            if (val.empty()) s.vivos.erase(k);
            else s.vivos[k] = val;
        }
    }
    if (campos.contains("subdominio")) {
        const std::string sub = evento::subdominioDe(campos.value("subdominio").toString().toStdString());
        if (sub.empty() && !campos.value("subdominio").toString().trimmed().isEmpty())
            return "El subdominio va con letras, números y guiones.";
        s.subdominio = sub;
    }
    if (!s.guardar(dir)) return "No se pudo guardar sitio.json.";
    ++webVersion_;
    webUltimo_.clear();
    emit webChanged();
    publicarWeb();
    return {};
}

QString EventoViewModel::leerPaginaWeb(const QString& pagina) const {
    if (abierto_.isEmpty()) return {};
    const std::string rel = evento::resolverPagina(e_, pagina.toStdString());
    if (rel.empty()) return {};
    QFile f(carpetaWebAbs() + "/" + QString::fromStdString(rel));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString EventoViewModel::escribirPaginaWeb(const QString& pagina, const QString& contenido) {
    if (abierto_.isEmpty()) return "No hay ningún evento abierto.";
    const std::string rel = evento::resolverPagina(e_, pagina.toStdString());
    if (rel.empty()) return "No conozco la página «" + pagina + "»: portada, cierre, una sección o un .html propio.";
    QDir().mkpath(carpetaWebAbs());
    QFile f(carpetaWebAbs() + "/" + QString::fromStdString(rel));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "No se pudo escribir " + QString::fromStdString(rel);
    f.write(contenido.toUtf8());
    f.close();
    ++webVersion_;
    webUltimo_.clear();
    emit webChanged();
    publicarWeb();
    return {};
}

void EventoViewModel::setAvisoWeb(const QString& texto, const QString& nivel) {
    const QString t = texto.trimmed().left(280);
    webAviso_ = t.isEmpty() ? json::object()
                            : json{{"texto", t.toStdString()},
                                   {"nivel", nivel == "urgente" ? "urgente" : "info"},
                                   {"desdeMs", QDateTime::currentMSecsSinceEpoch()}};
    if (!t.isEmpty()) anotar("Aviso en la página del evento: " + t, "evento");
    emit webChanged();
    publicarWeb();
}

QVariantMap EventoViewModel::avisoWeb() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(webAviso_.dump())).object().toVariantMap();
}

void EventoViewModel::publicarWeb() {
    if (sala_ == nullptr) return;
    json cuerpo{{"activo", false}};
    const QString dir = carpetaWebAbs();
    std::error_code ec;
    if (!dir.isEmpty() && std::filesystem::exists(std::filesystem::path(dir.toStdWString()) / "portada.html", ec)) {
        // El estado público: en qué sección va (nada de cues ni notas).
        std::string est = "proximo";
        int bloque = -1;
        if (guionCorriendo_) {
            switch (guionCorriendo_->estado()) {
                case evento::Guion::Estado::Corriendo: est = "en_vivo"; break;
                case evento::Guion::Estado::Pausado: est = "pausa"; break;
                case evento::Guion::Estado::Terminado: est = "terminado"; break;
                default: break;
            }
            if (est == "en_vivo" || est == "pausa") bloque = guionCorriendo_->posicion().bloque;
        }
        if (ensayo_) est = "proximo"; // un ensayo no se publica
        if (est == "proximo" && store_.datos(abierto_.toStdString()).estado == "terminado") est = "terminado";
        if ((est == "en_vivo" || est == "pausa") && webArranqueMs_ == 0) webArranqueMs_ = QDateTime::currentMSecsSinceEpoch();
        if (est == "proximo") webArranqueMs_ = 0;
        qint64 inicioMs = 0;
        if (!e_.fecha.empty() && !e_.inicio.empty()) {
            const QTimeZone tz = e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
            const QDateTime dt(QDate::fromString(QString::fromStdString(e_.fecha), "yyyy-MM-dd"),
                               QTime::fromString(QString::fromStdString(e_.inicio), "HH:mm"), tz.isValid() ? tz : QTimeZone::systemTimeZone());
            if (dt.isValid()) inicioMs = dt.toMSecsSinceEpoch();
        }
        const evento::SitioWeb s = evento::SitioWeb::leer(dir.toStdWString(), e_);
        evento::ExtrasVivo x;
        x.vivos = evento::armarVivos(s, plataformas_ ? plataformas_() : json::object());
        x.aviso = webAviso_;
        if (preguntas_) x.preguntas = preguntas_();
        if (enLaSala_) x.enLaSala = enLaSala_();
        if (datosSala_) x.sala = datosSala_();
        {
            // La asistencia sale del padrón del evento (cuántos se anotaron y cuántos llegaron).
            const QVariantMap r = resumenPadron();
            if (r.value("total").toInt() > 0)
                x.sala["asistencia"] = {{"total", r.value("total").toInt()}, {"presentes", r.value("presentes").toInt()}};
        }
        // Cuándo empezó de verdad cada sección (para el horario estimado de las que siguen).
        if (est == "proximo") webInicioReal_.clear();
        if (bloque >= 0 && !webInicioReal_.count(bloque)) webInicioReal_[bloque] = QDateTime::currentMSecsSinceEpoch();
        for (const auto& [b, ms] : webInicioReal_) x.inicioReal[b] = ms;
        if (guionCorriendo_ && bloque >= 0) {
            // Lo que le falta a la sección actual según lo previsto, y quién habla (si el cue
            // que corre es de orador: el que tiene la palabra se ve al aire, no es privado).
            const evento::Posicion p = guionCorriendo_->posicion();
            const auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
            int falta = 0;
            for (int c = p.cue; c < static_cast<int>(cues.size()); ++c)
                falta += cues[static_cast<std::size_t>(c)].duracionSeg;
            x.restanteActualSeg = std::max(0, falta - guionCorriendo_->enCueSeg(ahoraMs()));
            if (const evento::Cue* c = guionCorriendo_->cueActual(); c != nullptr && c->tipo == evento::Tipo::Orador)
                x.habla = c->datos.value("participante", std::string());
        }
        json estado = evento::estadoPublico(e_, s, est, bloque, inicioMs, webArranqueMs_, 0, x);
        estado["version"] = webVersion_;
        // La documentación pública del evento (docs/): la web la muestra y la sala la sirve.
        const evento::Documentos docs(rt_.dataRoot());
        estado["documentos"] = docs.publicos();
        // Para la sala (no se publica): el archivo de cada documento público, por su id.
        json docsArchivos = json::object();
        for (const auto& d : docs.lista())
            if (d.value("publico", true)) docsArchivos[d.value("id", std::string())] = d.value("archivo", std::string());
        cuerpo = {{"activo", true}, {"dir", dir.toStdString()}, {"docs_archivos", docsArchivos}, {"docs_dir", [&] {
                      const std::u8string u = docs.carpeta().u8string();
                      return std::string(u.begin(), u.end());
                  }()}, {"estado", estado}};
    }
    const QByteArray b = QByteArray::fromStdString(cuerpo.dump());
    if (b == webUltimo_) return;
    webUltimo_ = b;
    // La hora de la sala va aparte (cambia siempre y no tiene que forzar el reenvío).
    json conHora = cuerpo;
    if (conHora.contains("estado")) conHora["estado"]["ahoraMs"] = QDateTime::currentMSecsSinceEpoch();
    sala_->control().publicarEventoWeb(conHora);
}

EventoViewModel::~EventoViewModel() = default;

// --- La lista ---------------------------------------------------------------

namespace {
QVariantMap datosAMap(const evento::DatosDelEvento& d) {
    QStringList etiquetas;
    for (const auto& e : d.etiquetas) etiquetas << QString::fromStdString(e);
    return QVariantMap{{"estado", QString::fromStdString(d.estado)},
                       {"iniciadoMs", static_cast<qint64>(d.iniciadoMs)},
                       {"terminadoMs", static_cast<qint64>(d.terminadoMs)},
                       {"lugar", QString::fromStdString(d.lugar)},
                       {"etiquetas", etiquetas},
                       {"notas", QString::fromStdString(d.notas)},
                       {"automatico", d.automatico},
                       {"dia", QString::fromStdString(d.dia)}};
}
} // namespace

QVariantMap EventoViewModel::enCurso() const {
    const std::string c = store_.enCurso();
    if (c.empty()) return {};
    const evento::Evento e = store_.cargar(c);
    const evento::DatosDelEvento d = store_.datos(c);
    const auto barra = c.rfind('/');
    return QVariantMap{{"carpeta", QString::fromStdString(c)},
                       {"nombre", QString::fromStdString(e.nombre.empty() ? c : e.nombre)},
                       {"grupo", QString::fromStdString(barra == std::string::npos ? std::string() : c.substr(0, barra))},
                       {"desdeMs", static_cast<qint64>(d.iniciadoMs)},
                       {"automatico", d.automatico}};
}

std::filesystem::path EventoViewModel::archivoBitacora() const { return archivoBitacoraDe({}); }

std::filesystem::path EventoViewModel::archivoBitacoraDe(const std::string& carpeta) const {
    const auto d = carpetaEvento(carpeta);
    return d.empty() ? d : d / "bitacora.jsonl";
}

std::filesystem::path EventoViewModel::carpetaEvento(const std::string& carpeta) const {
    const std::string c = carpeta.empty() ? store_.enCurso() : carpeta;
    if (c.empty() || !evento::EventoStore::rutaValida(c)) return {};
    std::error_code ec;
    if (!std::filesystem::is_directory(store_.carpetaDe(c), ec)) return {};
    return store_.carpetaDe(c);
}

std::filesystem::path EventoViewModel::carpetaConfig() const { return rt_.dataRoot() / "Config"; }

nlohmann::json EventoViewModel::ajustesHistorico() const {
    std::ifstream f(rt_.dataRoot() / "Config" / "historico.json");
    const json j = f ? json::parse(f, nullptr, false) : json();
    return j.is_object() ? j : json::object();
}

QStringList EventoViewModel::carpetas() const {
    QStringList out;
    for (const auto& c : store_.carpetas()) out << QString::fromStdString(c);
    return out;
}

void EventoViewModel::setEventoAutomatico(bool si) {
    if (si == automatico_) return;
    automatico_ = si;
    const auto ruta = rt_.dataRoot() / "Config" / "historico.json";
    json j;
    {
        std::ifstream f(ruta);
        if (f) j = json::parse(f, nullptr, false);
    }
    if (!j.is_object()) j = json::object();
    j["evento_automatico"] = si;
    std::error_code ec;
    std::filesystem::create_directories(ruta.parent_path(), ec);
    std::ofstream(ruta) << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    asegurarEnCurso();
    emit enCursoChanged();
}

void EventoViewModel::asegurarEnCurso() {
    const std::string antes = store_.enCurso();
    if (automatico_) {
        std::string err;
        const std::string dia = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
        if (store_.asegurarDelDia(dia, ahoraMs(), &err).empty())
            emit notify("No se pudo crear el evento del día: " + QString::fromStdString(err), "error");
    }
    if (store_.enCurso() != antes) {
        emit enCursoChanged();
        emit listaChanged();
    }
}

bool EventoViewModel::iniciarEvento(const QString& carpeta) {
    std::string err;
    if (store_.enCurso() == carpeta.toStdString()) return true;
    if (!store_.iniciar(carpeta.toStdString(), ahoraMs(), &err)) {
        emit notify("No se pudo poner en curso: " + QString::fromStdString(err), "error");
        return false;
    }
    emit enCursoChanged();
    emit listaChanged();
    {
        // Su página web con dirección propia: que la dirección pública pase a ser esa.
        const std::filesystem::path dir = evento::carpetaWeb(store_.carpetaDe(carpeta.toStdString()));
        std::error_code ec;
        if (std::filesystem::exists(dir / "portada.html", ec)) {
            const evento::Evento ev = carpeta == abierto_ ? e_ : store_.cargar(carpeta.toStdString());
            const evento::SitioWeb s = evento::SitioWeb::leer(dir, ev);
            if (s.direccionAlIniciar && !s.subdominio.empty()) emit pedirDireccion(QString::fromStdString(s.subdominio));
        }
    }
    return true;
}

bool EventoViewModel::terminarEvento() {
    std::string err;
    if (!store_.terminar(ahoraMs(), &err)) {
        emit notify(QString::fromStdString(err), "error");
        return false;
    }
    asegurarEnCurso();
    emit enCursoChanged();
    emit listaChanged();
    return true;
}

QString EventoViewModel::moverEvento(const QString& carpeta, const QString& aCarpeta) {
    if (carpeta == abierto_ && sucio_) guardar();
    std::string err;
    const std::string nueva = store_.mover(carpeta.toStdString(), aCarpeta.toStdString(), &err);
    if (nueva.empty()) {
        emit notify("No se pudo mover: " + QString::fromStdString(err), "error");
        return {};
    }
    if (abierto_ == carpeta) {
        abierto_ = QString::fromStdString(nueva);
        emit guionChanged();
    }
    emit enCursoChanged();
    emit listaChanged();
    return QString::fromStdString(nueva);
}

QString EventoViewModel::crearCarpeta(const QString& ruta) {
    std::string err;
    const std::string c = store_.crearCarpeta(ruta.toStdString(), &err);
    if (c.empty()) {
        emit notify("No se pudo crear la carpeta: " + QString::fromStdString(err), "error");
        return {};
    }
    emit listaChanged();
    return QString::fromStdString(c);
}

QVariantMap EventoViewModel::datosDe(const QString& carpeta) const {
    return datosAMap(store_.datos(carpeta.toStdString()));
}

bool EventoViewModel::cambiarDatos(const QString& carpeta, const QVariantMap& campos) {
    evento::DatosDelEvento d = store_.datos(carpeta.toStdString());
    if (campos.contains("lugar")) d.lugar = campos.value("lugar").toString().toStdString();
    if (campos.contains("notas")) d.notas = campos.value("notas").toString().toStdString();
    if (campos.contains("etiquetas")) {
        d.etiquetas.clear();
        for (const QString& e : campos.value("etiquetas").toStringList())
            if (!e.trimmed().isEmpty()) d.etiquetas.push_back(e.trimmed().toStdString());
    }
    std::string err;
    if (!store_.guardarDatos(carpeta.toStdString(), d, &err)) {
        emit notify("No se pudo guardar la ficha: " + QString::fromStdString(err), "error");
        return false;
    }
    emit listaChanged();
    return true;
}

QVariantList EventoViewModel::eventos() const {
    QVariantList out;
    const std::string enCurso = store_.enCurso();
    for (const evento::Ficha& f : store_.listar()) {
        out.append(QVariantMap{{"carpeta", QString::fromStdString(f.carpeta)},
                               {"grupo", QString::fromStdString(f.grupo)},
                               {"enCurso", f.carpeta == enCurso},
                               {"historico", datosAMap(f.datos)},
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

void EventoViewModel::refrescar() { emit listaChanged(); }

QString EventoViewModel::crear(const QString& nombre, const QString& enCarpeta) {
    std::string err;
    const std::string carpeta = store_.crear(nombre.toStdString(), &err, enCarpeta.toStdString());
    if (carpeta.empty()) {
        emit notify("No se pudo crear el evento: " + QString::fromStdString(err), "error");
        return {};
    }
    emit listaChanged();
    abrir(QString::fromStdString(carpeta));
    // Todo evento nuevo nace con su página web básica (la plantilla): hay algo aunque
    // el asistente no la personalice, y le sirve de ejemplo de la estructura.
    asegurarWebBase();
    return QString::fromStdString(carpeta);
}

void EventoViewModel::asegurarWebBase() {
    if (abierto_.isEmpty()) return;
    // Solo lo que falta (una sección nueva, la portada de un evento recién creado):
    // lo que ya está —editado a mano o por el asistente— no se toca.
    const evento::ResultadoWeb r = evento::generar(e_, carpetaWebAbs().toStdWString(), false);
    if (r.escritas.empty()) return;
    ++webVersion_;
    webUltimo_.clear();
    emit webChanged();
    publicarWeb();
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
    ultimoGuion_.reset();
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
        emit notify("El padron de este evento no se pudo leer: " + QString::fromStdString(errPadron),
                    "error");
    migrarAutoAceptar();
    // Cada dos segundos: el que escanea en la puerta espera mirando el
    // telefono, y diez segundos ahi parado se sienten eternos.
    if (buzon_.interval() == 0) {
        buzon_.setInterval(ba::ajustes::entero("evento.buzonMs", 2000));
        connect(&buzon_, &QTimer::timeout, this, &EventoViewModel::recogerAcreditaciones);
    }
    buzon_.start();
    // Su página web: las páginas que nadie editó toman la plantilla de este programa (si se
    // mejoró, se ve ya) y se crean las que falten. Lo editado no se toca. Los datos no
    // están en los archivos: los publica la app (publicarWeb) y la página los dibuja.
    asegurarWebBase();
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
    ultimoGuion_.reset();
    buzon_.stop();
    e_ = {};
    padron_ = {};
    abierto_.clear();
    sucio_ = false;
    reparos_.clear();
    webUltimo_.clear();
    emit padronChanged();
    emit guionChanged();
    emit sucioChanged();
    emit reparosChanged();
}

bool EventoViewModel::borrar(const QString& carpeta) {
    // Si es el abierto, se cierra ANTES de borrarlo y sin guardar: cerrar
    // guarda lo pendiente, y hacerlo después volvía a escribir en el disco el
    // evento recién borrado (por eso la página cerraba y borraba por su cuenta).
    if (abierto_ == carpeta) {
        sucio_ = false;
        cerrar();
    }
    std::string err;
    if (!store_.borrar(carpeta.toStdString(), &err)) {
        emit notify("No se pudo borrar: " + QString::fromStdString(err), "error");
        return false;
    }
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

namespace {
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
} // namespace

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
            m["horaPrevista"] = horaPrevista(e_.inicio, e_.offsetDeCueSeg(bi, ci));
            cs.append(m);
        }
        bs.append(QVariantMap{{"id", QString::fromStdString(b.id)},
                              {"nombre", QString::fromStdString(b.nombre)},
                              {"nota", QString::fromStdString(b.nota)},
                              {"duracionSeg", b.duracionSeg()},
                              {"offsetSeg", e_.offsetDeBloqueSeg(bi)},
                              {"horaPrevista", horaPrevista(e_.inicio, e_.offsetDeBloqueSeg(bi))},
                              {"indice", static_cast<int>(bi)},
                              {"cues", cs}});
    }
    return QVariantMap{{"nombre", QString::fromStdString(e_.nombre)},
                       {"fecha", QString::fromStdString(e_.fecha)},
                       {"inicio", QString::fromStdString(e_.inicio)},
                       {"huso", QString::fromStdString(e_.huso)},
                       {"cuando", cuando()},
                       {"nota", QString::fromStdString(e_.nota)},
                       {"invitados", e_.invitados},
                       {"preguntas", e_.preguntas},
                       {"asamblea", e_.asamblea},
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
    for (const auto& i : app::graficos::instancias())
        out << QString::fromStdString(i.nombre);
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

QVariantList EventoViewModel::opcionesDeCampo(const QString& tipo, const QString& campo) const {
    QVariantList out;
    const auto op = [&out](const QString& valor, const QString& texto = {}) {
        out.append(QVariantMap{{"valor", valor}, {"texto", texto.isEmpty() ? valor : texto}});
    };
    if (campo == "accion") {
        if (tipo == "musica" || tipo == "video") {
            op("arrancar", "Arrancar");
            op("parar", "Parar");
        } else if (tipo == "grafico") {
            op("mostrar", "Mostrar");
            op("ocultar", "Sacar");
            op("prender", "Prender el rótulo automático");
            op("apagar", "Apagar el rótulo automático");
        } else if (tipo == "audio") {
            op("silenciar", "Silenciar");
            op("activar", "Activar");
            op("volumen", "Cambiar el volumen");
        }
    } else if (campo == "escena") {
        for (const std::string& e : rt_.engine().sceneNames()) op(QString::fromStdString(e));
    } else if (campo == "archivo") {
        // Los medios del evento (Media/), con su ruta relativa, solo los del tipo.
        static const QStringList kAudio{"mp3", "wav", "ogg", "oga", "flac", "m4a", "aac", "opus", "aif", "aiff"};
        static const QStringList kVideo{"mp4", "mov", "mkv", "webm", "avi", "m4v", "mxf", "ts", "mpg", "mpeg"};
        const QStringList& exts = tipo == "video" ? kVideo : kAudio;
        const QDir media(QString::fromStdU16String((rt_.dataRoot() / "Media").u16string()));
        QStringList rels;
        QDirIterator it(media.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (exts.contains(QFileInfo(f).suffix().toLower())) rels << media.relativeFilePath(f);
        }
        rels.sort(Qt::CaseInsensitive);
        for (const QString& r : rels) op(r);
    } else if (campo == "discurso") {
        const Discursos docs(carpetaDeDiscursos());
        for (const QString& carpeta : docs.carpetas())
            for (const QVariant& d : docs.listar(carpeta)) {
                const QVariantMap m = d.toMap();
                op(m.value("rel").toString(), m.value("rel").toString());
            }
    } else if (campo == "participante") {
        // Quien está en la sala ahora (los que llegan después se pueden escribir).
        if (sala_ != nullptr)
            for (const QJsonValue& v : sala_->client().snapshot().value("participants").toArray()) {
                const QJsonObject p = v.toObject();
                const QString id = p.value("id").toString();
                if (!id.isEmpty()) op(id, p.value("name").toString().isEmpty() ? id : p.value("name").toString());
            }
    } else if (campo == "grafico") {
        for (const QString& g : graficosDisponibles()) op(g);
    } else if (campo == "fuente" && tipo == "grafico") {
        for (const QString& f : fuentesDeGrafico()) op(f);
    } else if (campo == "fuente" && tipo == "audio") {
        for (const std::string& f : rt_.engine().audioSources()) op(QString::fromStdString(f));
    } else if (campo == "contenido" && tipo == "pantalla") {
        op("programa", "El programa (lo que sale al aire)");
        op("reloj", "Un reloj");
        op("prompter", "El teleprompter");
        op("tally", "Quién está al aire");
        op("votacion", "La votación");
    }
    return out;
}

QString EventoViewModel::enlaceDeCue(const QString& tipo) const {
    // El servidor de la sala, en esta máquina (la pantalla del orador solo lee: sin clave).
    if (tipo == "teleprompter") return "http://127.0.0.1:" + QString::number(rt_.gateway().port) + "/prompter/";
    return {};
}

QVariantList EventoViewModel::escenarios() const {
    QFile f(":/escenarios/indice.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array().toVariantList();
}

QString EventoViewModel::pedidoDeEscenario(const QString& id, const QString& notas) const {
    QString titulo = id;
    for (const QVariant& v : escenarios())
        if (v.toMap().value("id").toString() == id) titulo = v.toMap().value("titulo").toString();
    QString p = "Quiero preparar un evento nuevo: «" + titulo + "». Seguí la guía de ese tipo de evento: evento_escenario "
                "{\"id\": \"" + id + "\"} (trae el protocolo común y lo propio de este tipo). Empezá por las preguntas, "
                "de a pocas y con opciones; no armes nada hasta que te confirme el plan. Arrancalo DESDE CERO (un evento "
                "aparte, con evento_nuevo), verificá todo antes de mandar invitaciones, sugerime el ensayo y al terminar "
                "pasame el enlace de la página web del evento.";
    if (!notas.trimmed().isEmpty()) p += " Lo que ya sé: " + notas.trimmed();
    return p;
}

void EventoViewModel::setValorDeCue(int bloque, int cue, const QString& clave, const QVariant& valor) {
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

void EventoViewModel::setInvitados(bool si) {
    if (e_.invitados == si) return;
    e_.invitados = si;
    marcarSucio();
    emit guionChanged();
}

void EventoViewModel::setAsamblea(bool si) {
    if (e_.asamblea == si) return;
    e_.asamblea = si;
    marcarSucio();
    emit guionChanged();
}

void EventoViewModel::setPreguntas(bool si) {
    if (e_.preguntas == si) return;
    e_.preguntas = si;
    marcarSucio();
    emit guionChanged();
}

QString EventoViewModel::husoDelAparato() { return QString::fromUtf8(QTimeZone::systemTimeZoneId()); }

QString EventoViewModel::cuando() const {
    if (e_.fecha.empty() || e_.inicio.empty()) return {};
    if (!evento::fechaValida(e_.fecha) || !evento::horaValida(e_.inicio)) return {};
    const QDate d = QDate::fromString(QString::fromStdString(e_.fecha), Qt::ISODate);
    const QTime h = QTime::fromString(QString::fromStdString(e_.inicio), QStringLiteral("HH:mm"));
    // Sin huso guardado corre el del aparato, que es lo que el operador ve en
    // la pared. Es un valor por defecto razonable, no una adivinanza: el que
    // arma el evento esta en el lugar del evento casi siempre.
    const QTimeZone z =
        e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
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
    c->normalizarDatos();
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
    asegurarWebBase(); // las páginas de las secciones nuevas
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
        const evento::Cue& c =
            e_.bloques[static_cast<std::size_t>(p.bloque)].cues[static_cast<std::size_t>(p.cue)];
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
                anotar(donde + " " + que + " — no existe la escena \"" + QString::fromStdString(escena) +
                           "\"",
                       "error");
                continue;
            }
        }
        if (!evento::ejecutable(c.tipo)) {
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

    QString resumen =
        QString("Ensayo acelerado: %1 de %2 cues recorridos").arg(visitados).arg(e_.cantidadDeCues());
    if (colgados > 0) resumen += QString(", %1 bloque(s) vacío(s) que no pasan").arg(colgados);
    if (sinEjecutar > 0) resumen += QString(", %1 sin ejecución todavía").arg(sinEjecutar);
    if (problemas > 0) resumen += QString(", %1 con problemas").arg(problemas);
    anotar(resumen, problemas > 0 ? "error" : "ok");
    emit notify(resumen, problemas > 0 ? "error" : "ok");
}

namespace {

/// "1:23:45" o "4:05". YouTube acepta las dos y la corta es mas legible.
QString marcaDe(int seg) {
    const int h = seg / 3600, m = (seg % 3600) / 60, s = seg % 60;
    return h > 0 ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                 : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

} // namespace

QVariantMap EventoViewModel::capitulos(qint64 recInicioMs) const {
    const auto fallo = [](const QString& motivo) {
        return QVariantMap{{"ok", false}, {"motivo", motivo}, {"capitulos", QVariantList{}}};
    };
    const evento::Guion* g = guionDelInforme();
    if (g == nullptr)
        return fallo("El guion no se corrio en esta sesion: no hay de donde sacar los tiempos.");
    const auto& pasos = g->recorrido();
    if (pasos.empty()) return fallo("El guion no avanzo ningun cue todavia.");
    if (recInicioMs <= 0)
        return fallo("No se cuando arranco la grabacion. Los capitulos se miden desde ahi, no desde "
                     "el arranque del evento: si el guion empezo antes, todas las marcas estarian "
                     "corridas.");

    QVariantList caps;
    int antesDeGrabar = 0;
    int bloqueIdx = -1;
    for (const evento::Guion::Paso& p : pasos) {
        // Un capitulo por BLOQUE: se toma el primer cue de cada tramo.
        if (p.pos.bloque == bloqueIdx) continue;
        bloqueIdx = p.pos.bloque;
        const auto bi = static_cast<std::size_t>(bloqueIdx);
        if (bi >= e_.bloques.size()) continue;
        const int seg = static_cast<int>((p.desdeMs - recInicioMs) / 1000);
        // Lo que paso ANTES de apretar grabar no esta en el video. Se cuenta y
        // se avisa, en vez de inventarle un 0:00 a algo que no se grabo.
        if (seg < 0) {
            ++antesDeGrabar;
            continue;
        }
        const QString nombre = e_.bloques[bi].nombre.empty() ? QString("Bloque %1").arg(bloqueIdx + 1)
                                                             : QString::fromStdString(e_.bloques[bi].nombre);
        caps.append(QVariantMap{{"seg", seg}, {"marca", marcaDe(seg)}, {"titulo", nombre}});
    }
    if (caps.isEmpty()) return fallo("Todos los bloques empezaron antes de que arrancara la grabacion.");

    // YouTube pide que el PRIMERO sea 0:00. Si el guion arranco despues de
    // apretar grabar (lo normal: se graba el ambiente antes), se antepone uno
    // para lo que haya quedado ahi adelante en vez de mover el resto.
    QVariantMap primero = caps.first().toMap();
    if (primero.value("seg").toInt() > 0)
        caps.prepend(QVariantMap{{"seg", 0}, {"marca", "0:00"}, {"titulo", "Antes de empezar"}});

    // Y que ninguno dure menos de diez segundos, o los ignora a todos. Se
    // juntan los pegados en vez de devolver una lista que YouTube va a tirar.
    QVariantList limpios;
    for (const QVariant& v : caps) {
        const QVariantMap c = v.toMap();
        if (!limpios.isEmpty() && c.value("seg").toInt() - limpios.last().toMap().value("seg").toInt() < 10)
            continue;
        limpios.append(c);
    }

    QString texto;
    for (const QVariant& v : limpios) {
        const QVariantMap c = v.toMap();
        texto += c.value("marca").toString() + " " + c.value("titulo").toString() + "\n";
    }
    return QVariantMap{{"ok", limpios.size() >= 3},
                       {"motivo", limpios.size() >= 3 ? QString()
                                                      : QString("YouTube ignora las listas de menos de tres "
                                                                "capitulos: quedaron %1.")
                                                            .arg(limpios.size())},
                       {"capitulos", limpios},
                       {"texto", texto},
                       {"antesDeGrabar", antesDeGrabar}};
}

void EventoViewModel::pedirInvitacion(const QVariantMap& quien, bool remoto,
                                      std::function<void(const QVariantMap&)> luego) {
    if (sala_ == nullptr) return luego(QVariantMap{{"ok", false}, {"motivo", "La sala no está disponible."}});
    const QString codigo = quien.value("codigo").toString();
    // El nombre y la fecha salen del evento; el enlace, del gateway.
    const QString nombreEvento = e_.nombre.empty() ? abierto_ : QString::fromStdString(e_.nombre);
    QString cuandoTexto = QString::fromStdString(e_.fecha);
    if (!e_.inicio.empty()) cuandoTexto += " a las " + QString::fromStdString(e_.inicio);
    if (!e_.huso.empty()) cuandoTexto += " (" + QString::fromStdString(e_.huso) + ")";
    const QString instante = cuando();

    // El mismo enlace de la página del público (modelo: sala::EnlacesSala).
    sala_->enlaces().publico(
        remoto, [luego, quien, codigo, nombreEvento, cuandoTexto, instante](const sala::Enlace& e) {
            if (!e.ok)
                return luego(QVariantMap{
                    {"ok", false}, {"motivo", "No pude pedirle el enlace a la sala. Fijate `room_status`."}});
            // El código va en el FRAGMENTO, con la clave: no viaja en la petición
            // ni queda en los registros de ningún servidor por el que pase.
            const QString url = QString::fromStdString(e.url) + "&c=" + codigo;

            QString t;
            t += "Te esperamos en " + nombreEvento + ".\n\n";
            if (cuandoTexto.trimmed().isEmpty())
                t += "(El evento todavía no tiene fecha en el guion.)\n\n";
            else
                t += "Cuándo: " + cuandoTexto.trimmed() + "\n";
            if (!instante.isEmpty()) t += "En tu huso horario: " + instante + "\n";
            t += "\nEste es tu enlace personal:\n" + url + "\n\n";
            t += "Escanealo o abrilo al llegar: queda registrado que llegaste, y desde ahí podés "
                 "preguntar y votar.\n";
            t += "Tu código, por si te lo piden: " + codigo + "\n";
            if (!quien.value("nota").toString().isEmpty())
                t += "\nNota: " + quien.value("nota").toString() + "\n";

            QVariantList modulos;
            for (const std::string& fila : e.filas)
                modulos.append(QString::fromStdString(fila));
            luego(QVariantMap{{"ok", true}, {"texto", t}, {"url", url}, {"modulos", modulos}});
        });
}

QString EventoViewModel::acta(const QJsonArray& votaciones) const {
    const evento::Resumen r = padron_.resumen();
    QString t;
    const QString titulo = e_.nombre.empty() ? abierto_ : QString::fromStdString(e_.nombre);
    t += "# Acta — " + titulo + "\n\n";

    QString cuandoTexto = QString::fromStdString(e_.fecha);
    if (!e_.inicio.empty()) cuandoTexto += " " + QString::fromStdString(e_.inicio);
    if (!e_.huso.empty()) cuandoTexto += " (" + QString::fromStdString(e_.huso) + ")";
    t += cuandoTexto.trimmed().isEmpty() ? QString("**Fecha:** sin fecha en el guion\n\n")
                                         : "**Fecha:** " + cuandoTexto.trimmed() + "\n\n";

    // --- El quorum, arriba y con sus DOS numeros ---------------------------
    // Un acta que dice «se aprobo por 8 votos a favor» sin decir cuantos podian
    // votar no prueba nada.
    t += "## Asistencia\n\n";
    t += QString("- Presentes: **%1** de %2 en el padrón "
                 "(%3 en el lugar, %4 en línea%5)\n")
             .arg(r.presentes)
             .arg(r.total)
             .arg(r.presenciales)
             .arg(r.enLinea)
             .arg(r.sinDato > 0 ? QString(", %1 sin dato de por dónde").arg(r.sinDato) : QString());
    if (r.conVoto > 0)
        t += QString("- Con derecho a voto presentes: **%1** de %2 — **quórum**\n")
                 .arg(r.votoPresente)
                 .arg(r.conVoto);
    t += QString("- No llegaron: %1\n\n").arg(r.faltan);

    t += "| Nombre | Papel | Por dónde | Llegó | Voto |\n|---|---|---|---|---|\n";
    for (const evento::Asistente& a : padron_.gente()) {
        const bool llego = a.estado == evento::Estado::Llego || a.llegoMs > 0;
        const QString hora =
            a.llegoMs > 0 ? QDateTime::fromMSecsSinceEpoch(a.llegoMs).toString("HH:mm") : QString("—");
        t += QString("| %1 | %2 | %3 | %4 | %5 |\n")
                 .arg(QString::fromStdString(a.nombre).isEmpty() ? QString("(sin nombre)")
                                                                 : QString::fromStdString(a.nombre))
                 .arg(QString::fromUtf8(evento::nombreDePapel(a.papel)))
                 .arg(llego && !a.porDonde.empty() ? QString::fromStdString(a.porDonde)
                                                   : QString::fromUtf8(evento::nombreDeCanal(a.canal)))
                 .arg(hora)
                 .arg(a.vota ? "sí" : "—");
    }
    t += "\n";

    // --- Lo que se decidio --------------------------------------------------
    t += "## Lo que se votó\n\n";
    if (votaciones.isEmpty()) {
        t += "No se votó nada en este evento.\n\n";
    } else {
        int n = 0;
        for (const QJsonValue& vv : votaciones) {
            const QJsonObject v = vv.toObject();
            ++n;
            t += QString("### %1. %2\n\n").arg(n).arg(v.value("pregunta").toString());
            const bool formal = v.value("modo").toString() == "formal";
            // Se dice CUAL fue: una encuesta leida como decision es el error
            // que esta separacion existe para evitar.
            t += formal ? QString("*Votación formal") : QString("*Encuesta (resultado indicativo)");
            // Lo que vota el público es una consulta: no reemplaza la votación del cuerpo.
            if (!v.value("vinculante").toBool()) t += " — consulta, no vinculante";
            if (v.value("secreta").toBool()) t += ", secreta";
            t += "*\n\n";
            const int votaron = v.value("votaron").toInt();
            const int podian = v.value("podian").toInt();
            t += podian > 0 ? QString("Votaron %1 de %2.\n\n").arg(votaron).arg(podian)
                            : QString("Votaron %1.\n\n").arg(votaron);
            for (const QJsonValue& oo : v.value("resultado").toArray()) {
                const QJsonObject o = oo.toObject();
                t +=
                    QString("- %1: **%2**\n").arg(o.value("opcion").toString()).arg(o.value("votos").toInt());
            }
            t += "\n";
            const QJsonArray faltan = v.value("faltanVotar").toArray();
            if (!faltan.isEmpty()) {
                QStringList ns;
                for (const QJsonValue& f : faltan)
                    ns << f.toString();
                t += "No votaron: " + ns.join(", ") + "\n\n";
            }
        }
    }

    if (anexoActa_) t += anexoActa_();
    t += "---\n\n_Generada por Broadcast Appliance el " +
         QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") + "._\n";
    return t;
}

QString EventoViewModel::guardarActa(const QString& texto) const {
    if (abierto_.isEmpty()) return {};
    const QString nombre = "acta-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + ".md";
    const QString ruta =
        QString::fromStdString((store_.carpetaDe(abierto_.toStdString()) / nombre.toStdString()).string());
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(texto.toUtf8());
    if (f.error() != QFile::NoError) return {};
    f.close();
    // La huella del acta (SHA-256), al lado: cualquiera puede verificar que el
    // texto no cambió (`shasum -a 256 -c acta-….md.sha256`).
    QFile h(ruta + ".sha256");
    if (h.open(QIODevice::WriteOnly | QIODevice::Truncate))
        h.write(hashActa(texto).toUtf8() + "  " + nombre.toUtf8() + "\n");
    return ruta;
}

QString EventoViewModel::hashActa(const QString& texto) {
    return QString::fromLatin1(QCryptographicHash::hash(texto.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap EventoViewModel::informe() const {
    const evento::Guion* g = guionDelInforme();
    if (g == nullptr) return {};
    const auto& pasos = g->recorrido();
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
            bloqueActual =
                QVariantMap{{"nombre", bi < e_.bloques.size() ? QString::fromStdString(e_.bloques[bi].nombre)
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
    const QString nombre = "informe-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
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
        for (const evento::Reparo& r : evento::revisarArchivos(e_, store_.carpetaDe(abierto_.toStdString()), rt_.dataRoot() / "Media"))
            reparos_.push_back(r);
    }
    // Lo del aparato: disco, destino de la transmisión, entradas con señal,
    // pantallas vivas (sin bloque: es de todo el evento).
    if (chequeosAparato_)
        for (const QVariant& v : chequeosAparato_()) {
            const QVariantMap m = v.toMap();
            reparos_.push_back({-1, -1, m.value("grave").toBool(), m.value("texto").toString().toStdString()});
        }
    // Los cues de un tipo que el programa no ejecuta (la misma lista que el ensayo).
    for (std::size_t bi = 0; bi < e_.bloques.size(); ++bi)
        for (std::size_t ci = 0; ci < e_.bloques[bi].cues.size(); ++ci)
            if (!evento::ejecutable(e_.bloques[bi].cues[ci].tipo))
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), false,
                                    "el programa todavía no sabe ejecutar este tipo de cue"});
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
    m["bloqueNombre"] = QString::fromStdString(e_.bloques[static_cast<std::size_t>(p.bloque)].nombre);
    return m;
}

QVariantMap EventoViewModel::siguiente() const {
    if (!guionCorriendo_) return {};
    const evento::Posicion s = guionCorriendo_->siguienteDe(guionCorriendo_->posicion());
    const evento::Cue* c = guionCorriendo_->cueSiguiente();
    if (c == nullptr) return {};
    QVariantMap m = cueAMap(*c, s.bloque, s.cue);
    m["bloqueNombre"] = QString::fromStdString(e_.bloques[static_cast<std::size_t>(s.bloque)].nombre);
    return m;
}

QVariantMap EventoViewModel::reloj() const {
    if (!guionCorriendo_) return QVariantMap{{"desvioSeg", 0}, {"enCueSeg", 0}, {"restanteSeg", 0}};
    const int64_t t = ahoraMs();
    // En que numero de cue vamos, para "12 de 34".
    int nro = 0;
    const evento::Posicion p = guionCorriendo_->posicion();
    for (int b = 0; b < static_cast<int>(e_.bloques.size()) && b <= p.bloque; ++b) {
        const int hasta =
            b == p.bloque ? p.cue + 1 : static_cast<int>(e_.bloques[static_cast<std::size_t>(b)].cues.size());
        nro += hasta;
    }
    // Back-timing (T46): lo que falta del guion si se sigue lo previsto desde
    // acá, lo que falta del bloque, a qué hora terminaría y, si el guion tiene
    // hora de inicio, contra qué hora de fin planeada se compara.
    int restanteGuion = 0, restanteBloque = 0, totalPrevisto = 0;
    const int enCue = guionCorriendo_->enCueSeg(t);
    for (int b = 0; b < static_cast<int>(e_.bloques.size()); ++b) {
        const auto& cues = e_.bloques[static_cast<std::size_t>(b)].cues;
        for (int c = 0; c < static_cast<int>(cues.size()); ++c) {
            const int d = cues[static_cast<std::size_t>(c)].duracionSeg;
            totalPrevisto += d;
            if (!p.valida()) continue;
            int falta = 0;
            if (b > p.bloque || (b == p.bloque && c > p.cue)) falta = d;
            else if (b == p.bloque && c == p.cue) falta = std::max(0, d - enCue);
            restanteGuion += falta;
            if (b == p.bloque) restanteBloque += falta;
        }
    }
    QVariantMap m{{"desvioSeg", guionCorriendo_->desvioSeg(t)},
                  {"enCueSeg", enCue},
                  {"restanteSeg", guionCorriendo_->restanteSeg(t)},
                  {"cueNro", p.valida() ? nro : 0},
                  {"cuesTotal", static_cast<int>(e_.cantidadDeCues())},
                  {"restanteBloqueSeg", restanteBloque},
                  {"restanteGuionSeg", restanteGuion},
                  {"horaFinPrevista", QDateTime::fromMSecsSinceEpoch(t + restanteGuion * 1000LL).toString("HH:mm")},
                  // Lo que lleva (sin pausas) contra lo previsto del guion entero.
                  {"transcurridoSeg", guionCorriendo_->transcurridoSeg(t)},
                  {"totalPrevistoSeg", totalPrevisto}};
    if (!e_.inicio.empty()) {
        const QTime ini = QTime::fromString(QString::fromStdString(e_.inicio), "HH:mm");
        if (ini.isValid()) {
            const QDateTime finPlan(QDate::currentDate(), ini.addSecs(totalPrevisto));
            m["horaFinPlaneada"] = finPlan.toString("HH:mm");
            // Positivo: sobra tiempo; negativo: hay que recortar.
            m["backTimingSeg"] = static_cast<int>((finPlan.toMSecsSinceEpoch() - (t + restanteGuion * 1000LL)) / 1000);
        }
    }
    return m;
}

void EventoViewModel::setEnsayo(bool on) {
    if (ensayo_ == on) return;
    ensayo_ = on;
    emit correChanged();
    anotar(on ? "Ensayo: el guion corre sin salir al aire" : "Ensayo apagado: el guion sale al aire", "info");
}

void EventoViewModel::anotar(const QString& texto, const QString& tipo) {
    QVariantMap m{
        {"hora", QDateTime::currentDateTime().toString("HH:mm:ss")}, {"texto", texto}, {"tipo", tipo}};
    bitacora_.prepend(m);
    // La bitacora de la página es para mirar de reojo; la del histórico
    // (bitacora.jsonl del evento en curso) la toma del registro general.
    if (!texto.startsWith("(ensayo)"))
        ba::diag::registro().linea(tipo == "error" ? ba::diag::Nivel::Aviso : ba::diag::Nivel::Info, "guion", 0,
                                   texto.toStdString());
    while (bitacora_.size() > 60)
        bitacora_.removeLast();
    emit bitacoraChanged();
}

int EventoViewModel::arrancarRevisando(bool forzar) {
    revisar();
    const int graves = static_cast<int>(std::count_if(reparos_.begin(), reparos_.end(), [](const evento::Reparo& r) { return r.grave; }));
    if (graves > 0 && !forzar) return graves;
    arrancar();
    return 0;
}

void EventoViewModel::arrancar() {
    if (e_.cantidadDeCues() == 0) {
        emit notify("El guion no tiene ningún cue", "error");
        return;
    }
    // Correrlo de verdad lo pone en curso: lo que pase desde ahora es de este evento.
    if (!ensayo_ && !abierto_.isEmpty() && store_.enCurso() != abierto_.toStdString()) iniciarEvento(abierto_);
    ultimoGuion_.reset();
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
        ultimoGuion_.reset();
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
    guionCorriendo_->terminar(ahoraMs());
    ultimoGuion_ = std::move(guionCorriendo_);
    emit correChanged();
    emit relojChanged();
}

QString EventoViewModel::dentroDelEvento(const QString& sub, const QString& rel) const {
    if (abierto_.isEmpty() || rel.isEmpty()) return {};
    const QDir carpeta(QString::fromStdString(store_.carpetaDe(abierto_.toStdString()).string()));
    // "Medios/x.wav" y "x.wav" valen lo mismo: el operador escribe lo que le
    // sale y las dos formas apuntan al mismo lado.
    QString limpio = rel;
    if (limpio.startsWith(sub + "/") || limpio.startsWith(sub + "\\")) limpio = limpio.mid(sub.size() + 1);
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
    // Los medios del evento son su biblioteca (Media/, la de la página Medios: cada
    // evento tiene la suya). Los guiones de antes nombraban Medios/ de la carpeta del
    // guion: si no está en Media/, se busca ahí.
    if (rel.isEmpty()) return {};
    QString limpio = rel;
    for (const QString& pre : {QStringLiteral("Media/"), QStringLiteral("Medios/"), QStringLiteral("Media\\"),
                               QStringLiteral("Medios\\")})
        if (limpio.startsWith(pre)) limpio = limpio.mid(pre.size());
    const QDir media(QString::fromStdString((rt_.dataRoot() / "Media").string()));
    const QString abs = QDir::cleanPath(media.filePath(limpio));
    // Fuera de la biblioteca no se lee nada (un «..» en un guion que vino de otro lado).
    if (abs.startsWith(QDir::cleanPath(media.absolutePath()) + "/") && QFileInfo::exists(abs)) return abs;
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
            // Las mismas operaciones que la página Sala y el MCP (sala::OradoresSala).
            if (c->duracionSeg > 0) sala_->oradores().tiempo(quien.toStdString(), c->duracionSeg);
            sala_->oradores().alAire(quien.toStdString(), true);
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
                anotar("No encontré la música \"" + rel + "\" en los medios del evento", "error");
                emit notify("El cue no se pudo ejecutar: falta «" + rel + "» en los medios del evento", "error");
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
            // La misma carga que la página Teleprompter (sala::Teleprompter): el
            // discurso de Discursos/ con su ruta de título. `arrancar` en los
            // datos del cue: por defecto el texto queda LISTO pero parado, que
            // es lo que se quiere casi siempre (el orador todavia esta
            // caminando hacia el atril).
            const Discursos docs(carpetaDeDiscursos());
            const std::string err =
                sala_->teleprompter().cargarDiscurso(docs, rel.toStdString(), c->datos.value("arrancar", false));
            if (!err.empty()) {
                anotar("No se pudo abrir el discurso: " + rel, "error");
                emit notify("No encontré el discurso \"" + rel + "\" en la carpeta del evento", "error");
                return;
            }
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
                    emit notify("El cue no se pudo ejecutar: no existe el vínculo \"" + id + "\"", "error");
                    return;
                }
                anotar(
                    (accion == "prender" ? "Prende el rótulo automático " : "Apaga el rótulo automático ") +
                        id,
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
                anotar("No hay ninguna fuente de gráfico llamada \"" + fuente + "\" en las escenas", "error");
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
                valores =
                    QJsonDocument::fromJson(QByteArray::fromStdString(c->datos["valores"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
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
        case evento::Tipo::Espera: anotar("Espera tu confirmación: " + titulo, "info"); return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa: anotar(titulo, "info"); return;
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
                anotar("No encontré el video \"" + rel + "\" en los medios del evento", "error");
                emit notify("El cue no se pudo ejecutar: falta «" + rel + "» en los medios del evento", "error");
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
                    kVideo, QJsonDocument(ajustes).toJson(QJsonDocument::Compact).toStdString())) {
                anotar("No se pudo cargar " + rel + ": " + QString::fromStdString(rt_.lastError()), "error");
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
        case evento::Tipo::Pantalla: {
            // Lo mismo que pantalla_mostrar: `contenido` es el texto de un
            // cartel, una dirección (url) o el nombre de una vista (programa,
            // reloj, prompter, tally, votacion).
            const std::string pantalla = c->datos.value("pantalla", std::string());
            const std::string contenido = c->datos.value("contenido", std::string());
            std::string tipo = c->datos.value("tipo", std::string());
            if (tipo.empty())
                tipo = contenido.rfind("http", 0) == 0 ? "url"
                       : (contenido == "programa" || contenido == "reloj" || contenido == "prompter" ||
                          contenido == "tally" || contenido == "votacion")
                           ? contenido
                           : "mensaje";
            json args{{"pantalla", pantalla}, {"tipo", tipo}};
            if (tipo == "url") args["url"] = contenido;
            if (tipo == "mensaje" || tipo == "qr") args["texto"] = contenido;
            if (c->datos.contains("pie")) args["pie"] = c->datos["pie"];
            if (!herramienta_) {
                anotar("No se puede mandar a la pantalla: el servidor de herramientas no está", "error");
                return;
            }
            QPointer<EventoViewModel> yo(this);
            herramienta_("pantalla_mostrar", args, [yo, titulo, pantalla](bool ok, const std::string& texto) {
                if (!yo) return;
                yo->anotar((ok ? "Pantalla «" + QString::fromStdString(pantalla) + "»: " + titulo
                               : "No se pudo mandar a «" + QString::fromStdString(pantalla) + "»: " +
                                     QString::fromStdString(texto)),
                           ok ? "ok" : "error");
            });
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
                       {"codigo", QString::fromStdString(a.codigo)},
                       {"llegoMs", static_cast<qint64>(a.llegoMs)},
                       {"porDonde", QString::fromStdString(a.porDonde)}};
}

} // namespace

QVariantList EventoViewModel::padron() const {
    QVariantList out;
    for (const evento::Asistente& a : padron_.gente())
        out.append(aMapa(a));
    return out;
}

QVariantMap EventoViewModel::resumenPadron() const {
    const evento::Resumen r = padron_.resumen();
    return QVariantMap{
        {"total", r.total},     {"presentes", r.presentes},      {"presenciales", r.presenciales},
        {"enLinea", r.enLinea}, {"confirmados", r.confirmados},  {"faltan", r.faltan},
        {"conVoto", r.conVoto}, {"votoPresente", r.votoPresente}, {"sinDato", r.sinDato}};
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
    if (!padron_.acreditar(id.toStdString(), porDonde.toStdString(), static_cast<std::int64_t>(ahora)))
        return false;
    return guardarPadronAhora();
}

bool EventoViewModel::despedir(const QString& id) {
    if (abierto_.isEmpty()) return false;
    if (!padron_.despedir(id.toStdString())) return false;
    return guardarPadronAhora();
}

void EventoViewModel::recogerAcreditaciones() {
    if (abierto_.isEmpty() || sala_ == nullptr) return;
    QPointer<EventoViewModel> yo(this);
    sala_->control().acreditaciones([this, yo](const std::string& err, const nlohmann::json& rj) {
        if (!yo || !err.empty()) return;
        const QJsonObject r = QJsonDocument::fromJson(QByteArray::fromStdString(rj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
        const QJsonArray cs = r.value("codigos").toArray();
        if (cs.isEmpty()) return;
        int hechos = 0;
        QStringList desconocidos;
        for (const QJsonValue& v : cs) {
            const std::string codigo = v.toString().toStdString();
            const evento::Asistente* a = padron_.porCodigo(codigo);
            // Un codigo que no esta en el padron NO se anota solo: quien entra
            // sin estar invitado es una decision del que organiza. Pero se
            // avisa, porque del otro lado hay alguien parado esperando.
            if (a == nullptr) {
                desconocidos << QString::fromStdString(codigo);
                continue;
            }
            if (acreditar(QString::fromStdString(a->id), "qr")) ++hechos;
        }
        if (hechos > 0)
            emit notify(QString::number(hechos) + (hechos == 1 ? " persona llegó" : " personas llegaron") +
                            " (escanearon su invitación).",
                        "info");
        if (!desconocidos.isEmpty())
            emit notify("Escanearon códigos que no están en el padrón: " + desconocidos.join(", ") +
                            ". Si corresponde, anotalos con el padrón.",
                        "warn");
    });
}

void EventoViewModel::migrarAutoAceptar() {
    // La lista blanca vieja del lobby (`Evento::autoAceptar`) y el permiso del
    // padron (`dejarPasarSolo`) decian lo mismo. Dos listas para lo mismo se
    // desincronizan —se agrega a alguien en una y el otro dia no entra—, asi
    // que la vieja se vuelca en el padron y se apaga.
    //
    // Se hace al abrir y una sola vez: despues el guion ya no la trae.
    if (e_.autoAceptar.empty()) return;
    int movidos = 0;
    for (const evento::Invitado& i : e_.autoAceptar) {
        if (i.personaId.empty()) continue;
        const evento::Asistente* ya = padron_.porPersona(i.personaId);
        if (ya != nullptr) {
            evento::Asistente prender;
            prender.dejarPasarSolo = true;
            padron_.cambiar(ya->id, prender, {"dejarPasarSolo"});
        } else {
            evento::Asistente a;
            a.personaId = i.personaId;
            a.nombre = i.nombre;
            a.nota = i.nota;
            a.dejarPasarSolo = true;
            // Estaba en la lista de entrar solo, asi que alguien lo decidio a
            // proposito: es invitado de verdad, no publico anonimo.
            a.papel = evento::Papel::Panelista;
            a.canal = evento::Canal::Online;
            a.estado = evento::Estado::Confirmo;
            padron_.agregar(std::move(a));
        }
        ++movidos;
    }
    e_.autoAceptar.clear();
    guardarPadronAhora();
    // El guion se guarda para que la lista vieja no vuelva a aparecer en la
    // proxima apertura y la migracion se repita para siempre.
    std::string err;
    store_.guardar(abierto_.toStdString(), e_, &err);
    if (movidos > 0)
        emit notify(QString::number(movidos) + " de la lista de auto-aceptar pasaron al padrón del evento.",
                    "info");
}

bool EventoViewModel::puedeEntrarSolo(const QString& personaId) const {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    return a != nullptr && a->dejarPasarSolo;
}

bool EventoViewModel::acreditarPorPersona(const QString& personaId, const QString& porDonde) {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr) return false;
    return acreditar(QString::fromStdString(a->id), porDonde);
}

QString EventoViewModel::buscarEnPadron(const QString& personaId, const QString& nombre) const {
    // La ficha de rostro manda sobre el nombre: el nombre lo escribe cualquiera
    // y hay dos Gonzalez en cualquier lista de cuarenta.
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr) a = padron_.porNombre(nombre.toStdString());
    return a == nullptr ? QString() : QString::fromStdString(a->id);
}

namespace {
/// Lo que conviene confirmar a mano antes de una sesión (no hay cómo medirlo solo).
const std::vector<std::pair<const char*, const char*>>& itemsDeChequeo() {
    static const std::vector<std::pair<const char*, const char*>> v{
        {"ptz", "PTZ probado con las cámaras reales (presets, que no se muevan al aire)"},
        {"casteo", "Casteo probado en los televisores del recinto"},
        {"caida", "Ensayo de caída: cerrar la app al aire y cronometrar cuánto tarda en volver"},
        {"glosario", "Glosario y nombres revisados (integrantes, localidades)"},
        {"respaldo", "Máquina de respaldo lista con la escena básica"},
        {"auriculares", "Los invitados remotos con auriculares (o mix-minus probado)"},
        {"sonido", "Prueba de sonido con cada micrófono de la sala"},
    };
    return v;
}
} // namespace

QVariantList EventoViewModel::chequeo() const {
    QVariantList out;
    json guardado = json::object();
    if (!abierto_.isEmpty()) {
        std::ifstream f(store_.carpetaDe(abierto_.toStdString()) / "chequeo.json");
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) guardado = j;
    }
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
    for (const auto& [id, texto] : itemsDeChequeo()) uno(id, texto);
    for (const auto& [id, g] : guardado.items())
        if (!vistos.count(id)) uno(id, g.value("texto", id));
    return out;
}

bool EventoViewModel::marcarChequeo(const QString& item, bool hecho, const QString& nota, const QString& texto) {
    if (abierto_.isEmpty() || item.trimmed().isEmpty()) return false;
    const auto ruta = store_.carpetaDe(abierto_.toStdString()) / "chequeo.json";
    json j = json::object();
    {
        std::ifstream f(ruta);
        const json x = json::parse(f, nullptr, false);
        if (x.is_object()) j = x;
    }
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
    std::ofstream o(ruta);
    o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!o) return false;
    emit chequeoChanged();
    return true;
}

} // namespace ba::app
