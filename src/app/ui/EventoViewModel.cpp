// ============================================================================
// src/app/ui/EventoViewModel.cpp — ver EventoViewModel.hpp
// ============================================================================
#include "i18n/I18n.hpp"

#include <QCoreApplication>
#include "ui/CarruselViewModel.hpp"
#include "emision/InputSpecs.hpp"
#include "ui/EjecutorLinea.hpp"
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
#include <limits>

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
#include <QUrl>

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "emision/BroadcastEngine.hpp"
#include "escenas/Conmutador.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Votaciones.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "evento/Condicion.hpp"
#include "evento/LineaIntercambio.hpp"
#include "evento/LineaPlantillas.hpp"
#include "medios/Playlist.hpp"
#include "ptz/ControlCamaras.hpp"
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
#include "persist/ColaEscritura.hpp"
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

/// Los textos se traducen al mostrarlos (tiposDeCue), en el contexto «evento».
constexpr CampoDeTipo kTipos[] = {
    {"escena", QT_TRANSLATE_NOOP("evento", "Escena"), QT_TRANSLATE_NOOP("evento", "Pone una escena al aire."), "escena"},
    {"musica", QT_TRANSLATE_NOOP("evento", "Música"), QT_TRANSLATE_NOOP("evento", "Arranca o para un tema de los medios del evento."), "archivo,accion,repetir"},
    {"video", QT_TRANSLATE_NOOP("evento", "Video"), QT_TRANSLATE_NOOP("evento", "Reproduce un video de los medios del evento en su propia escena."), "archivo,accion,repetir,alAire"},
    {"teleprompter", QT_TRANSLATE_NOOP("evento", "Teleprompter"), QT_TRANSLATE_NOOP("evento", "Carga un discurso y lo deja listo."), "prompter,discurso,arrancar"},
    {"orador", QT_TRANSLATE_NOOP("evento", "Orador"), QT_TRANSLATE_NOOP("evento", "Le da la palabra a alguien, con su tiempo."), "participante"},
    {"grafico", QT_TRANSLATE_NOOP("evento", "Gráfico"), QT_TRANSLATE_NOOP("evento", "Saca al aire un gráfico animado en una fuente de escena."),
     "fuente,grafico,accion,seVa"},
    {"carrusel", QT_TRANSLATE_NOOP("evento", "Carrusel"),
     QT_TRANSLATE_NOOP("evento", "Pone un carrusel en una lámina y lo muestra. Si en el medio lo cambiaste a mano desde la tarjeta, al llegar acá vuelve a esta."),
     "carrusel,lamina"},
    {"pantalla", QT_TRANSLATE_NOOP("evento", "Pantalla"), QT_TRANSLATE_NOOP("evento", "Manda contenido a una pantalla."), "pantalla,contenido"},
    {"audio", QT_TRANSLATE_NOOP("evento", "Audio"), QT_TRANSLATE_NOOP("evento", "Silencia una fuente, la activa o le cambia el volumen."), "accion,fuente,db"},
    {"pausa", QT_TRANSLATE_NOOP("evento", "Pausa"), QT_TRANSLATE_NOOP("evento", "Espera los segundos que diga la duración."), ""},
    {"espera", QT_TRANSLATE_NOOP("evento", "Espera al operador"), QT_TRANSLATE_NOOP("evento", "Frena el guion hasta que confirmes."), ""},
    {"nota", QT_TRANSLATE_NOOP("evento", "Nota"), QT_TRANSLATE_NOOP("evento", "No hace nada: es una indicación para vos."), ""},
};

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// El nombre fijo de la música del evento: un tema nuevo reemplaza al anterior en vez
/// de encimarse, que es lo peor que puede pasar en vivo.
constexpr const char* kMusica = "Música del evento";

using evento::linea::Accion;

} // namespace

EventoViewModel::EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala, graficos::Emisor* graficos,
                                 bool sinAbrirUltimo, QObject* parent)
    : QObject(parent), rt_(rt), sala_(sala), graficos_(graficos), store_(rt.dataRoot() / "Eventos") {
    // El reloj del evento va aparte del guion: cambia cada segundo y no tiene
    // sentido redibujar la lista de cues por eso.
    // Y de paso deja puesto el estado vivo (la sección `linea` del documento de aire): «cada segundo y en
    // cada cambio». Lo que entra solo lo programa cada EjecutorLinea a la hora justa.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() {
        escribirVivo();
        emit relojChanged();
    });
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
    // Tras un corte de luz, la línea que estaba corriendo vuelve en HOLD y pregunta qué
    // está pasando (docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.8). Un rato después, como la
    // playlist: que el aire ya haya vuelto (T47) y estén el conmutador y los gráficos.
    // Sin corte (se cerró bien), lo que haya quedado escrito no es de un corte.
    if (rt.corteAnterior())
        QTimer::singleShot(ba::ajustes::entero("arranque.retomarLineaMs", 2500), this, &EventoViewModel::retomarLinea);
    else
        borrarVivo();
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
    paginas << QVariantMap{{"pagina", "portada.html"}, {"nombre", tr("Portada (antes de empezar)")},
                           {"existe", std::filesystem::exists(dir / "portada.html", ec)}};
    for (std::size_t i = 0; i < e_.bloques.size(); ++i) {
        const std::string p = evento::paginaDeBloque(e_.bloques[i], i);
        paginas << QVariantMap{{"pagina", QString::fromStdString(p)},
                               {"nombre", QString::fromStdString(e_.bloques[i].nombre)},
                               {"existe", std::filesystem::exists(dir / p, ec)}};
    }
    paginas << QVariantMap{{"pagina", "cierre.html"}, {"nombre", tr("Cierre (al terminar)")},
                           {"existe", std::filesystem::exists(dir / "cierre.html", ec)}};
    const evento::SitioWeb s = evento::SitioWeb::leer(dir, e_);
    return {{"hay", hay},
            {"carpeta", carpetaWebAbs()},
            {"paginas", paginas},
            {"sitio", QJsonDocument::fromJson(QByteArray::fromStdString(s.aJson().dump())).object().toVariantMap()},
            {"aviso", avisoWeb()}};
}

QVariantMap EventoViewModel::generarWeb(bool sobrescribir) {
    if (abierto_.isEmpty()) return {{"error", tr("No hay ningún evento abierto.")}};
    const evento::ResultadoWeb r = evento::generar(e_, carpetaWebAbs().toStdWString(), sobrescribir);
    QStringList esc, sal;
    for (const auto& x : r.escritas) esc << QString::fromStdString(x);
    for (const auto& x : r.salteadas) sal << QString::fromStdString(x);
    ++webVersion_;
    webUltimo_.clear();
    emit webChanged();
    publicarWeb();
    if (!r.error.empty()) return {{"error", QString::fromStdString(r.error)}, {"escritas", esc}, {"salteadas", sal}};
    anotar(tr("Página web del evento: %1 archivos generados").arg(QString::number(esc.size())), "evento");
    return {{"escritas", esc}, {"salteadas", sal}, {"carpeta", carpetaWebAbs()}};
}

QString EventoViewModel::setWebSitio(const QVariantMap& campos) {
    if (abierto_.isEmpty()) return tr("No hay ningún evento abierto.");
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
            return tr("El subdominio va solo con letras minúsculas y números.");
        s.subdominio = sub;
    }
    if (!s.guardar(dir)) return tr("No se pudo guardar sitio.json.");
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
    if (abierto_.isEmpty()) return tr("No hay ningún evento abierto.");
    const std::string rel = evento::resolverPagina(e_, pagina.toStdString());
    if (rel.empty()) return tr("No conozco la página «%1»: portada, cierre, una sección o un .html propio.").arg(pagina);
    QDir().mkpath(carpetaWebAbs());
    QFile f(carpetaWebAbs() + "/" + QString::fromStdString(rel));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return tr("No se pudo escribir %1").arg(QString::fromStdString(rel));
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
    if (!t.isEmpty()) anotar(tr("Aviso en la página del evento: %1").arg(t), "evento");
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
        if (corrida_) {
            switch (corrida_->estado()) {
                case evento::linea::EstadoCorrida::Corriendo: est = "en_vivo"; break;
                case evento::linea::EstadoCorrida::Pausada: est = "pausa"; break;
                case evento::linea::EstadoCorrida::Terminada: est = "terminado"; break;
                default: break;
            }
            if (est == "en_vivo" || est == "pausa") bloque = cursor_.bloque;
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
        if (corriendo() && bloque >= 0 && bloque < static_cast<int>(e_.bloques.size())) {
            // Lo que le falta a la sección actual según lo previsto, y quién habla (si el cue
            // que corre es de orador: el que tiene la palabra se ve al aire, no es privado).
            const auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
            int falta = 0;
            for (int c = cursor_.cue; c < static_cast<int>(cues.size()); ++c)
                falta += cues[static_cast<std::size_t>(c)].duracionSeg;
            x.restanteActualSeg = std::max(0, falta - enCueSeg(ahoraMs()));
            if (const evento::Cue* c = cuePtrConst(cursor_.bloque, cursor_.cue); c != nullptr && c->tipo == evento::Tipo::Orador)
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
            emit notify(tr("No se pudo crear el evento del día: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo poner en curso: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo mover: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo crear la carpeta: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo guardar la ficha: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo crear el evento: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("No se pudo abrir el evento: %1").arg(QString::fromStdString(err)), "error");
        return false;
    }
    parar();
    ejecutores_.clear();
    corrida_.reset();
    e_ = std::move(e);
    ba::i18n::usarEnEvento(e_.idioma); // lo que ven y escuchan el público y los invitados
    abierto_ = carpeta;
    // LA LÍNEA MANDA: la del archivo o, en un evento de antes (v1), la migración del
    // guion de cues, con una copia del v1 en Respaldos/ y el archivo pasado a v2.
    bool migrado = false;
    QString avisoMigracion; // a la bitácora, después de limpiarla
    if (e_.linea.is_object()) {
        linea_ = evento::linea::Linea::deJson(e_.linea);
    } else {
        linea_ = evento::linea::migrar(e_);
        migrado = !e_.bloques.empty();
    }
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    if (migrado) {
        const auto dir = store_.carpetaDe(carpeta.toStdString());
        std::error_code ec;
        std::filesystem::create_directories(dir / "Respaldos", ec);
        if (!std::filesystem::exists(dir / "Respaldos" / "evento-v1.json", ec))
            std::filesystem::copy_file(dir / "evento.json", dir / "Respaldos" / "evento-v1.json", ec);
        e_.linea = linea_.aJson();
        std::string errMig;
        avisoMigracion = store_.guardar(carpeta.toStdString(), e_, &errMig)
                             ? tr("El guion pasó a la línea de tiempo: la copia de antes quedó en Respaldos/evento-v1.json")
                             : tr("No se pudo guardar el guion pasado a la línea de tiempo: %1").arg(QString::fromStdString(errMig));
    }
    sucio_ = false;
    reparos_.clear();
    bitacora_.clear();
    if (!avisoMigracion.isEmpty()) anotar(avisoMigracion, "info");
    // El padron viene con el evento. Que no se pueda leer NO impide abrirlo:
    // el guion es lo que hay que poder correr, y quedarse sin evento por un
    // padron roto seria perder el acto entero por la lista de invitados.
    std::string errPadron;
    bool okPadron = false;
    padron_ = evento::cargarPadron(store_.carpetaDe(carpeta.toStdString()), &okPadron, &errPadron);
    if (!okPadron)
        emit notify(tr("El padron de este evento no se pudo leer: %1").arg(QString::fromStdString(errPadron)),
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
    // Lo que la línea mira afuera; un bloque del orden del día se arma ya (si el resto
    // de la app todavía no está, se arma al correr).
    enchufar();
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
    ejecutores_.clear();
    corrida_.reset();
    buzon_.stop();
    e_ = {};
    linea_ = {};
    proyeccion_ = {};
    ba::i18n::usarEnEvento({});
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
        emit notify(tr("No se pudo borrar: %1").arg(QString::fromStdString(err)), "error");
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
                       {"idioma", QString::fromStdString(e_.idioma)},
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
                               {"titulo", QCoreApplication::translate("evento", t.titulo)},
                               {"ayuda", QCoreApplication::translate("evento", t.ayuda)},
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

QVariantList EventoViewModel::opcionesDeCampo(const QString& tipo, const QString& campo, const QVariantMap& datos) const {
    QVariantList out;
    const auto op = [&out](const QString& valor, const QString& texto = {}) {
        out.append(QVariantMap{{"valor", valor}, {"texto", texto.isEmpty() ? valor : texto}});
    };
    if (campo == "accion") {
        if (tipo == "musica" || tipo == "video") {
            op("arrancar", tr("Arrancar"));
            op("parar", tr("Parar"));
        } else if (tipo == "grafico") {
            op("mostrar", tr("Mostrar"));
            op("ocultar", tr("Sacar"));
            op("prender", tr("Prender el rótulo automático"));
            op("apagar", tr("Apagar el rótulo automático"));
        } else if (tipo == "audio") {
            op("silenciar", tr("Silenciar"));
            op("activar", tr("Activar"));
            op("volumen", tr("Cambiar el volumen"));
        }
    } else if (campo == "escena") {
        for (const std::string& e : rt_.engine().sceneNames()) op(QString::fromStdString(e));
    } else if (campo == "carrusel" && carruseles_ != nullptr) {
        for (const QString& n : carruseles_->nombres()) op(n);
    } else if (campo == "lamina" && carruseles_ != nullptr) {
        // Las del carrusel elegido en este cue, con su título: «3 · El derrumbe micénico».
        for (const QVariant& v : carruseles_->laminasDe(datos.value("carrusel").toString())) {
            const QVariantMap l = v.toMap();
            op(QString::number(l.value("numero").toInt()),
               tr("%1 · %2").arg(l.value("numero").toInt()).arg(l.value("titulo").toString()));
        }
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
    } else if (campo == "seVa") {
        op("", tr("Lo que diga el gráfico"));
        op("al-avanzar", tr("Al avanzar a otra cue"));
        op("sola", tr("Sola (a los «cuándo se va» ms)"));
        op("queda", tr("Queda hasta que la saquen"));
    } else if (campo == "grafico") {
        for (const QString& g : graficosDisponibles()) op(g);
    } else if (campo == "fuente" && tipo == "grafico") {
        for (const QString& f : fuentesDeGrafico()) op(f);
    } else if (campo == "fuente" && tipo == "audio") {
        for (const std::string& f : rt_.engine().audioSources()) op(QString::fromStdString(f));
    } else if (campo == "prompter") {
        // Los teleprompters del evento (cada uno con su página): el principal primero.
        for (const auto& p : listaPrompters().lista())
            op(QString::fromStdString(p.value("id", std::string())), QString::fromStdString(p.value("nombre", std::string())));
    } else if (campo == "contenido" && tipo == "pantalla") {
        op("programa", tr("El programa (lo que sale al aire)"));
        op("reloj", tr("Un reloj"));
        op("prompter", tr("El teleprompter"));
        op("tally", tr("Quién está al aire"));
        op("votacion", tr("La votación"));
    }
    return out;
}

QString EventoViewModel::enlaceDeCue(const QString& tipo, const QVariantMap& datos) const {
    // El servidor de la sala, en esta máquina (la pantalla del orador solo lee: sin clave).
    // Cada teleprompter tiene su página: el principal en /prompter/, los demás en /prompter/<id>/.
    if (tipo == "teleprompter") {
        const QString id = datos.value("prompter").toString();
        const QString carpeta = id.isEmpty() || id == sala::ListaPrompters::kPrincipal || !listaPrompters().existe(id.toStdString())
                                    ? QString()
                                    : id + "/";
        return "http://127.0.0.1:" + QString::number(rt_.gateway().port) + "/prompter/" + carpeta;
    }
    return {};
}

sala::ListaPrompters EventoViewModel::listaPrompters() const {
    sala::ListaPrompters l;
    const QString discursos = carpetaDeDiscursos();
    if (!discursos.isEmpty()) l.cargar(QDir::cleanPath(QDir(discursos).absoluteFilePath("../prompters.json")).toStdString());
    return l;
}

QVariantList EventoViewModel::escenarios() const {
    // Las guías son del operador (para él y el asistente): en su idioma.
    const std::string indice = ba::i18n::leer("guias/escenarios/indice.json");
    return QJsonDocument::fromJson(QByteArray::fromStdString(indice)).array().toVariantList();
}

QString EventoViewModel::pedidoDeEscenario(const QString& id, const QString& notas, const QString& plantilla) const {
    QString titulo = id;
    for (const QVariant& v : escenarios())
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
    if (!notas.trimmed().isEmpty())
        p += QCoreApplication::translate("asistente", " Lo que ya sé: %1").arg(notas.trimmed());
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
    lineaDesdeGuion();
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

void EventoViewModel::setIdioma(const QString& codigo) {
    e_.idioma = codigo.toStdString();
    ba::i18n::usarEnEvento(e_.idioma);
    marcarSucio();
}

QVariantList EventoViewModel::idiomas() const {
    // "" = el del operador; después los que hay, con su nombre en su idioma.
    QVariantList out{QVariantMap{
        {"value", QString()},
        {"label", QString::fromStdString(ba::i18n::llenar(ba::i18n::traducir("evento", "El del programa (%1)"),
                                                          {ba::i18n::actual().nombre}))}}};
    for (const auto& p : ba::i18n::idiomas())
        out << QVariantMap{{"value", QString::fromStdString(p.codigo)}, {"label", QString::fromStdString(p.nombre)}};
    return out;
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
    b.nombre = nombre.isEmpty() ? tr("Bloque nuevo").toStdString() : nombre.toStdString();
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
    e_.linea = linea_.aJson(); // lo que manda; `bloques` va como su lectura
    std::string err;
    if (!store_.guardar(abierto_.toStdString(), e_, &err)) {
        emit notify(tr("No se pudo guardar el guion: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("El guion no tiene ningún cue"), "error");
        return;
    }
    // Se camina el recorrido de verdad, no la lista: asi se ve el orden real
    // y quedan a la vista los bloques que nunca se visitan.
    const std::vector<std::string> escenas = rt_.engine().sceneNames();
    int visitados = 0;
    int problemas = 0;
    int sinEjecutar = 0;
    bitacora_.clear();

    // En el orden en que pasa: bloque por bloque y cue por cue.
    for (std::size_t bi = 0; bi < e_.bloques.size(); ++bi)
        for (std::size_t ci = 0; ci < e_.bloques[bi].cues.size(); ++ci) {
        const evento::Cue& c = e_.bloques[bi].cues[ci];
        ++visitados;
        const QString donde = QString("%1.%2").arg(bi + 1).arg(ci + 1);
        const QString que = c.texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c.tipo))
                                            : QString::fromStdString(c.texto);
        const std::string falta = c.queLeFalta();
        if (!falta.empty()) {
            ++problemas;
            anotar(tr("%1 %2 — %3").arg(donde, que, QString::fromStdString(falta)), "error");
            continue;
        }
        if (c.tipo == evento::Tipo::Escena) {
            const std::string escena = c.datos.value("escena", std::string());
            if (std::find(escenas.begin(), escenas.end(), escena) == escenas.end()) {
                ++problemas;
                anotar(tr("%1 %2 — no existe la escena \"%3\"").arg(donde, que, QString::fromStdString(escena)), "error");
                continue;
            }
        }
        if (!evento::ejecutable(c.tipo)) {
            ++sinEjecutar;
            anotar(tr("%1 %2 — el guion lo tiene, pero todavía no sé ejecutarlo").arg(donde, que), "info");
            continue;
        }
        anotar(tr("%1 %2 — listo").arg(donde, que), "ok");
        }

    // Bloques que el recorrido nunca pisa: estan escritos pero no pasan.
    int colgados = 0;
    for (const evento::Bloque& b : e_.bloques)
        if (b.cues.empty()) ++colgados;

    QString resumen =
        tr("Ensayo acelerado: %1 de %2 cues recorridos").arg(visitados).arg(e_.cantidadDeCues());
    if (colgados > 0) resumen += tr(", %n bloque vacío que no pasa", "", colgados);
    if (sinEjecutar > 0) resumen += tr(", %1 sin ejecución todavía").arg(sinEjecutar);
    if (problemas > 0) resumen += tr(", %1 con problemas").arg(problemas);
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
    if (!corrida_) return fallo(tr("El guion no se corrio en esta sesion: no hay de donde sacar los tiempos."));
    const std::string salida = salidaPorOmision();
    const auto& pasos = corrida_->instancias();
    if (pasos.empty()) return fallo(tr("El guion no avanzo ningun cue todavia."));
    if (recInicioMs <= 0)
        return fallo(tr("No se cuando arranco la grabacion. Los capitulos se miden desde ahi, no desde "
                        "el arranque del evento: si el guion empezo antes, todas las marcas estarian "
                        "corridas."));

    QVariantList caps;
    int antesDeGrabar = 0;
    std::string bloqueId;
    const auto& bloques = corrida_->linea().bloques;
    for (const evento::linea::Instancia& p : pasos) {
        if (p.salida != salida) continue;
        // Un capitulo por BLOQUE: se toma el primer paso de cada tramo.
        const evento::linea::Bloque* b = corrida_->linea().bloqueDe(p.paso);
        if (b == nullptr || b->id == bloqueId) continue;
        bloqueId = b->id;
        const int bloqueIdx = static_cast<int>(b - bloques.data());
        const int seg = static_cast<int>((p.desdeMs - recInicioMs) / 1000);
        // Lo que paso ANTES de apretar grabar no esta en el video. Se cuenta y
        // se avisa, en vez de inventarle un 0:00 a algo que no se grabo.
        if (seg < 0) {
            ++antesDeGrabar;
            continue;
        }
        const QString nombre = b->nombre.empty() ? tr("Bloque %1").arg(bloqueIdx + 1) : QString::fromStdString(b->nombre);
        caps.append(QVariantMap{{"seg", seg}, {"marca", marcaDe(seg)}, {"titulo", nombre}});
    }
    if (caps.isEmpty()) return fallo(tr("Todos los bloques empezaron antes de que arrancara la grabacion."));

    // YouTube pide que el PRIMERO sea 0:00. Si el guion arranco despues de
    // apretar grabar (lo normal: se graba el ambiente antes), se antepone uno
    // para lo que haya quedado ahi adelante en vez de mover el resto.
    QVariantMap primero = caps.first().toMap();
    if (primero.value("seg").toInt() > 0)
        caps.prepend(QVariantMap{{"seg", 0}, {"marca", "0:00"}, {"titulo", tr("Antes de empezar")}});

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
                                                      : tr("YouTube ignora las listas de menos de tres "
                                                           "capitulos: quedaron %1.")
                                                            .arg(limpios.size())},
                       {"capitulos", limpios},
                       {"texto", texto},
                       {"antesDeGrabar", antesDeGrabar}};
}

void EventoViewModel::pedirInvitacion(const QVariantMap& quien, bool remoto,
                                      std::function<void(const QVariantMap&)> luego) {
    if (sala_ == nullptr) return luego(QVariantMap{{"ok", false}, {"motivo", tr("El servidor no está disponible.")}});
    const QString codigo = quien.value("codigo").toString();
    // El nombre y la fecha salen del evento; el enlace, del gateway.
    const QString nombreEvento = e_.nombre.empty() ? abierto_ : QString::fromStdString(e_.nombre);
    // La invitación la lee el invitado: en el idioma del evento abierto (el que fijó
    // abrir() / setIdioma() con ba::i18n::usarEnEvento). Se arma cada vez que se pide.
    // El idioma se toma ahora: la respuesta del gateway llega después.
    const ba::i18n::En ev(ba::i18n::idiomaDelEvento());
    QString cuandoTexto = QString::fromStdString(e_.fecha);
    if (!e_.inicio.empty())
        cuandoTexto = qs(ba::i18n::llenar(ba::i18n::traducirEvento("evento", "%1 a las %2"), {e_.fecha, e_.inicio}));
    if (!e_.huso.empty()) cuandoTexto += " (" + QString::fromStdString(e_.huso) + ")";
    const QString instante = cuando();

    // El mismo enlace de la página del público (modelo: sala::EnlacesSala).
    sala_->enlaces().publico(
        remoto, [luego, quien, codigo, nombreEvento, cuandoTexto, instante, ev](const sala::Enlace& e) {
            if (!e.ok)
                return luego(QVariantMap{
                    {"ok", false}, {"motivo", tr("No pude pedirle el enlace a la conferencia. Fijate `room_status`.")}});
            const auto t1 = [&ev](const char* texto, const QString& dato) {
                return qs(ba::i18n::llenar(ev.traducir("evento", texto), {dato.toStdString()}));
            };
            // El código va en el FRAGMENTO, con la clave: no viaja en la petición
            // ni queda en los registros de ningún servidor por el que pase.
            const QString url = QString::fromStdString(e.url) + "&c=" + codigo;

            QString t;
            t += t1(QT_TRANSLATE_NOOP("evento", "Te esperamos en %1."), nombreEvento) + "\n\n";
            if (cuandoTexto.trimmed().isEmpty())
                t += qs(ev.traducir("evento", "(El evento todavía no tiene fecha en el guion.)")) + "\n\n";
            else
                t += t1(QT_TRANSLATE_NOOP("evento", "Cuándo: %1"), cuandoTexto.trimmed()) + "\n";
            if (!instante.isEmpty()) t += t1(QT_TRANSLATE_NOOP("evento", "En tu huso horario: %1"), instante) + "\n";
            t += "\n" + qs(ev.traducir("evento", "Este es tu enlace personal:")) + "\n" + url + "\n\n";
            t += qs(ev.traducir("evento", "Escanealo o abrilo al llegar: queda registrado que llegaste, y desde ahí podés "
                                          "preguntar y votar.")) +
                 "\n";
            t += t1(QT_TRANSLATE_NOOP("evento", "Tu código, por si te lo piden: %1"), codigo) + "\n";
            if (!quien.value("nota").toString().isEmpty())
                t += "\n" + t1(QT_TRANSLATE_NOOP("evento", "Nota: %1"), quien.value("nota").toString()) + "\n";

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
    // El acta es del evento: va en su idioma (el que fijó abrir() / setIdioma() con
    // ba::i18n::usarEnEvento; los anexos de la asamblea usan el mismo). Se arma cada vez.
    const auto T = [](const char* texto) { return qs(ba::i18n::traducirEvento("evento", texto)); };
    t += "# " + T(QT_TRANSLATE_NOOP("evento", "Acta — %1")).arg(titulo) + "\n\n";

    QString cuandoTexto = QString::fromStdString(e_.fecha);
    if (!e_.inicio.empty()) cuandoTexto += " " + QString::fromStdString(e_.inicio);
    if (!e_.huso.empty()) cuandoTexto += " (" + QString::fromStdString(e_.huso) + ")";
    t += cuandoTexto.trimmed().isEmpty() ? T(QT_TRANSLATE_NOOP("evento", "**Fecha:** sin fecha en el guion")) + "\n\n"
                                         : T(QT_TRANSLATE_NOOP("evento", "**Fecha:** %1")).arg(cuandoTexto.trimmed()) + "\n\n";

    // --- El quorum, arriba y con sus DOS numeros ---------------------------
    // Un acta que dice «se aprobo por 8 votos a favor» sin decir cuantos podian
    // votar no prueba nada.
    t += "## " + T(QT_TRANSLATE_NOOP("evento", "Asistencia")) + "\n\n";
    t += "- " +
         (r.sinDato > 0
              ? T(QT_TRANSLATE_NOOP("evento", "Presentes: **%1** de %2 en el padrón (%3 en el lugar, %4 en línea, %5 "
                                              "sin dato de por dónde)"))
                    .arg(r.presentes).arg(r.total).arg(r.presenciales).arg(r.enLinea).arg(r.sinDato)
              : T(QT_TRANSLATE_NOOP("evento", "Presentes: **%1** de %2 en el padrón (%3 en el lugar, %4 en línea)"))
                    .arg(r.presentes).arg(r.total).arg(r.presenciales).arg(r.enLinea)) +
         "\n";
    if (r.conVoto > 0)
        t += "- " +
             T(QT_TRANSLATE_NOOP("evento", "Con derecho a voto presentes: **%1** de %2 — **quórum**"))
                 .arg(r.votoPresente)
                 .arg(r.conVoto) +
             "\n";
    t += "- " + T(QT_TRANSLATE_NOOP("evento", "No llegaron: %1")).arg(r.faltan) + "\n\n";

    t += QString("| %1 | %2 | %3 | %4 | %5 |\n|---|---|---|---|---|\n")
             .arg(T(QT_TRANSLATE_NOOP("evento", "Nombre")), T(QT_TRANSLATE_NOOP("evento", "Papel")),
                  T(QT_TRANSLATE_NOOP("evento", "Por dónde")), T(QT_TRANSLATE_NOOP("evento", "Llegó")),
                  T(QT_TRANSLATE_NOOP("evento", "Voto")));
    for (const evento::Asistente& a : padron_.gente()) {
        const bool llego = a.estado == evento::Estado::Llego || a.llegoMs > 0;
        const QString hora =
            a.llegoMs > 0 ? QDateTime::fromMSecsSinceEpoch(a.llegoMs).toString("HH:mm") : QString("—");
        t += QString("| %1 | %2 | %3 | %4 | %5 |\n")
                 .arg(QString::fromStdString(a.nombre).isEmpty() ? T(QT_TRANSLATE_NOOP("evento", "(sin nombre)"))
                                                                 : QString::fromStdString(a.nombre))
                 .arg(QString::fromStdString(evento::etiquetaDePapel(a.papel)))
                 .arg(llego && !a.porDonde.empty() ? QString::fromStdString(a.porDonde)
                                                   : QString::fromStdString(evento::etiquetaDeCanal(a.canal)))
                 .arg(hora)
                 .arg(a.vota ? T(QT_TRANSLATE_NOOP("evento", "sí")) : QString("—"));
    }
    t += "\n";

    // --- Lo que se decidio --------------------------------------------------
    t += "## " + T(QT_TRANSLATE_NOOP("evento", "Lo que se votó")) + "\n\n";
    if (votaciones.isEmpty()) {
        t += T(QT_TRANSLATE_NOOP("evento", "No se votó nada en este evento.")) + "\n\n";
    } else {
        int n = 0;
        for (const QJsonValue& vv : votaciones) {
            const QJsonObject v = vv.toObject();
            ++n;
            t += QString("### %1. %2\n\n").arg(n).arg(v.value("pregunta").toString());
            const bool formal = v.value("modo").toString() == "formal";
            // Se dice CUAL fue: una encuesta leida como decision es el error
            // que esta separacion existe para evitar.
            t += "*" + (formal ? T(QT_TRANSLATE_NOOP("evento", "Votación formal"))
                               : T(QT_TRANSLATE_NOOP("evento", "Encuesta (resultado indicativo)")));
            // Lo que vota el público es una consulta: no reemplaza la votación del cuerpo.
            if (!v.value("vinculante").toBool()) t += T(QT_TRANSLATE_NOOP("evento", " — consulta, no vinculante"));
            if (v.value("secreta").toBool()) t += T(QT_TRANSLATE_NOOP("evento", ", secreta"));
            t += "*\n\n";
            const int votaron = v.value("votaron").toInt();
            const int podian = v.value("podian").toInt();
            t += (podian > 0 ? T(QT_TRANSLATE_NOOP("evento", "Votaron %1 de %2.")).arg(votaron).arg(podian)
                             : T(QT_TRANSLATE_NOOP("evento", "Votaron %1.")).arg(votaron)) +
                 "\n\n";
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
                t += T(QT_TRANSLATE_NOOP("evento", "No votaron: %1")).arg(ns.join(", ")) + "\n\n";
            }
        }
    }

    if (anexoActa_) t += anexoActa_();
    t += "---\n\n_" +
         T(QT_TRANSLATE_NOOP("evento", "Generada por Broadcast Appliance el %1."))
             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")) +
         "_\n";
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
    // Un renglón por paso que salió (lo emitido de la salida por omisión).
    if (!corrida_) return {};
    const std::string salida = salidaPorOmision();
    const auto& pasos = corrida_->instancias();
    if (pasos.empty()) return {};

    // Se agrupa por bloque respetando el ORDEN EN QUE PASO, no el del guion:
    // si el operador salto o repitio, el informe tiene que contarlo.
    QVariantList bloques;
    QVariantMap bloqueActual;
    QVariantList cuesDelBloque;
    std::string bloqueId;
    bool hayBloque = false;
    int bPrevisto = 0;
    int bReal = 0;
    int totalPrevisto = 0;
    int totalReal = 0;
    int nro = 0;

    const auto cerrarBloque = [&]() {
        if (!hayBloque) return;
        bloqueActual["cues"] = cuesDelBloque;
        bloqueActual["previstoSeg"] = bPrevisto;
        bloqueActual["realSeg"] = bReal;
        bloqueActual["desvioSeg"] = bReal - bPrevisto;
        bloques.append(bloqueActual);
        cuesDelBloque.clear();
        bPrevisto = 0;
        bReal = 0;
    };

    for (const evento::linea::Instancia& p : pasos) {
        // El último paso puede seguir al aire: no tiene duración real todavía.
        if (p.salida != salida || p.hastaMs == 0) continue;
        const evento::linea::Bloque* b = corrida_->linea().bloqueDe(p.paso);
        const std::string id = b ? b->id : std::string();
        if (!hayBloque || id != bloqueId) {
            cerrarBloque();
            hayBloque = true;
            bloqueId = id;
            bloqueActual = QVariantMap{{"nombre", b ? QString::fromStdString(b->nombre) : tr("(bloque borrado)")}};
        }
        const evento::linea::Paso* paso = corrida_->linea().paso(p.paso);
        // El tipo es el del cue con que empieza el paso en el guion de cues.
        const auto [cb, cc] = proyeccion_.cueDe(p.paso);
        const evento::Cue* c = cuePtrConst(cb, cc);
        const int previsto = paso ? static_cast<int>(paso->duracionPlanMs() / 1000) : 0;
        const int real = static_cast<int>((p.hastaMs - p.desdeMs) / 1000);
        cuesDelBloque.append(QVariantMap{
            {"nro", ++nro},
            {"paso", QString::fromStdString(p.paso)},
            {"texto", paso ? QString::fromStdString(paso->nombre) : QString::fromStdString(p.paso)},
            {"tipo", c != nullptr ? QString::fromStdString(evento::nombreDeTipo(c->tipo)) : QString("paso")},
            {"entro", QString::fromUtf8(evento::linea::nombreDe(p.comoEntro))},
            {"previstoSeg", previsto},
            {"realSeg", real},
            {"desvioSeg", real - previsto}});
        bPrevisto += previsto;
        bReal += real;
        totalPrevisto += previsto;
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
        emit notify(tr("Todavía no hay nada que informar: el guion no se corrió"), "error");
        return {};
    }
    const QDir carpeta(QString::fromStdString(store_.carpetaDe(abierto_.toStdString()).string()));
    const QString nombre = "informe-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
    const QString ruta = carpeta.filePath(nombre);
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit notify(tr("No se pudo escribir el informe en %1").arg(ruta), "error");
        return {};
    }
    f.write(QJsonDocument(QJsonObject::fromVariantMap(inf)).toJson());
    emit notify(tr("Informe guardado en %1").arg(nombre), "ok");
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
                                        tr("no existe un grafico llamado \"%1\"").arg(qs(g)).toStdString()});
                // La fuente se marca SIN gravedad: es normal escribir el guion
                // antes de armar las escenas, y se arregla agregando la fuente.
                const std::string f = c.datos.value("fuente", std::string());
                if (!f.empty() && graficos_->fuentePorNombre(QString::fromStdString(f)) == 0)
                    reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), false,
                                        tr("todavia no hay ninguna fuente de grafico llamada \"%1\": agregala a la "
                                           "escena donde tiene que verse")
                                            .arg(qs(f))
                                            .toStdString()});
                continue;
            }
            if (c.tipo != evento::Tipo::Escena) continue;
            const std::string nombre = c.datos.value("escena", std::string());
            if (nombre.empty()) continue; // ya lo dijo la revision del modelo
            if (std::find(escenas.begin(), escenas.end(), nombre) == escenas.end()) {
                reparos_.push_back({static_cast<int>(bi), static_cast<int>(ci), true,
                                    tr("no existe una escena llamada \"%1\"").arg(qs(nombre)).toStdString()});
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
                                    tr("el programa todavía no sabe ejecutar este tipo de cue").toStdString()});
    emit reparosChanged();

    int graves = 0;
    for (const evento::Reparo& r : reparos_)
        if (r.grave) ++graves;
    if (reparos_.empty())
        emit notify(tr("La revisión no encontró problemas"), "ok");
    else if (graves > 0)
        emit notify(tr("La revisión encontró %n problema que impide correr el guion", "", graves),
                    "error");
    else
        emit notify(tr("La revisión encontró cosas para mirar, ninguna grave"), "info");
}

// --- Correr -----------------------------------------------------------------

bool EventoViewModel::corriendo() const {
    if (!corrida_) return false;
    const auto e = corrida_->estado();
    return e == evento::linea::EstadoCorrida::Corriendo || e == evento::linea::EstadoCorrida::Pausada;
}

QString EventoViewModel::estado() const {
    if (!corrida_) return "parado";
    switch (corrida_->estado()) {
        case evento::linea::EstadoCorrida::Corriendo: return "corriendo";
        case evento::linea::EstadoCorrida::Pausada: return "pausado";
        case evento::linea::EstadoCorrida::Terminada: return "terminado";
        default: return "parado";
    }
}

bool EventoViewModel::guionCorriendo() const {
    // Pausado tambien cuenta: el evento esta pasando, el operador solo freno
    // el guion un momento.
    return !ensayo_ && corriendo();
}

QVariantMap EventoViewModel::actual() const {
    if (!corriendo()) return {};
    if (const evento::Cue* c = cuePtrConst(cursor_.bloque, cursor_.cue)) {
        QVariantMap m = cueAMap(*c, cursor_.bloque, cursor_.cue);
        m["bloqueNombre"] = QString::fromStdString(e_.bloques[static_cast<std::size_t>(cursor_.bloque)].nombre);
        if (const auto* r = proyeccion_.ref(cursor_.bloque, cursor_.cue)) m["paso"] = QString::fromStdString(r->paso);
        return m;
    }
    // Un paso que no está en el guion de cues (uno insertado del estante).
    const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision());
    const evento::linea::Paso* p = a ? corrida_->linea().paso(a->paso) : nullptr;
    if (p == nullptr) return {};
    return QVariantMap{{"paso", QString::fromStdString(p->id)},     {"texto", QString::fromStdString(p->nombre)},
                       {"tipo", "paso"},                             {"bloque", -1},
                       {"cue", -1},                                  {"duracionSeg", static_cast<int>(p->duracionPlanMs() / 1000)}};
}

QVariantMap EventoViewModel::siguiente() const {
    if (!corriendo()) return {};
    const std::string s = salidaPorOmision();
    const auto mapa = [this](int b, int c) {
        QVariantMap m = cueAMap(*cuePtrConst(b, c), b, c);
        m["bloqueNombre"] = QString::fromStdString(e_.bloques[static_cast<std::size_t>(b)].nombre);
        if (const auto* r = proyeccion_.ref(b, c)) m["paso"] = QString::fromStdString(r->paso);
        return m;
    };
    // Lo que queda del paso al aire (lo que en el v1 era el cue siguiente)…
    const evento::linea::Instancia* a = corrida_->alAire(s);
    const auto [nb, nc] = cueSiguienteDe(cursor_.bloque, cursor_.cue);
    if (const auto* r = proyeccion_.ref(nb, nc); a && r && r->paso == a->paso && !r->pieza.empty() && !corrida_->disparada(s, r->pieza))
        return mapa(nb, nc);
    // …y si no, el próximo paso (el que diga la corrida: saltear y mover el próximo cuentan).
    const evento::linea::Resolucion res = corrida_->resolver(ahoraMs());
    const evento::linea::SalidaResuelta* sr = res.salida(s);
    if (sr == nullptr || sr->proximo.empty()) return {};
    const auto [b, c] = proyeccion_.cueDe(sr->proximo);
    if (cuePtrConst(b, c) != nullptr) return mapa(b, c);
    const evento::linea::Paso* p = corrida_->linea().paso(sr->proximo);
    return p ? QVariantMap{{"paso", QString::fromStdString(p->id)}, {"texto", QString::fromStdString(p->nombre)}, {"tipo", "paso"}}
             : QVariantMap{};
}

void EventoViewModel::moverCursor(int bloque, int cue) {
    const int64_t t = ahoraMs();
    cursor_ = {bloque, cue, t, corrida_ ? corrida_->pausadoMs(t) : 0};
}

std::pair<int, int> EventoViewModel::cueSiguienteDe(int bloque, int cue) const {
    const int n = static_cast<int>(e_.bloques.size());
    if (bloque < 0) bloque = 0, cue = -1;
    for (int b = bloque; b < n; ++b) {
        const int desde = b == bloque ? cue + 1 : 0;
        if (desde < static_cast<int>(e_.bloques[static_cast<std::size_t>(b)].cues.size())) return {b, desde};
    }
    return {-1, -1};
}

std::pair<int, int> EventoViewModel::cueAnteriorDe(int bloque, int cue) const {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return {-1, -1};
    if (cue > 0) return {bloque, cue - 1};
    for (int b = bloque - 1; b >= 0; --b) {
        const auto& cues = e_.bloques[static_cast<std::size_t>(b)].cues;
        if (!cues.empty()) return {b, static_cast<int>(cues.size()) - 1};
    }
    return {-1, -1};
}

int EventoViewModel::enCueSeg(int64_t ahora) const {
    if (!corrida_ || cursor_.desdeMs == 0) return 0;
    // Lo pausado mientras estaba en este cue no cuenta: el operador paró el evento.
    const int64_t pausa = corrida_->pausadoMs(ahora) - cursor_.pausadoAlEntrarMs;
    return static_cast<int>(std::max<int64_t>(0, ahora - cursor_.desdeMs - pausa) / 1000);
}

QVariantMap EventoViewModel::reloj() const {
    if (!corrida_) return QVariantMap{{"desvioSeg", 0}, {"enCueSeg", 0}, {"restanteSeg", 0}};
    const int64_t t = ahoraMs();
    // En que numero de cue vamos, para "12 de 34".
    int nro = 0;
    const Cursor& p = cursor_;
    for (int b = 0; b < static_cast<int>(e_.bloques.size()) && b <= p.bloque; ++b) {
        const int hasta =
            b == p.bloque ? p.cue + 1 : static_cast<int>(e_.bloques[static_cast<std::size_t>(b)].cues.size());
        nro += hasta;
    }
    // Back-timing (T46): lo que falta del guion si se sigue lo previsto desde
    // acá, lo que falta del bloque, a qué hora terminaría y, si el guion tiene
    // hora de inicio, contra qué hora de fin planeada se compara.
    int restanteGuion = 0, restanteBloque = 0, totalPrevisto = 0;
    const int enCue = enCueSeg(t);
    for (int b = 0; b < static_cast<int>(e_.bloques.size()); ++b) {
        const auto& cues = e_.bloques[static_cast<std::size_t>(b)].cues;
        for (int c = 0; c < static_cast<int>(cues.size()); ++c) {
            const int d = cues[static_cast<std::size_t>(c)].duracionSeg;
            totalPrevisto += d;
            if (!p.valido()) continue;
            int falta = 0;
            if (b > p.bloque || (b == p.bloque && c > p.cue)) falta = d;
            else if (b == p.bloque && c == p.cue) falta = std::max(0, d - enCue);
            restanteGuion += falta;
            if (b == p.bloque) restanteBloque += falta;
        }
    }
    // Lo que lleva el evento sin las pausas, y el desvío contra lo previsto para el cue
    // donde está: positivo = vamos tarde.
    const int transcurrido =
        corrida_->arranqueMs() > 0 ? static_cast<int>((t - corrida_->arranqueMs() - corrida_->pausadoMs(t)) / 1000) : 0;
    const int previsto = p.valido() ? e_.offsetDeCueSeg(static_cast<std::size_t>(p.bloque), static_cast<std::size_t>(p.cue)) : -1;
    const evento::Cue* c = cuePtrConst(p.bloque, p.cue);
    const std::string s = salidaPorOmision();
    QVariantMap m{{"desvioSeg", previsto >= 0 && corriendo() ? transcurrido - previsto : 0},
                  {"enCueSeg", enCue},
                  {"restanteSeg", c != nullptr && c->duracionSeg > 0 ? c->duracionSeg - enCue : 0},
                  {"cueNro", p.valido() ? nro : 0},
                  {"cuesTotal", static_cast<int>(e_.cantidadDeCues())},
                  {"restanteBloqueSeg", restanteBloque},
                  {"restanteGuionSeg", restanteGuion},
                  {"horaFinPrevista", QDateTime::fromMSecsSinceEpoch(t + restanteGuion * 1000LL).toString("HH:mm")},
                  {"transcurridoSeg", transcurrido},
                  {"totalPrevistoSeg", totalPrevisto},
                  // Lo del avance automático, para el MCP y el bus (la página usa las propiedades).
                  {"entraSoloEnSeg", corrida_->entraSoloEnSeg(s, t)},
                  {"hold", corrida_->hold(s)},
                  {"retoma", corrida_->retomando()}};
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

int EventoViewModel::entraSoloEnSeg() const { return corriendo() ? corrida_->entraSoloEnSeg(salidaPorOmision(), ahoraMs()) : 0; }

bool EventoViewModel::hold() const { return corriendo() && corrida_->hold(salidaPorOmision()); }

QVariantMap EventoViewModel::retoma() const {
    if (!corrida_ || !corrida_->retomando()) return QVariantMap{{"activa", false}};
    return QVariantMap{{"activa", true},
                       {"pideDescripcion", corrida_->pideDescripcion()},
                       {"descripcion", QString::fromStdString(corrida_->descripcion())},
                       {"resumen", deJson(corrida_->resumenRetoma())}};
}

void EventoViewModel::setEnsayo(bool on) {
    if (ensayo_ == on) return;
    ensayo_ = on;
    emit correChanged();
    anotar(on ? tr("Ensayo: el guion corre sin salir al aire") : tr("Ensayo apagado: el guion sale al aire"), "info");
}

void EventoViewModel::anotar(const QString& texto, const QString& tipo, bool alRegistro) {
    QVariantMap m{
        {"hora", QDateTime::currentDateTime().toString("HH:mm:ss")}, {"texto", texto}, {"tipo", tipo}};
    bitacora_.prepend(m);
    // La bitacora de la página es para mirar de reojo; la del histórico
    // (bitacora.jsonl del evento en curso) la toma del registro general.
    if (alRegistro)
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

void EventoViewModel::nuevaCorrida() {
    for (auto& [s, e] : ejecutores_)
        e->parar();
    ejecutores_.clear();
    corrida_ = std::make_unique<evento::linea::Corrida>(lineaDeCorrida(), entradaBase(true));
    cursor_ = {};
    palabras_.clear();
    grabado_ = nullptr;
    graficosDePieza_.clear();
    musicaDePieza_.clear();
    dskDePieza_.clear();
    ptzPendientes_.clear();
    mediosVigilados_.clear();
    playlistDePieza_.clear();
    // Un ejecutor por salida: su timer va a la hora justa de lo que le toca. El tick
    // es uno para todas (la corrida las resuelve juntas).
    for (const std::string& s : linea_.salidasEfectivas()) {
        auto e = std::make_unique<EjecutorLinea>(*corrida_, s, [this](const Accion& a) { hacerAccion(a); }, [] { return ahoraMs(); },
                                                 this);
        connect(e.get(), &EjecutorLinea::toca, this, [this]() {
            if (corrida_) despachar(corrida_->tick(ahoraMs()));
        });
        ejecutores_[s] = std::move(e);
    }
    // Lo que la línea mira afuera (el bus, el aire de cada salida, el orden del día).
    enchufar();
    vigilarTemas();
}

void EventoViewModel::despachar(const std::vector<Accion>& acc) {
    seguirPalabra(acc);
    for (const Accion& a : acc) {
        auto it = ejecutores_.find(a.salida);
        if (it == ejecutores_.end()) it = ejecutores_.find(salidaPorOmision());
        if (it != ejecutores_.end()) it->second->hacer(a);
    }
    for (auto& [s, e] : ejecutores_)
        e->programar();
    volcarRecorrido();
    // Terminó el ensayo que graba: los tiempos quedan para que una persona los confirme.
    if (!corriendo() && grabar_ && corrida_ && grabado_.is_null()) {
        grabado_ = evento::linea::tiemposGrabados(linea_, corrida_->instancias());
        if (!grabado_["ops"].empty())
            emit notify(tr("El ensayo grabó los tiempos de %n paso(s): confirmalos para que queden como duraciones previstas", "",
                           static_cast<int>(grabado_["ops"].size())),
                        "info");
    }
    if (corriendo()) {
        escribirVivo();
        // El reloj de la vista se realinea con el cambio: así la cuenta y el aviso
        // «entra solo en 0:05» cambian de segundo juntos.
        reloj_.start();
    } else {
        reloj_.stop();
        borrarVivo();
    }
    emit correChanged();
    emit relojChanged();
}

void EventoViewModel::arrancar() {
    std::size_t pasos = 0;
    for (const auto& b : linea_.bloques)
        pasos += b.pasos.size();
    if (pasos == 0) {
        emit notify(tr("El guion no tiene ningún cue"), "error");
        return;
    }
    // Correrlo de verdad lo pone en curso: lo que pase desde ahora es de este evento.
    if (!ensayo_ && !abierto_.isEmpty() && store_.enCurso() != abierto_.toStdString()) iniciarEvento(abierto_);
    nuevaCorrida();
    anotar(ensayo_ ? tr("Arranca el ensayo") : tr("Arranca el evento"), "ok");
    despachar(corrida_->arrancar(ahoraMs()));
}

void EventoViewModel::setHold(bool on) {
    if (!corriendo()) return;
    // Tras un corte, soltar el HOLD es seguir como estaba.
    if (!on && corrida_->retomando()) {
        seguirComoEstaba();
        return;
    }
    if (hold() == on || !corrida_->setHold(salidaPorOmision(), on, ahoraMs())) return;
    anotar(on ? tr("HOLD: este paso no se va solo") : tr("HOLD suelto: el paso vuelve a irse solo"), "info");
    despachar(corrida_->alDia(ahoraMs()));
}

void EventoViewModel::avanzar() {
    if (!corriendo()) {
        arrancar();
        return;
    }
    // Lo que en el v1 era el cue siguiente: si es una pieza del paso al aire que no
    // salió todavía (un adlib, o una que no llegó a su hora), se dispara ya; si no, GO.
    const std::string s = salidaPorOmision();
    const evento::linea::Instancia* a = corrida_->alAire(s);
    const auto [nb, nc] = cueSiguienteDe(cursor_.bloque, cursor_.cue);
    const auto* r = proyeccion_.ref(nb, nc);
    if (a && r && r->paso == a->paso && !r->pieza.empty() && !corrida_->disparada(s, r->pieza)) {
        despachar(corrida_->adlib(s, r->pieza, ahoraMs()));
        return;
    }
    despachar(corrida_->go(s, ahoraMs()));
}

void EventoViewModel::retroceder() {
    if (!corriendo() || !cursor_.valido()) return;
    const auto [pb, pc] = cueAnteriorDe(cursor_.bloque, cursor_.cue);
    const auto* r = proyeccion_.ref(pb, pc);
    if (r == nullptr) return;
    anotar(tr("Vuelve atrás"), "info");
    // Dentro del mismo paso: se vuelve a disparar esa pieza. Si no, se va a su paso.
    const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision());
    if (a && r->paso == a->paso && !r->pieza.empty()) {
        despachar(corrida_->adlib(salidaPorOmision(), r->pieza, ahoraMs()));
        moverCursor(pb, pc);
        emit correChanged();
        return;
    }
    irA(pb, pc);
}

void EventoViewModel::irA(int bloque, int cue) {
    const auto* r = proyeccion_.ref(bloque, cue);
    if (r == nullptr) return;
    const std::string s = salidaPorOmision();
    if (!corriendo()) {
        if (!ensayo_ && !abierto_.isEmpty() && store_.enCurso() != abierto_.toStdString()) iniciarEvento(abierto_);
        nuevaCorrida();
    }
    std::string err;
    std::vector<Accion> acc = corrida_->irA(s, r->paso, ahoraMs(), &err);
    if (!err.empty()) {
        anotar(QString::fromStdString(err), "error");
        despachar(acc);
        return;
    }
    // El cue elegido, si es una pieza del medio del paso, se hace ya (como en el v1).
    if (!r->pieza.empty() && !corrida_->disparada(s, r->pieza)) {
        const std::vector<Accion> mas = corrida_->adlib(s, r->pieza, ahoraMs());
        acc.insert(acc.end(), mas.begin(), mas.end());
    }
    anotar(tr("Salta al cue elegido"), "info");
    despachar(acc);
    moverCursor(bloque, cue);
    emit correChanged();
}

void EventoViewModel::pausar() {
    if (!corriendo()) return;
    corrida_->pausar(ahoraMs());
    anotar(tr("Guion en pausa"), "info");
    despachar({});
}

void EventoViewModel::reanudar() {
    if (!corriendo()) return;
    anotar(tr("Sigue el guion"), "info");
    despachar(corrida_->reanudar(ahoraMs()));
}

void EventoViewModel::parar() {
    if (!corriendo()) return;
    sacarAlAvanzar();
    despachar(corrida_->terminar(ahoraMs()));
}

// --- La línea corriendo -----------------------------------------------------------

evento::linea::Entrada EventoViewModel::entradaBase(bool hoy) const {
    evento::linea::Entrada e;
    const QTimeZone tz = e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
    const QTimeZone z = tz.isValid() ? tz : QTimeZone::systemTimeZone();
    QDate dia = QDateTime::currentDateTime(z).date();
    if (!hoy && evento::fechaValida(e_.fecha)) dia = QDate::fromString(QString::fromStdString(e_.fecha), "yyyy-MM-dd");
    e.medianocheMs = QDateTime(dia, QTime(0, 0), z).toMSecsSinceEpoch();
    if (evento::horaValida(e_.inicio))
        e.inicioPlaneadoMs = QDateTime(dia, QTime::fromString(QString::fromStdString(e_.inicio), "HH:mm"), z).toMSecsSinceEpoch();
    // Las cámaras de cada escena, para los conflictos de PTZ entre salidas: las fuentes
    // de la escena (una cámara es una entrada, y una PTZ nombra la suya).
    e.camarasDeEscena = [this](const std::string& escena) { return rt_.engine().sceneSources(escena); };
    return e;
}

std::string EventoViewModel::horaDe(int64_t ms) const {
    const QTimeZone tz = e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
    return QDateTime::fromMSecsSinceEpoch(ms, tz.isValid() ? tz : QTimeZone::systemTimeZone()).toString("HH:mm:ss").toStdString();
}

int64_t EventoViewModel::horaHoyMs(const std::string& hhmm) const {
    if (!evento::horaValida(hhmm)) return -1;
    const QTimeZone tz = e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
    const QTimeZone z = tz.isValid() ? tz : QTimeZone::systemTimeZone();
    return QDateTime(QDateTime::currentDateTime(z).date(), QTime::fromString(QString::fromStdString(hhmm), "HH:mm"), z)
        .toMSecsSinceEpoch();
}

void EventoViewModel::lineaDesdeGuion() {
    // Lo editado en el guion de cues pasa a la línea; lo que no se tocó queda exacto.
    linea_ = evento::linea::reconciliar(linea_, e_.bloques);
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    if (corrida_) corrida_->setLinea(lineaDeCorrida());
    vigilarTemas();
}

void EventoViewModel::guionDesdeLinea() {
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    if (corrida_) corrida_->setLinea(lineaDeCorrida());
    vigilarTemas();
    if (!sucio_) {
        sucio_ = true;
        emit sucioChanged();
    }
    emit guionChanged();
    if (corrida_) despachar(corrida_->alDia(ahoraMs()));
}

void EventoViewModel::escribirVivo() {
    // Lo que sale de verdad: el ensayo no se retoma.
    if (!corriendo() || ensayo_) return;
    json j = corrida_->aJsonVivo(ahoraMs());
    j["evento"] = abierto_.toStdString();
    // La sección de la línea del documento único de aire (F3-04; antes, Runtime/linea-aire.json
    // aparte): el paso de la corrida y los HOLD van en la MISMA versión que la escena, la
    // placa y las salidas de ese momento. Lo escribe el dueño del documento, no este hilo.
    rt_.documentoAire().ponerLinea(std::move(j));
}

void EventoViewModel::borrarVivo() { rt_.documentoAire().ponerLinea(json()); }

void EventoViewModel::retomarLinea() {
    // Lo leyó el runtime al arrancar, con el resto del documento (una sola lectura, de un
    // solo momento): lo que había al aire y en qué iba la línea.
    const json& j = rt_.aireAnterior().linea;
    if (rt_.lineaDesconocida()) {
        // El documento (o la línea de la versión anterior) volvió dañado: no se sabe en qué
        // iba la línea. Nunca seguir callados: la del evento abierto vuelve en HOLD en
        // todas las salidas, con la pregunta de siempre, sin nada repuesto (lo que estaba
        // al aire lo dice quien opera). La alarma ya la levantó el runtime.
        if (abierto_.isEmpty()) return;
        const int64_t ahora = ahoraMs();
        retomarDesde(json{{"estado", "corriendo"}, {"evento", abierto_.toStdString()}, {"escritoMs", ahora}, {"arranqueMs", ahora}});
        return;
    }
    if (!j.is_object()) return;
    const std::string est = j.value("estado", std::string());
    if (est != "corriendo" && est != "pausada") {
        borrarVivo();
        return;
    }
    const QString ev = QString::fromStdString(j.value("evento", std::string()));
    if (ev.isEmpty() || (ev != abierto_ && !abrir(ev))) return;
    retomarDesde(j);
}

void EventoViewModel::retomarDesde(const json& vivo) {
    ensayo_ = false;
    nuevaCorrida();
    const std::vector<Accion> acc = corrida_->retomar(vivo, ahoraMs());
    // El cursor, en el paso que estaba al aire (desde cuando estaba).
    if (const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision())) {
        const auto [b, c] = proyeccion_.cueDe(a->paso);
        moverCursor(b, c);
        cursor_.desdeMs = a->desdeMs;
    }
    anotar(tr("Volvió la luz: la línea quedó en HOLD. Contá qué está pasando para seguir."), "error");
    emit notify(tr("Volvió la luz en medio del evento: la línea quedó en HOLD. ¿Qué está pasando?"), "error");
    despachar(acc);
}

QString EventoViewModel::pedidoDeRetoma(const QString& descripcion) const {
    const QString nombre = e_.nombre.empty() ? abierto_ : QString::fromStdString(e_.nombre);
    const QString resumen = corrida_ ? QString::fromStdString(corrida_->resumenRetoma().dump()) : QString("{}");
    return tr("Volvió la luz en medio del evento «%1» y la línea de tiempo quedó en HOLD en todas las salidas (el aire "
              "volvió como estaba). La persona que opera cuenta qué está pasando: «%2». Mirá linea_estado (y linea_recorrido "
              "si hace falta) y PROPONÉ cómo adecuar la línea: dónde reenganchar, qué saltear, qué reponer y qué recortar, "
              "probándolo con linea_editar o linea_ajustar con probar:true. Mostrale la propuesta y esperá que la confirme; "
              "recién ahí aplicala y soltá el HOLD con linea_soltar (en `decision`, lo que se decidió). Si dice que siga como "
              "estaba, linea_soltar sin cambiar nada. El resumen de la retoma: %3")
        .arg(nombre, descripcion, resumen);
}

QString EventoViewModel::describirRetoma(const QString& texto) {
    if (!corrida_ || !corrida_->retomando() || texto.trimmed().isEmpty()) return {};
    corrida_->describir(texto.trimmed().toStdString(), ahoraMs());
    anotar(tr("Lo que está pasando: %1").arg(texto.trimmed()), "info");
    despachar({});
    return pedidoDeRetoma(texto.trimmed());
}

void EventoViewModel::seguirComoEstaba() {
    if (!corrida_ || !corrida_->retomando()) return;
    anotar(tr("Sigue como estaba: se suelta el HOLD"), "ok");
    despachar(corrida_->soltar(tr("seguir como estaba").toStdString(), ahoraMs()));
}

std::filesystem::path EventoViewModel::archivoRecorrido() const {
    if (abierto_.isEmpty()) return {};
    return store_.carpetaDe(abierto_.toStdString()) / "recorrido.jsonl";
}

void EventoViewModel::volcarRecorrido() {
    if (!corrida_) return;
    const std::vector<json> v = corrida_->sacarRecorrido();
    // Lo emitido de verdad: lo del ensayo no se anota.
    if (v.empty() || ensayo_ || abierto_.isEmpty()) return;
    std::ofstream f(archivoRecorrido(), std::ios::app);
    for (const json& x : v)
        f << x.dump(-1, ' ', false, json::error_handler_t::replace) << '\n';
}

// --- La línea, para el MCP ---------------------------------------------------------

namespace {

json reparosJson(const std::vector<evento::linea::Reparo>& rs) {
    json out = json::array();
    for (const auto& r : rs)
        out.push_back({{"bloque", r.bloque}, {"paso", r.paso}, {"pieza", r.pieza}, {"grave", r.grave}, {"texto", r.texto}});
    return out;
}

/// El número de la última revisión en Respaldos/ (linea-<n>.json); 0 si no hay.
int ultimaRevision(const std::filesystem::path& respaldos) {
    int n = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(respaldos, ec)) {
        const std::string f = e.path().filename().string();
        if (f.rfind("linea-", 0) != 0 || e.path().extension() != ".json") continue;
        try {
            n = std::max(n, std::stoi(f.substr(6)));
        } catch (...) {
        }
    }
    return n;
}

} // namespace

json EventoViewModel::lineaEstado() const {
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    json j;
    if (corrida_) {
        j = corrida_->estadoJson(t, hora);
    } else {
        // Sin correr: lo que dice el plan desde ahora (o desde la hora prevista).
        const evento::linea::Corrida plan(linea_, entradaBase(false));
        j = plan.estadoJson(t, hora);
    }
    j["evento"] = abierto_.toStdString();
    j["ensayo"] = ensayo_;
    j["salidaPorOmision"] = salidaPorOmision();
    // Las PTZ que esperan que su cámara salga del aire: «mover igual» o «dejar para
    // cuando se libere» (lineaPtz).
    json ptz = json::array();
    for (const auto& [clave, x] : ptzPendientes_)
        ptz.push_back({{"pieza", x.pieza}, {"paso", x.paso}, {"salida", x.salida}, {"camara", x.camara}, {"posicion", x.posicion},
                       {"desde", horaDe(x.desdeMs)}});
    j["ptzPendientes"] = ptz;
    return j;
}

json EventoViewModel::lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const {
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    const std::string n = nivel.empty() ? "pasos" : nivel;
    if (corrida_) return evento::linea::vista(corrida_->linea(), corrida_->resolver(t), s, desdeMs, hastaMs, n, t, hora);
    evento::linea::Entrada e = entradaBase(false);
    e.ahoraMs = t;
    return evento::linea::vista(linea_, evento::linea::resolver(linea_, e), s, desdeMs, hastaMs, n, t, hora);
}

json EventoViewModel::lineaEditar(const json& ops, bool probar) {
    if (abierto_.isEmpty()) return {{"ok", false}, {"error", tr("No hay un evento abierto (guion_abrir).").toStdString()}};
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    // En vivo, lo emitido (y lo que está al aire) no se toca.
    std::function<bool(const std::string&)> intocable;
    if (corriendo())
        intocable = [this](const std::string& paso) {
            const auto& is = corrida_->instancias();
            return std::any_of(is.begin(), is.end(), [&](const evento::linea::Instancia& x) { return x.paso == paso; });
        };
    evento::linea::Edicion ed = evento::linea::editar(linea_, ops, intocable);
    if (!ed.ok) return {{"ok", false}, {"error", ed.error}};
    // El pronóstico como quedaría: con lo emitido y lo insertado, si corre.
    const std::string s = salidaPorOmision();
    json pron;
    if (corrida_) {
        evento::linea::Corrida prueba = *corrida_;
        prueba.setLinea(ed.linea);
        pron = evento::linea::vista(prueba.linea(), prueba.resolver(t), s, 0, 0, "pasos", t, hora);
    } else {
        evento::linea::Entrada e = entradaBase(false);
        e.ahoraMs = t;
        pron = evento::linea::vista(ed.linea, evento::linea::resolver(ed.linea, e), s, 0, 0, "pasos", t, hora);
    }
    evento::linea::OpcionesRevision o;
    o.contexto = entradaBase(false);
    json out{{"ok", true},
             {"probar", probar},
             {"diff", ed.diff},
             {"pronostico", pron},
             {"reparos", reparosJson(evento::linea::revisar(ed.linea, o))}};
    if (probar) return out;
    // La revisión: la línea de antes, para poder deshacer.
    const auto respaldos = store_.carpetaDe(abierto_.toStdString()) / "Respaldos";
    std::error_code ec;
    std::filesystem::create_directories(respaldos, ec);
    const int n = ultimaRevision(respaldos) + 1;
    {
        std::ofstream f(respaldos / ("linea-" + std::to_string(n) + ".json"));
        f << linea_.aJson().dump(2, ' ', false, json::error_handler_t::replace);
        if (!f) return {{"ok", false}, {"error", tr("No se pudo escribir la revisión en Respaldos/").toStdString()}};
    }
    linea_ = std::move(ed.linea);
    guionDesdeLinea();
    guardar();
    anotar(tr("La línea cambió (la de antes quedó como revisión %1)").arg(n), "info");
    out["revision"] = n;
    return out;
}

json EventoViewModel::lineaDeshacer() {
    if (abierto_.isEmpty()) return {{"ok", false}, {"error", tr("No hay un evento abierto (guion_abrir).").toStdString()}};
    const auto respaldos = store_.carpetaDe(abierto_.toStdString()) / "Respaldos";
    const int n = ultimaRevision(respaldos);
    if (n == 0) return {{"ok", false}, {"error", tr("No hay revisiones de la línea para deshacer.").toStdString()}};
    const auto archivo = respaldos / ("linea-" + std::to_string(n) + ".json");
    std::ifstream f(archivo);
    const json j = f ? json::parse(f, nullptr, false) : json();
    if (!j.is_object()) return {{"ok", false}, {"error", tr("La revisión %1 no se puede leer.").arg(n).toStdString()}};
    f.close();
    linea_ = evento::linea::Linea::deJson(j);
    std::error_code ec;
    std::filesystem::remove(archivo, ec);
    guionDesdeLinea();
    guardar();
    anotar(tr("La línea volvió a la revisión %1").arg(n), "info");
    return {{"ok", true}, {"revision", n}};
}

json EventoViewModel::lineaRevisar() const {
    evento::linea::OpcionesRevision o;
    o.contexto = entradaBase(false);
    const std::vector<std::string> escenas = rt_.engine().sceneNames();
    o.existeEscena = [&escenas](const std::string& e) { return std::find(escenas.begin(), escenas.end(), e) != escenas.end(); };
    const auto rs = evento::linea::revisar(linea_, o);
    const int graves = static_cast<int>(std::count_if(rs.begin(), rs.end(), [](const auto& r) { return r.grave; }));
    return {{"reparos", reparosJson(rs)}, {"graves", graves}, {"sePuedeCorrer", graves == 0}};
}

json EventoViewModel::lineaAjustar(const std::string& salida, int64_t objetivoMs) const {
    const int64_t t = ahoraMs();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    json a;
    if (corrida_) {
        a = evento::linea::proponerAjuste(corrida_->linea(), corrida_->resolver(t), s, objetivoMs);
    } else {
        evento::linea::Entrada e = entradaBase(false);
        e.ahoraMs = t;
        a = evento::linea::proponerAjuste(linea_, evento::linea::resolver(linea_, e), s, objetivoMs);
    }
    a["objetivo"] = horaDe(objetivoMs);
    return a;
}

namespace {
/// "" si la salida existe en la línea; si no, por qué no.
std::string errorDeSalida(const evento::linea::Linea& l, const std::string& s) {
    const auto ss = l.salidasEfectivas();
    if (std::find(ss.begin(), ss.end(), s) != ss.end()) return {};
    std::string hay;
    for (const auto& x : ss)
        hay += (hay.empty() ? "" : ", ") + x;
    return ba::i18n::llenar(ba::i18n::traducir("evento", "No hay una salida «%1» en la línea (hay: %2)."), {s, hay});
}
} // namespace

std::string EventoViewModel::lineaGo(const std::string& salida) {
    if (abierto_.isEmpty()) return tr("No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    if (!corriendo()) {
        arrancar();
        return corriendo() ? std::string() : tr("No se pudo arrancar: la línea no tiene pasos.").toStdString();
    }
    despachar(corrida_->go(s, ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaHold(const std::string& salida, std::optional<bool> on) {
    if (!corriendo()) return tr("La línea no está corriendo: no hay paso que frenar.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    const bool v = on ? *on : !corrida_->hold(s);
    if (!v && corrida_->retomando()) {
        seguirComoEstaba();
        return {};
    }
    if (!corrida_->setHold(s, v, ahoraMs())) return tr("No hay ningún paso al aire en esa salida.").toStdString();
    anotar(v ? tr("HOLD: este paso no se va solo") : tr("HOLD suelto: el paso vuelve a irse solo"), "info");
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaMoverProximo(const std::string& salida, int delta) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    if (!corrida_->moverProximo(s, delta, ahoraMs())) return tr("No hay a dónde mover el próximo.").toStdString();
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaSaltar(const std::string& salida) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    if (!corrida_->saltar(s, ahoraMs())) return tr("No hay próximo para saltear.").toStdString();
    anotar(tr("Saltea el próximo paso"), "info");
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaIr(const std::string& salida, const std::string& paso) {
    if (abierto_.isEmpty()) return tr("No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    if (!corriendo()) {
        if (!ensayo_ && store_.enCurso() != abierto_.toStdString()) iniciarEvento(abierto_);
        nuevaCorrida();
    }
    std::string err;
    const std::vector<Accion> acc = corrida_->irA(s, paso, ahoraMs(), &err);
    if (err.empty()) anotar(tr("Va al paso «%1»").arg(QString::fromStdString(paso)), "info");
    despachar(acc);
    return err;
}

std::string EventoViewModel::lineaRepetir(const std::string& salida) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    if (corrida_->alAire(s) == nullptr) return tr("No hay ningún paso al aire en esa salida.").toStdString();
    anotar(tr("Repite el paso al aire"), "info");
    despachar(corrida_->repetir(s, ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaAdlib(const std::string& salida, const std::string& pieza) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->adlib(s, pieza, ahoraMs(), &err);
    despachar(acc);
    return err;
}

std::string EventoViewModel::lineaInsertar(const std::string& salida, const std::string& item, bool alAire) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->insertar(s, item, alAire, ahoraMs(), &err);
    if (err.empty()) anotar(tr("Inserta «%1» del estante").arg(QString::fromStdString(item)), "info");
    despachar(acc);
    return err;
}

json EventoViewModel::lineaRecorrido(int ultimos) const {
    json v = json::array();
    std::ifstream f(archivoRecorrido());
    for (std::string l; std::getline(f, l);) {
        json x = json::parse(l, nullptr, false);
        if (!x.is_object()) continue;
        if (x.contains("ms") && x["ms"].is_number_integer()) x["hora"] = horaDe(x["ms"].get<int64_t>());
        v.push_back(std::move(x));
    }
    if (ultimos > 0 && static_cast<int>(v.size()) > ultimos) v.erase(v.begin(), v.end() - ultimos);
    return {{"renglones", v}, {"ensayo", ensayo_}};
}

std::string EventoViewModel::lineaRetomar(const std::string& descripcion, bool simular) {
    if (simular) {
        if (!corriendo() || ensayo_)
            return tr("Para simular un corte la línea tiene que estar corriendo, sin ensayo.").toStdString();
        // Como si se hubiera cortado la luz recién: el estado vivo de ahora, retomado.
        retomarDesde(corrida_->aJsonVivo(ahoraMs()));
    }
    if (!corrida_ || !corrida_->retomando())
        return tr("La línea no está volviendo de un corte: no hay nada que describir.").toStdString();
    if (!descripcion.empty()) describirRetoma(QString::fromStdString(descripcion));
    return {};
}

std::string EventoViewModel::lineaSoltar(const std::string& decision) {
    if (!corrida_ || !corrida_->retomando()) return tr("La línea no está en HOLD por un corte.").toStdString();
    const std::string d = decision.empty() ? tr("seguir como estaba").toStdString() : decision;
    anotar(tr("Se suelta el HOLD del corte: %1").arg(QString::fromStdString(d)), "ok");
    despachar(corrida_->soltar(d, ahoraMs()));
    return {};
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

bool EventoViewModel::prepararEscenaDeVideo(const std::string& salida) {
    // Una por salida (la por omisión, la de siempre): cada salida pasa su video.
    const std::string escenaV = escenaVideoDe(salida), fuenteV = videoDe(salida);
    const auto escenas = rt_.engine().sceneNames();
    const bool existe = std::find(escenas.begin(), escenas.end(), escenaV) != escenas.end();
    if (!existe) {
        if (!rt_.addScene(escenaV)) {
            anotar(tr("No se pudo crear la escena del video: %1").arg(QString::fromStdString(rt_.lastError())),
                   "error");
            return false;
        }
        // Protegida: si alguien la borra, todos los cues de video del guion
        // dejan de funcionar y no es obvio por qué.
        rt_.setSceneProtected(escenaV, true);
        anotar(tr("Creé la escena «%1» para los videos").arg(QString::fromStdString(escenaV)), "info");
    }
    // La fuente de video: se crea una vez y después solo se le cambia el
    // archivo. Ocupa el cuadro entero, que es lo que uno espera de un video.
    const auto fuentes = rt_.engine().sceneSources(escenaV);
    if (std::find(fuentes.begin(), fuentes.end(), fuenteV) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = fuenteV;
        spec.rect = CanvasRect{0.0, 0.0, 1.0, 1.0};
        spec.settingsJson = R"({"clear_on_media_end":true})";
        if (!rt_.addSource(escenaV, spec)) {
            anotar(tr("No se pudo crear la fuente de video: %1").arg(QString::fromStdString(rt_.lastError())),
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

void EventoViewModel::sacarAlAvanzar(const std::string& salida) {
    if (graficos_ == nullptr) return;
    for (auto it = alAvanzar_.begin(); it != alAvanzar_.end();) {
        if (!salida.empty() && it->first != salida) {
            ++it;
            continue;
        }
        for (const auto& [fuente, gen] : it->second) graficos_->sacar(fuente, gen);
        it = alAvanzar_.erase(it);
    }
}

std::string EventoViewModel::tomarEscena(const std::string& escena, const nlohmann::json& datos, const std::string& salida) {
    if (escena.empty()) return tr("el cue no dice qué escena poner").toStdString();
    escenas::Conmutador* c = conmutador(salida);
    if (c == nullptr) return tr("la salida «%1» no tiene conmutador de escenas").arg(QString::fromStdString(salida)).toStdString();
    // Ya al aire (la puso el operador a mano, o la repite el cue): no hay nada que tomar.
    if (c->alAire() == escena) return {};
    // Lo cambia la línea: no es «a mano».
    ++cambiandoEscena_;
    std::string e;
    if (c->armada() != escena) e = c->armar(escena);
    // La transición del cue si dice; si no, la de entrada de la escena (o la global).
    if (e.empty()) e = c->take(datos.value("transicion", std::string()), datos.value("ms", -1));
    --cambiandoEscena_;
    return e;
}

void EventoViewModel::armarEscena(const std::string& escena, const std::string& salida) {
    escenas::Conmutador* c = conmutador(salida);
    if (ensayo_ || c == nullptr) return;
    // La que ya está al aire no se arma (el TAKE no tendría sentido) y la ya armada, tampoco.
    if (escena.empty() || escena == c->alAire() || escena == c->armada()) return;
    if (const std::string e = c->armar(escena); !e.empty()) {
        anotar(enSalida(salida, tr("No se pudo armar «%1» en el PVW: %2").arg(QString::fromStdString(escena), QString::fromStdString(e))),
               "error");
        return;
    }
    anotar(enSalida(salida, tr("Armada en el PVW: %1").arg(QString::fromStdString(escena))), "info");
}

evento::Cue EventoViewModel::cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const {
    evento::Cue c;
    c.id = z.id;
    if (!evento::tipoDeNombre(z.tipo, c.tipo)) c.tipo = evento::Tipo::Nota;
    c.texto = z.texto;
    c.datos = z.datos;
    // La duración es la del guion de cues (la de un orador es su tiempo para hablar);
    // si la pieza no está ahí (del estante, insertada), la de su paso.
    const auto [b, k] = proyeccion_.cueDe(paso, z.id);
    if (const evento::Cue* v = cuePtrConst(b, k)) c.duracionSeg = v->duracionSeg;
    else if (const evento::linea::Paso* p = corrida_ ? corrida_->linea().paso(paso) : nullptr)
        c.duracionSeg = static_cast<int>(p->duracionPlanMs() / 1000);
    return c;
}

void EventoViewModel::hacerAccion(const Accion& a) {
    if (!corrida_) return;
    // Cada salida sale de verdad (L6): su conmutador, su mezcla, sus capas. El cursor
    // del guion de cues sigue a la por omisión (es su lectura).
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const bool principal = salida == salidaPorOmision();
    switch (a.que) {
        case Accion::Que::EntraPaso: {
            if (!ensayo_) sacarAlAvanzar(salida);
            if (principal) {
                const auto [b, c] = proyeccion_.cueDe(a.paso);
                moverCursor(b, c);
            }
            const evento::linea::Paso* p = corrida_->linea().paso(a.paso);
            const QString nombre = QString::fromStdString(p && !p->nombre.empty() ? p->nombre : a.paso);
            anotar(enSalida(salida, ensayo_ ? tr("(ensayo) Entra «%1»").arg(nombre) : tr("Entra «%1»").arg(nombre)), "info",
                   !ensayo_);
            return;
        }
        case Accion::Que::Pieza: {
            const evento::linea::Pieza* z = corrida_->pieza(a.paso, a.pieza);
            if (z == nullptr) return;
            // El cursor del guion de cues sigue a lo que se dispara en el paso al aire.
            if (principal && !a.reposicion && !a.paso.empty()) {
                const auto [b, c] = proyeccion_.cueDe(a.paso, a.pieza);
                const auto* r = proyeccion_.ref(cursor_.bloque, cursor_.cue);
                if (b >= 0 && r != nullptr && r->paso == a.paso && (b > cursor_.bloque || (b == cursor_.bloque && c > cursor_.cue)))
                    moverCursor(b, c);
            }
            const QString titulo = QString::fromStdString(z->texto.empty() ? z->tipo : z->texto);
            if (ensayo_) {
                // El ensayo recorre la línea igual pero no toca nada: es para ver
                // tiempos y orden antes del evento.
                anotar(enSalida(salida, tr("(ensayo) %1").arg(titulo)), "info", false);
                return;
            }
            if (a.reposicion) anotar(enSalida(salida, tr("Repone: %1").arg(titulo)), "info");
            // Una acción del asistente programada en la línea: se aprobó al escribirla y
            // no frena el vivo (se hace por el mismo servidor de herramientas).
            if (z->tipo == "asistente") {
                const std::string herramienta = z->datos.value("herramienta", std::string());
                if (!herramienta_ || herramienta.empty()) {
                    anotar(tr("No se pudo hacer «%1»: el servidor de herramientas no está").arg(titulo), "error");
                    return;
                }
                QPointer<EventoViewModel> yo(this);
                herramienta_(herramienta, z->datos.value("args", json::object()), [yo, titulo](bool ok, const std::string& texto) {
                    if (!yo) return;
                    yo->anotar(ok ? tr("Hecho: %1").arg(titulo)
                                  : tr("No se pudo hacer «%1»: %2").arg(titulo, QString::fromStdString(texto)),
                               ok ? "ok" : "error");
                });
                return;
            }
            if (ejecutarPiezaLinea(*z, a, titulo)) return;
            evento::Tipo t{};
            if (!evento::tipoDeNombre(z->tipo, t)) {
                // Honestidad: el tipo existe en la línea pero todavía no se ejecuta.
                anotar(tr("Todavía no sé ejecutar una pieza de tipo «%1»: %2").arg(QString::fromStdString(z->tipo), titulo), "error");
                return;
            }
            const evento::linea::Pista* pi = corrida_->linea().pista(z->pista);
            ejecutarCue(cueDePieza(*z, a.paso), a.paso, salida + "/" + a.paso + "/" + a.pieza, salida, pi == nullptr || pi->salida.empty());
            return;
        }
        case Accion::Que::Saca:
            if (!ensayo_) sacarPieza(a);
            return;
        case Accion::Que::Armar:
            armarEscena(a.escena, salida);
            return;
        case Accion::Que::Avisar:
            anotar(enSalida(salida, QString::fromStdString(a.texto)), "info");
            emit notify(QString::fromStdString(a.texto), "info");
            return;
        case Accion::Que::Termina:
            anotar(enSalida(salida, ensayo_ ? tr("Terminó el ensayo") : tr("Terminó el evento")), "ok");
            return;
    }
}

void EventoViewModel::sacarPieza(const Accion& a) {
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    if (const auto it = graficosDePieza_.find(clave); it != graficosDePieza_.end()) {
        // Con su generación: si después se puso otra cosa en esa fuente, no se toca.
        if (graficos_ != nullptr) graficos_->sacar(it->second.first, it->second.second);
        graficosDePieza_.erase(it);
    }
    if (const auto it = musicaDePieza_.find(salida); it != musicaDePieza_.end() && it->second == clave) {
        rt_.engine().stopMedia(musicaDe(salida));
        musicaDePieza_.erase(it);
        anotar(enSalida(salida, tr("Música: para")), "ok");
    }
    // La capa que subió la pieza baja con ella (una fija, como la de LSA, no: pide confirmación).
    if (const auto it = dskDePieza_.find(clave); it != dskDePieza_.end()) {
        if (control::Dsk* d = sistema().dsk) {
            const std::string e = d->mostrar(it->second.first, false, false, it->second.second);
            if (!e.empty()) anotar(enSalida(salida, QString::fromStdString(e)), "info");
        }
        dskDePieza_.erase(it);
    }
    // Una PTZ que esperaba y ya no va: no se mueve más.
    ptzPendientes_.erase(clave);
    mediosVigilados_.erase(a.pieza);
    if (playlistDePieza_ == a.pieza) playlistDePieza_.clear();
}

void EventoViewModel::ejecutarCue(const evento::Cue& cue, const std::string& paso, const std::string& clave, const std::string& salida,
                                  bool global) {
    const evento::Cue* c = &cue;
    const QString titulo = c->texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c->tipo))
                                            : QString::fromStdString(c->texto);

    // Todo por el runtime (rt_.setScene, setMuted…), no directo al motor: el runtime avisa
    // (la tarjeta Dirección, el mezclador, el tally) y lo deja guardado en la configuración.
    switch (c->tipo) {
        case evento::Tipo::Escena: {
            const std::string escena = c->datos.value("escena", std::string());
            // Sin transición propia, la del paso (si dice).
            json datos = c->datos;
            const evento::linea::Paso* p = corrida_ ? corrida_->linea().paso(paso) : nullptr;
            if (!datos.contains("transicion") && p != nullptr && p->transicion.is_object()) {
                datos["transicion"] = p->transicion.value("tipo", std::string());
                datos["ms"] = p->transicion.value("ms", -1);
            }
            if (const std::string err = tomarEscena(escena, datos, salida); !err.empty()) {
                anotar(enSalida(salida, tr("No se pudo poner la escena «%1»: %2").arg(QString::fromStdString(escena), QString::fromStdString(err))),
                       "error");
                emit notify(tr("El cue no se pudo ejecutar: %1").arg(QString::fromStdString(err)), "error");
                return;
            }
            anotar(enSalida(salida, tr("Al aire: %1").arg(QString::fromStdString(escena))), "ok");
            return;
        }
        case evento::Tipo::Orador: {
            const QString quien = QString::fromStdString(c->datos.value("participante", std::string()));
            if (sala_ == nullptr || quien.isEmpty()) {
                anotar(tr("No se pudo dar la palabra: no dice a quién"), "error");
                return;
            }
            // Las mismas operaciones que la página Sala y el MCP (sala::OradoresSala).
            if (c->duracionSeg > 0) sala_->oradores().tiempo(quien.toStdString(), c->duracionSeg);
            sala_->oradores().alAire(quien.toStdString(), true);
            anotar(tr("Habla %1").arg(quien), "ok");
            return;
        }
        case evento::Tipo::Musica: {
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            // El nombre es fijo (kMusica): asi un tema nuevo reemplaza al anterior
            // en vez de encimarse, que es lo peor que puede pasar en vivo.
            const std::string medio = musicaDe(salida);
            if (accion == "parar") {
                rt_.engine().stopMedia(medio);
                musicaDePieza_.erase(salida);
                anotar(enSalida(salida, tr("Música: para")), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar(tr("No encontré la música \"%1\" en los medios del evento").arg(rel), "error");
                emit notify(tr("El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            if (!rt_.engine().playMedia(medio, ruta.toStdString(), repetir)) {
                anotar(tr("No se pudo reproducir %1: %2").arg(rel, QString::fromStdString(rt_.engine().lastError())),
                       "error");
                return;
            }
            // Con varias salidas, la música de cada una suena solo en su mezcla.
            if (corrida_ && corrida_->linea().salidasEfectivas().size() > 1)
                if (escenas::Conmutadores* cs = sistema().conmutadores)
                    for (escenas::Conmutador* c : cs->todos())
                        rt_.setMuted(medio, c->salida() != salida, c->salida());
            musicaDePieza_[salida] = clave; // la saca su pieza cuando se termina su vida (si la tiene)
            const std::string pieza = clave.substr(clave.rfind('/') + 1);
            if (!repetir) mediosVigilados_[pieza] = medio; // para «medio:<pieza>.termino»
            vigilarMedios();
            anotar(enSalida(salida, repetir ? tr("Música: %1 (en bucle)").arg(rel) : tr("Música: %1").arg(rel)), "ok");
            return;
        }
        case evento::Tipo::Teleprompter: {
            // El texto vive en Discursos/ de la carpeta del evento, no pegado
            // en la tablet: asi viaja con el evento y el asistente puede
            // ayudar a escribirlo o acortarlo.
            const QString rel = QString::fromStdString(c->datos.value("discurso", std::string()));
            if (sala_ == nullptr || rel.isEmpty()) {
                anotar(tr("No se pudo cargar el discurso: no dice cuál"), "error");
                return;
            }
            // La misma carga que la página Teleprompter (sala::Teleprompter): el
            // discurso de Discursos/ con su ruta de título. `arrancar` en los
            // datos del cue: por defecto el texto queda LISTO pero parado, que
            // es lo que se quiere casi siempre (el orador todavia esta
            // caminando hacia el atril).
            // En cuál (cada uno con su texto y su página); sin decir, el principal.
            const std::string cual = c->datos.value("prompter", std::string());
            if (!cual.empty() && !listaPrompters().existe(cual)) {
                anotar(tr("No hay un teleprompter «%1» en el evento").arg(QString::fromStdString(cual)), "error");
                return;
            }
            const Discursos docs(carpetaDeDiscursos());
            const std::string err = sala_->teleprompter().para(cual).cargarDiscurso(docs, rel.toStdString(),
                                                                                   c->datos.value("arrancar", false));
            if (!err.empty()) {
                anotar(tr("No se pudo abrir el discurso: %1").arg(rel), "error");
                emit notify(tr("No encontré el discurso \"%1\" en la carpeta del evento").arg(rel), "error");
                return;
            }
            anotar(tr("Teleprompter: %1").arg(rel), "ok");
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
                    anotar(tr("No hay motor de gráficos para los vínculos"), "error");
                    return;
                }
                const QString id = QString::fromStdString(c->datos.value("vinculo", std::string()));
                if (!vinculos_->activar(id, accion == "prender")) {
                    anotar(tr("No existe el vínculo \"%1\"").arg(id), "error");
                    emit notify(tr("El cue no se pudo ejecutar: no existe el vínculo \"%1\"").arg(id), "error");
                    return;
                }
                anotar(
                    (accion == "prender" ? tr("Prende el rótulo automático %1") : tr("Apaga el rótulo automático %1")).arg(id),
                    "ok");
                return;
            }
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (graficos_ == nullptr) {
                anotar(tr("No hay renderizador de gráficos en este bundle"), "error");
                return;
            }
            const std::uint64_t id = graficos_->fuentePorNombre(fuente);
            if (id == 0) {
                anotar(tr("No hay ninguna fuente de gráfico llamada \"%1\" en las escenas").arg(fuente), "error");
                emit notify(tr("El cue no se pudo ejecutar: falta la fuente de gráfico \"%1\"").arg(fuente),
                            "error");
                return;
            }
            if (accion == "ocultar") {
                const QString err = graficos_->ocultar(id);
                if (!err.isEmpty()) {
                    anotar(tr("No se pudo sacar el gráfico: %1").arg(err), "error");
                    return;
                }
                anotar(tr("Saca el gráfico de %1").arg(fuente), "ok");
                return;
            }
            const QString grafico = QString::fromStdString(c->datos.value("grafico", std::string()));
            QJsonObject valores;
            if (c->datos.contains("valores"))
                valores =
                    QJsonDocument::fromJson(QByteArray::fromStdString(c->datos["valores"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            const int dur = c->datos.value("duracionMs", 0);
            const bool repetir = c->datos.value("repetir", false);
            // CUÁNDO SE VA: lo que diga la cue (seVa); si no dice, «sola» si trae un
            // «cuándo se va» (salidaEn), y si no, lo que diga la ficha del gráfico (los
            // títulos y placas: al avanzar), o queda.
            std::string seVa = c->datos.value("seVa", std::string());
            if (seVa.empty())
                seVa = valores.value("salidaEn").toInt(0) > 0
                           ? "sola"
                           : graficos_->catalogo().seVaDe(grafico).toStdString();
            if (seVa != "sola") valores.remove("salidaEn"); // al avanzar o queda: no se va solo
            const QString err = graficos_->mostrar(grafico, valores, id, dur, repetir);
            if (!err.isEmpty()) {
                anotar(tr("No se pudo sacar al aire el gráfico \"%1\": %2").arg(grafico, err), "error");
                emit notify(tr("El cue de gráfico falló: %1").arg(err), "error");
                return;
            }
            // La fuente es una sola: si otra salida la tenía para «al avanzar», ahora es de esta.
            for (auto& [s, lista] : alAvanzar_)
                lista.erase(std::remove_if(lista.begin(), lista.end(), [id](const auto& a) { return a.first == id; }), lista.end());
            if (seVa == "al-avanzar") alAvanzar_[salida].emplace_back(id, graficos_->generacion(id));
            graficosDePieza_[clave] = {id, graficos_->generacion(id)};
            anotar((seVa == "al-avanzar" ? tr("Al aire el gráfico %1 en %2 (se va al avanzar)")
                    : seVa == "sola"       ? tr("Al aire el gráfico %1 en %2 (se va solo)")
                                           : tr("Al aire el gráfico %1 en %2"))
                       .arg(grafico, fuente),
                   "ok");
            return;
        }
        case evento::Tipo::Carrusel: {
            // Fija carrusel y lámina (no «la siguiente»): si en el medio se movió a mano desde
            // la tarjeta, el guion lo vuelve a donde dice. Las mismas operaciones que la tarjeta.
            const QString nombre = QString::fromStdString(c->datos.value("carrusel", std::string()));
            const int lamina = evento::numeroDeLamina(c->datos);
            if (carruseles_ == nullptr) {
                anotar(tr("No hay carruseles en este programa"), "error");
                return;
            }
            const QString err = carruseles_->mostrar(nombre, lamina);
            if (!err.isEmpty()) {
                anotar(tr("No se pudo poner el carrusel: %1").arg(err), "error");
                emit notify(tr("El cue de carrusel falló: %1").arg(err), "error");
                return;
            }
            anotar(tr("Carrusel «%1», lámina %2").arg(nombre).arg(lamina), "ok");
            return;
        }
        case evento::Tipo::Espera: anotar(tr("Espera tu confirmación: %1").arg(titulo), "info"); return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa: anotar(titulo, "info"); return;
        case evento::Tipo::Audio: {
            const std::string accion = c->datos.value("accion", std::string());
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (fuente.isEmpty()) {
                anotar(tr("El cue de audio no dice sobre que fuente actuar"), "error");
                return;
            }
            const std::string f = fuente.toStdString();
            // En qué mezcla: la de su salida; desde una pista global, en todas (así una
            // salida no le apaga el micrófono a otra: eso solo lo hace una pista global).
            std::vector<std::string> mezclas{salida};
            if (global)
                if (escenas::Conmutadores* cs = sistema().conmutadores) {
                    mezclas.clear();
                    for (escenas::Conmutador* c : cs->todos())
                        mezclas.push_back(c->salida());
                }
            const auto enTodas = [&](const auto& hacer) {
                bool ok = !mezclas.empty();
                for (const std::string& m : mezclas)
                    ok = hacer(m) && ok;
                return ok;
            };
            if (accion == "silenciar" || accion == "activar") {
                const bool mudo = accion == "silenciar";
                if (!enTodas([&](const std::string& m) { return rt_.setMuted(f, mudo, m); })) {
                    anotar(tr("No existe la fuente de audio \"%1\"").arg(fuente), "error");
                    emit notify(tr("El cue no se pudo ejecutar: no existe la fuente \"%1\"").arg(fuente),
                                "error");
                    return;
                }
                anotar((mudo ? tr("Silencia %1") : tr("Activa %1")).arg(fuente), "ok");
                return;
            }
            if (accion == "volumen") {
                // En dB, que es como piensa el operador. 0 = sin cambio.
                const double db = c->datos.value("db", 0.0);
                const float mul = static_cast<float>(std::pow(10.0, db / 20.0));
                if (!enTodas([&](const std::string& m) { return rt_.setVolume(f, mul, m); })) {
                    anotar(tr("No existe la fuente de audio \"%1\"").arg(fuente), "error");
                    return;
                }
                anotar(fuente + " a " + QString::number(db, 'f', 1) + " dB", "ok");
                return;
            }
            anotar(tr("No sé qué es la acción de audio \"%1\" (silenciar, activar o volumen)").arg(QString::fromStdString(accion)),
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
                rt_.updateSourceSettings(videoDe(salida), R"({"local_file":""})");
                anotar(tr("Video: para"), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar(tr("No encontré el video \"%1\" en los medios del evento").arg(rel), "error");
                emit notify(tr("El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            if (!prepararEscenaDeVideo(salida)) return;
            const bool repetir = c->datos.value("repetir", false);
            // Se le cambia el ARCHIVO a la fuente que ya esta en la escena, no
            // se crea una nueva: `playMedia` sirve para la musica, que va por
            // un canal global de audio y no se ve.
            const QJsonObject ajustes{{"local_file", ruta}, {"looping", repetir}, {"clear_on_media_end", true}};
            if (!rt_.updateSourceSettings(
                    videoDe(salida), QJsonDocument(ajustes).toJson(QJsonDocument::Compact).toStdString())) {
                anotar(tr("No se pudo cargar %1: %2").arg(rel, QString::fromStdString(rt_.lastError())), "error");
                return;
            }
            // Y se rebobina: si el archivo es el mismo que ya estaba, el motor no
            // tiene motivo para volver a empezar y el cue no mostraria nada.
            rt_.engine().restartMedia(videoDe(salida));
            if (!repetir) mediosVigilados_[clave.substr(clave.rfind('/') + 1)] = videoDe(salida); // «medio:<pieza>.termino»
            vigilarMedios();
            // Al aire recién DESPUÉS de que el archivo esté cargado: al revés
            // se ve un cuadro en negro mientras abre.
            if (c->datos.value("alAire", true)) {
                escenas::Conmutador* cm = conmutador(salida);
                escenaPrevia_ = cm ? QString::fromStdString(cm->alAire()) : QString();
                // Por el conmutador de su salida (corte directo, como antes): el tally, el
                // AFV y la escena guardada se enteran igual que con cualquier otro cambio.
                ++cambiandoEscena_;
                const std::string err = cm ? cm->poner(escenaVideoDe(salida)) : tr("no hay conmutador de escenas").toStdString();
                --cambiandoEscena_;
                if (!err.empty()) {
                    anotar(enSalida(salida, tr("No se pudo poner al aire la escena del video: %1").arg(QString::fromStdString(err))), "error");
                    return;
                }
            }
            anotar(enSalida(salida, repetir ? tr("Video: %1 (en bucle)").arg(rel) : tr("Video: %1").arg(rel)), "ok");
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
                anotar(tr("No se puede mandar a la pantalla: el servidor de herramientas no está"), "error");
                return;
            }
            QPointer<EventoViewModel> yo(this);
            herramienta_("pantalla_mostrar", args, [yo, titulo, pantalla](bool ok, const std::string& texto) {
                if (!yo) return;
                yo->anotar((ok ? tr("Pantalla «%1»: %2").arg(QString::fromStdString(pantalla), titulo)
                               : tr("No se pudo mandar a «%1»: %2")
                                     .arg(QString::fromStdString(pantalla), QString::fromStdString(texto))),
                           ok ? "ok" : "error");
            });
            return;
        }
        default:
            // Honestidad: el tipo existe en el guion pero todavia no se
            // ejecuta. Se anota y la línea sigue, en vez de hacer como que
            // paso algo.
            anotar(tr("Todavía no sé ejecutar un cue de tipo \"%1\": %2")
                       .arg(QString::fromStdString(evento::nombreDeTipo(c->tipo)), titulo),
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
    if (vino("registro")) a.registro = m.value("registro").toString().toStdString();
    if (vino("datos")) {
        const QJsonObject o = QJsonObject::fromVariantMap(m.value("datos").toMap());
        a.datos = nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (!a.datos.is_object()) queCampos.pop_back();
    }
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
                       {"porDonde", QString::fromStdString(a.porDonde)},
                       {"registro", QString::fromStdString(a.registro)},
                       {"datos", QJsonDocument::fromJson(QByteArray::fromStdString(a.datos.dump(
                                                              -1, ' ', false, nlohmann::json::error_handler_t::replace)))
                                     .object()
                                     .toVariantMap()}};
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
        emit notify(tr("No se pudo guardar el padron: %1").arg(QString::fromStdString(err)), "error");
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
        emit notify(tr("Para anotar a alguien hace falta al menos su nombre."), "error");
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
            emit notify(tr("%n persona llegó (escanearon su invitación).", "", hechos),
                        "info");
        if (!desconocidos.isEmpty())
            emit notify(tr("Escanearon códigos que no están en el padrón: %1. Si corresponde, anotalos con el padrón.")
                            .arg(desconocidos.join(", ")),
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
        emit notify(tr("%1 de la lista de auto-aceptar pasaron al padrón del evento.").arg(movidos),
                    "info");
}

std::filesystem::path EventoViewModel::carpetaAbierta() const {
    if (abierto_.isEmpty()) return {};
    return store_.carpetaDe(abierto_.toStdString());
}

QString EventoViewModel::registrarDesdeTotem(const QString& personaId, const QString& nombre, const QString& totem,
                                             const nlohmann::json& datos) {
    if (abierto_.isEmpty()) return {};
    // La ficha de rostro manda (como buscarEnPadron); sin ficha, el nombre dicho
    // solo engancha a alguien que ya estaba en la lista armada de antemano.
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr && !nombre.trimmed().isEmpty()) a = padron_.porNombre(nombre.toStdString());
    std::string id;
    if (a != nullptr) {
        id = a->id;
        evento::Asistente c;
        std::vector<std::string> que{"datos", "registro"};
        c.datos = datos.is_object() ? datos : nlohmann::json::object();
        c.registro = totem.toStdString();
        if (a->personaId.empty() && !personaId.isEmpty()) {
            c.personaId = personaId.toStdString();
            que.emplace_back("personaId");
        }
        if (a->nombre.empty() && !nombre.trimmed().isEmpty()) {
            c.nombre = nombre.trimmed().toStdString();
            que.emplace_back("nombre");
        }
        if (a->canal == evento::Canal::SinDefinir) {
            c.canal = evento::Canal::Presencial;
            que.emplace_back("canal");
        }
        padron_.cambiar(id, c, que);
    } else {
        evento::Asistente n;
        n.personaId = personaId.toStdString();
        n.nombre = nombre.trimmed().isEmpty() ? tr("(sin nombre)").toStdString() : nombre.trimmed().toStdString();
        n.canal = evento::Canal::Presencial; // se paró frente a un tótem del lugar (o escaneó su QR ahí)
        n.datos = datos.is_object() ? datos : nlohmann::json::object();
        n.registro = totem.toStdString();
        // El contacto legible sale de lo que dijo, si dijo un correo o un teléfono.
        for (const char* k : {"correo", "email", "mail", "telefono", "celular"})
            if (n.contacto.empty() && n.datos.contains(k) && n.datos[k].is_string()) n.contacto = n.datos[k].get<std::string>();
        id = padron_.agregar(std::move(n));
    }
    const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    padron_.acreditar(id, "registro", static_cast<std::int64_t>(ahora));
    guardarPadronAhora();
    return QString::fromStdString(id);
}

bool EventoViewModel::registradoPorTotem(const QString& personaId) const {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    return a != nullptr && !a->registro.empty();
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
    for (const auto& [id, texto] : itemsDeChequeo()) uno(id, ba::i18n::traducir("evento", texto));
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


// --- La línea con varias salidas y lo de afuera (L6/L7) ------------------------------------
//
// docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.4, 3.6 y 3.9. La corrida (pura) decide; acá se
// engancha a lo que hay en la app: el conmutador de cada salida, el bus (para las
// condiciones), las capas DSK, las cámaras PTZ, el orden del día, las votaciones y la
// playlist. Nada de esto publica temas linea/* (eso es de ui/LineaBus).

escenas::Conmutador* EventoViewModel::conmutador(const std::string& salida) const {
    escenas::Conmutadores* cs = sistema().conmutadores;
    if (cs == nullptr) return nullptr;
    // Los ids de la línea son los de las salidas del motor; la por omisión de la línea,
    // si el motor no tiene una con ese id, es la por omisión del motor.
    escenas::Conmutador* c = cs->de(salida);
    return c == nullptr && salida == salidaPorOmision() ? &cs->porOmision() : c;
}

QString EventoViewModel::enSalida(const std::string& salida, const QString& texto) const {
    if (linea_.salidasEfectivas().size() < 2) return texto;
    return tr("«%1» · %2").arg(QString::fromStdString(salida), texto);
}

std::string EventoViewModel::musicaDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kMusica) : std::string(kMusica) + " · " + salida;
}

std::string EventoViewModel::escenaVideoDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kEscenaVideo) : std::string(kEscenaVideo) + " · " + salida;
}

std::string EventoViewModel::videoDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kVideo) : std::string(kVideo) + " · " + salida;
}

void EventoViewModel::enchufar() {
    const Sistema s = sistema();
    const auto primeraVez = [this](const QObject* o) { return o != nullptr && enchufados_.insert(o).second; };
    // El aire de cada salida: lo hecho «a mano» y las PTZ que esperan una cámara.
    if (primeraVez(s.conmutadores)) {
        const auto seguir = [this](escenas::Conmutador* c) {
            connect(c, &escenas::Conmutador::alAireCambio, this,
                    [this, c](const QString& escena) { alAireCambio(c->salida(), escena.toStdString()); });
        };
        for (escenas::Conmutador* c : s.conmutadores->todos())
            seguir(c);
        connect(s.conmutadores, &escenas::Conmutadores::creado, this, seguir);
    }
    // El orden del día: los bloques que salen de él se re-expanden al cambiar.
    if (primeraVez(s.ordenDelDia))
        connect(s.ordenDelDia, &asamblea::OrdenDelDia::cambio, this, &EventoViewModel::expandirOrdenDelDia);
    // La playlist: cuando se para sola, terminó «su medio» (para un paso que dura lo que ella).
    if (primeraVez(s.playlist))
        connect(s.playlist, &medios::Playlist::estadoCambio, this, [this]() {
            const Sistema s = sistema();
            if (s.playlist == nullptr || playlistDePieza_.empty()) return;
            const bool suena = s.playlist->estado().value("reproduciendo", false);
            if (suena) playlistAndando_ = true;
            if (suena || !playlistAndando_) return;
            const std::string pieza = playlistDePieza_;
            playlistDePieza_.clear();
            playlistAndando_ = false;
            if (corriendo()) despachar(corrida_->medioTermino(pieza, ahoraMs()));
        });
    // Los pulsos (tecla:, botonera:, web:): los publican las superficies y los atajos
    // en «linea/pulso» (un plugin también puede). Se atienden después, en limpio, como
    // los temas de las condiciones.
    if (primeraVez(s.bus)) {
        QPointer<EventoViewModel> yo(this);
        s.bus->suscribir("linea/pulso", [yo](const std::string&, const json& valor) {
            if (!yo || !valor.is_string()) return;
            const std::string clave = valor.get<std::string>();
            QMetaObject::invokeMethod(
                yo, [yo, clave]() { if (yo && yo->corriendo()) yo->lineaPulso(clave); }, Qt::QueuedConnection);
        });
    }
    expandirOrdenDelDia();
}

void EventoViewModel::vigilarTemas() {
    control::BusEventos* bus = sistema().bus;
    if (bus == nullptr || !corrida_) return;
    for (const std::string& tema : corrida_->temasQueMira()) {
        if (!suscripciones_.count(tema)) {
            // Lo de afuera llega en medio de cualquier cosa (hasta de un TAKE de la
            // línea): se atiende después, en limpio. Los temas que dejan de usarse
            // quedan suscriptos: la corrida ignora lo que no mira.
            QPointer<EventoViewModel> yo(this);
            suscripciones_[tema] = bus->suscribir(tema, [yo](const std::string& t, const json& valor) {
                if (!yo) return;
                QMetaObject::invokeMethod(
                    yo,
                    [yo, t, valor]() {
                        if (!yo || !yo->corrida_) return;
                        const std::vector<Accion> acc = yo->corrida_->hecho(t, valor, ahoraMs());
                        if (yo->corriendo()) yo->despachar(acc);
                    },
                    Qt::QueuedConnection);
            });
        }
        // La corrida es nueva (o el tema): se pone al día con lo último que se publicó.
        const json u = bus->ultimo(tema);
        if (u.is_object() && u.contains("valor")) {
            const std::vector<Accion> acc = corrida_->hecho(tema, u["valor"], ahoraMs());
            if (corriendo() && !acc.empty()) despachar(acc);
        }
    }
}

void EventoViewModel::alAireCambio(const std::string& salida, const std::string& escena) {
    // ¿Fue a mano? Lo que cambia la línea misma (tomarEscena, el video) no cuenta.
    if (cambiandoEscena_ == 0 && corriendo() && !ensayo_) {
        const std::string antes = corrida_->aMano(salida);
        corrida_->marcarAMano(salida, escena, ahoraMs());
        const std::string ahora = corrida_->aMano(salida);
        if (ahora != antes) {
            if (!ahora.empty())
                anotar(enSalida(salida, tr("A mano: «%1» en vez de la del paso (la línea sigue; «reenganchar» vuelve)")
                                            .arg(QString::fromStdString(ahora))),
                       "info");
            volcarRecorrido();
            emit correChanged();
        }
    }
    // Una cámara pudo haber salido del aire: las PTZ que la esperaban, ahora.
    ptz::ControlCamaras* cams = sistema().camaras;
    if (cams == nullptr) return;
    std::vector<std::string> listas;
    for (const auto& [clave, x] : ptzPendientes_)
        if (cams->chequearMovimiento(QString::fromStdString(x.camara)).isEmpty()) listas.push_back(clave);
    for (const std::string& clave : listas)
        moverPtz(clave, false);
}

void EventoViewModel::expandirOrdenDelDia() {
    asamblea::OrdenDelDia* od = sistema().ordenDelDia;
    if (od == nullptr || abierto_.isEmpty()) return;
    std::set<std::string> emitidos;
    if (corrida_)
        for (const evento::linea::Instancia& x : corrida_->instancias())
            emitidos.insert(x.paso);
    if (!evento::linea::expandirOrdenDelDia(linea_, od->puntos(), emitidos)) return;
    anotar(tr("El orden del día cambió: la línea se volvió a armar (lo que ya salió no se tocó)"), "info");
    guionDesdeLinea();
}

bool EventoViewModel::ejecutarPiezaLinea(const evento::linea::Pieza& z, const Accion& a, const QString& titulo) {
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    const Sistema s = sistema();
    const auto falla = [&](const QString& porque) {
        anotar(enSalida(salida, tr("No se pudo hacer «%1»: %2").arg(titulo, porque)), "error");
        emit notify(tr("La pieza «%1» no se pudo hacer: %2").arg(titulo, porque), "error");
    };
    const auto bien = [&](const QString& que) { anotar(enSalida(salida, que), "ok"); };
    const json& d = z.datos;

    if (z.tipo == "ptz") {
        // {camara, posicion, segundos?, siOcupada: esperar|mover|saltar}
        if (s.camaras == nullptr) return falla(tr("no hay control de cámaras")), true;
        PtzPendiente x{salida, a.paso, a.pieza, d.value("camara", std::string()), d.value("posicion", std::string()),
                       d.value("segundos", -1.0), ahoraMs()};
        if (x.camara.empty() || x.posicion.empty()) return falla(tr("la PTZ no dice qué cámara ni a qué posición")), true;
        const QString motivo = s.camaras->chequearMovimiento(QString::fromStdString(x.camara));
        const std::string si = d.value("siOcupada", std::string("esperar"));
        ptzPendientes_[clave] = x;
        if (motivo.isEmpty() || si == "mover") {
            moverPtz(clave, !motivo.isEmpty());
        } else if (si == "saltar") {
            ptzPendientes_.erase(clave);
            anotar(enSalida(salida, tr("PTZ «%1»: no se mueve (%2)").arg(titulo, motivo)), "info");
        } else {
            // Ocupada: espera a que se libere, y la persona puede decir «mover igual».
            anotar(enSalida(salida, tr("PTZ «%1» espera: %2").arg(titulo, motivo)), "info");
            emit notify(tr("%1 La PTZ «%2» se hace cuando se libere (o «mover igual»).").arg(motivo, titulo), "info");
            emit correChanged();
        }
        return true;
    }

    if (z.tipo == "dsk") {
        // {capa: 1..4, accion: subir|bajar, confirmada?}: en las capas de ESTA salida.
        if (s.dsk == nullptr) return falla(tr("no hay capas DSK")), true;
        const int capa = d.value("capa", 0);
        const bool subir = d.value("accion", std::string("subir")) != "bajar";
        const std::string e = s.dsk->mostrar(capa, subir, d.value("confirmada", false), salida);
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (subir) dskDePieza_[clave] = {capa, salida}; // baja cuando se termina su vida
        else dskDePieza_.erase(clave);
        bien(subir ? tr("Sube la capa %1").arg(capa) : tr("Baja la capa %1").arg(capa));
        return true;
    }

    if (z.tipo == "asamblea") {
        // {accion: tratar (punto) | siguiente | punto (punto, estado, resultado?) |
        //  votacion (que: abrir|pausar|reanudar|cerrar|a_quorum, tema, menu, segundos,
        //  resultado: al cerrar, también el resultado oficial; de fábrica, sí)}
        const std::string accion = d.value("accion", std::string());
        const auto numero = [&](const char* k) {
            const json& v = d.contains(k) ? d[k] : json();
            return v.is_number() ? v.get<int>() : v.is_string() ? std::atoi(v.get<std::string>().c_str()) : 0;
        };
        if (accion == "tratar" || accion == "siguiente" || accion == "punto") {
            if (s.ordenDelDia == nullptr) return falla(tr("no hay orden del día")), true;
            const int n = numero("punto");
            const std::string e = accion == "siguiente" ? s.ordenDelDia->siguiente()
                                  : accion == "tratar"  ? s.ordenDelDia->punto(n, "en_tratamiento", "")
                                                        : s.ordenDelDia->punto(n, d.value("estado", std::string()), d.value("resultado", std::string()));
            if (!e.empty()) return falla(QString::fromStdString(e)), true;
            bien(accion == "siguiente" ? tr("Orden del día: el punto que sigue") : tr("Orden del día: punto %1").arg(n));
            return true;
        }
        if (accion == "votacion") {
            if (s.votaciones == nullptr) return falla(tr("no hay votaciones")), true;
            const std::string que = d.value("que", std::string());
            const bool conResultado = que == "cerrar" && d.value("resultado", true);
            QPointer<EventoViewModel> yo(this);
            QPointer<asamblea::Votaciones> vs(s.votaciones);
            s.votaciones->accion(que, d, "linea", [yo, vs, titulo, salida, conResultado](const std::string& e) {
                if (!yo) return;
                if (!e.empty()) {
                    yo->anotar(yo->enSalida(salida, tr("Votación (%1): %2").arg(titulo, QString::fromStdString(e))), "error");
                    return;
                }
                yo->anotar(yo->enSalida(salida, tr("Votación: %1").arg(titulo)), "ok");
                // Cerrada: el resultado oficial (a Quórum y al bus: sesion/votacion).
                if (conResultado && vs)
                    vs->accion("a_quorum", json::object(), "linea", [yo, salida](const std::string& e2) {
                        if (yo && !e2.empty())
                            yo->anotar(yo->enSalida(salida, tr("El resultado no se pudo cargar: %1").arg(QString::fromStdString(e2))),
                                       "error");
                    });
            });
            return true;
        }
        return falla(tr("«%1» no es una acción de la asamblea (tratar, siguiente, punto, votacion)").arg(QString::fromStdString(accion))),
               true;
    }

    if (z.tipo == "playlist") {
        // {accion: play|next|prev|stop, indice?, autoNext?, loop?}: la línea manda sobre la
        // playlist, que sigue siendo la dueña del in/out y del auto-next.
        if (s.playlist == nullptr) return falla(tr("no hay playlist")), true;
        if (d.contains("autoNext")) s.playlist->setAutoNext(d.value("autoNext", true));
        if (d.contains("loop")) s.playlist->setLoop(d.value("loop", false));
        const std::string accion = d.value("accion", std::string("play"));
        const std::string e = s.playlist->control(accion, d.value("indice", -1));
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (accion == "play") {
            playlistDePieza_ = a.pieza; // su fin (medio:<pieza>.termino) es cuando la playlist se para sola
            playlistAndando_ = false;
        } else if (accion == "stop") {
            playlistDePieza_.clear();
        }
        bien(tr("Playlist: %1").arg(QString::fromStdString(accion)));
        return true;
    }
    return false;
}

void EventoViewModel::moverPtz(const std::string& clave, bool forzar) {
    const auto it = ptzPendientes_.find(clave);
    if (it == ptzPendientes_.end()) return;
    const PtzPendiente x = it->second;
    ptzPendientes_.erase(it);
    ptz::ControlCamaras* cams = sistema().camaras;
    if (cams == nullptr) return;
    const QVariantMap r = cams->irAPosicion(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), x.segundos);
    if (!r.value("ok").toBool()) {
        anotar(enSalida(x.salida, tr("PTZ «%1» a «%2»: %3")
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), r.value("error").toString())),
               "error");
    } else {
        anotar(enSalida(x.salida, (forzar ? tr("PTZ «%1» a «%2» (movida igual, estaba al aire)") : tr("PTZ «%1» a «%2»"))
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion))),
               "ok");
    }
    emit correChanged();
}

void EventoViewModel::vigilarMedios() {
    if (mediosVigilados_.empty()) {
        vigiaMedios_.stop();
        mediosAndando_.clear();
        return;
    }
    if (vigiaMedios_.isActive()) return;
    if (vigiaMedios_.interval() != 250) {
        // Solo mientras haya piezas que terminan con su medio (la CPU ociosa importa).
        vigiaMedios_.setInterval(250);
        connect(&vigiaMedios_, &QTimer::timeout, this, [this]() {
            std::map<std::string, std::string> estado;
            for (const auto& m : rt_.engine().mediaSources())
                estado[m.name] = m.state;
            std::vector<std::string> terminaron;
            for (const auto& [pieza, medio] : mediosVigilados_) {
                const auto e = estado.find(medio);
                const std::string st = e == estado.end() ? std::string("none") : e->second;
                if (st == "playing") mediosAndando_.insert(pieza);
                else if (mediosAndando_.count(pieza) && (st == "ended" || st == "stopped" || st == "none" || st == "error"))
                    terminaron.push_back(pieza);
            }
            for (const std::string& pieza : terminaron) {
                mediosVigilados_.erase(pieza);
                mediosAndando_.erase(pieza);
                if (corriendo()) despachar(corrida_->medioTermino(pieza, ahoraMs()));
            }
            if (mediosVigilados_.empty()) vigiaMedios_.stop();
        });
    }
    vigiaMedios_.start();
}

std::string EventoViewModel::lineaPulso(const std::string& clave) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    if (const std::string e = evento::linea::errorDeCondicion(clave); !e.empty()) return e;
    despachar(corrida_->pulso(clave, ahoraMs()));
    return {};
}

std::string EventoViewModel::lineaReenganchar(const std::string& salida) {
    if (!corriendo()) return tr("La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea_, s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->reenganchar(s, ahoraMs(), &err);
    if (err.empty()) anotar(enSalida(s, tr("Reengancha: vuelve a la escena del paso")), "info");
    despachar(acc);
    return err;
}

std::string EventoViewModel::lineaPtz(const std::string& pieza, const std::string& decision) {
    for (const auto& [clave, x] : ptzPendientes_) {
        if (x.pieza != pieza) continue;
        const std::string k = clave;
        if (decision == "mover") moverPtz(k, true);
        else if (decision == "saltar") {
            anotar(enSalida(x.salida, tr("PTZ «%1»: se deja sin mover").arg(QString::fromStdString(pieza))), "info");
            ptzPendientes_.erase(k);
            emit correChanged();
        } else if (decision != "esperar") {
            return tr("La decisión es «mover», «esperar» o «saltar».").toStdString();
        }
        return {};
    }
    return tr("No hay ninguna PTZ «%1» esperando.").arg(QString::fromStdString(pieza)).toStdString();
}

// --- Lo que dijo cada orador, en el recorrido -----------------------------------------------
//
// El pronóstico usa lo previsto de la pieza `orador`; lo que habló de verdad lo sabe el
// orden del día (OrdenDelDia::tiemposPalabra, por persona y acumulado). Mientras un paso
// con orador está al aire se guarda cuánto llevaba cada uno al entrar; al salir, la
// diferencia va al recorrido («palabra»): quién habló en ese paso y cuánto.

std::map<std::string, int64_t> EventoViewModel::segundosDePalabra() const {
    std::map<std::string, int64_t> m;
    asamblea::OrdenDelDia* od = sistema().ordenDelDia;
    if (od == nullptr) return m;
    for (const json& o : od->tiemposPalabra(json::array()))
        m[o.value("nombre", std::string())] = o.value("segundos", 0LL);
    return m;
}

void EventoViewModel::seguirPalabra(const std::vector<Accion>& acc) {
    if (!corrida_ || ensayo_) return;
    for (const Accion& a : acc) {
        if (a.que != Accion::Que::EntraPaso && a.que != Accion::Que::Termina) continue;
        const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
        // Se fue el que estaba: lo que habló cada uno mientras tanto.
        if (const auto it = palabras_.find(salida); it != palabras_.end()) {
            const Palabra antes = it->second;
            palabras_.erase(it);
            json hablaron = json::array();
            int64_t total = 0;
            for (const auto& [nombre, seg] : segundosDePalabra()) {
                const auto a0 = antes.segundos.find(nombre);
                const int64_t d = seg - (a0 == antes.segundos.end() ? 0 : a0->second);
                if (d <= 0 || nombre.empty()) continue;
                hablaron.push_back({{"nombre", nombre}, {"segundos", d}});
                total += d;
            }
            if (!hablaron.empty())
                corrida_->anotarDeAfuera(ahoraMs(), "palabra", {{"salida", salida}, {"paso", antes.paso}, {"segundos", total},
                                                                 {"previstoSeg", antes.previstoSeg}, {"hablaron", hablaron}});
        }
        if (a.que != Accion::Que::EntraPaso) continue;
        const evento::linea::Paso* p = corrida_->linea().paso(a.paso);
        if (p == nullptr || std::none_of(p->piezas.begin(), p->piezas.end(), [](const auto& z) { return z.tipo == "orador"; })) continue;
        palabras_[salida] = {a.paso, static_cast<int>(p->duracionPlanMs() / 1000), segundosDePalabra()};
    }
}

// --- Grabar la línea en vivo (L10) ------------------------------------------------------------

evento::linea::Linea EventoViewModel::lineaDeCorrida() const {
    return grabar_ ? evento::linea::lineaParaGrabar(linea_) : linea_;
}

void EventoViewModel::setGrabarTiempos(bool on) {
    if (on == grabar_) return;
    if (corriendo()) {
        emit notify(tr("Grabar los tiempos se elige antes de arrancar el ensayo"), "error");
        return;
    }
    grabar_ = on;
    // Grabar es un ensayo: no sale nada al aire mientras se toca GO a mano.
    if (on) setEnsayo(true);
    grabado_ = nullptr;
    emit correChanged();
}

QVariantMap EventoViewModel::tiemposGrabadosUi() const {
    if (!grabado_.is_object()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(grabado_.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

json EventoViewModel::lineaTiemposGrabados() const {
    if (grabado_.is_object()) return grabado_;
    return {{"grabando", grabar_}, {"corriendo", corriendo()}, {"pasos", json::array()}};
}

QString EventoViewModel::aplicarTiemposGrabados() {
    if (!grabado_.is_object() || grabado_["ops"].empty()) return tr("No hay tiempos grabados para guardar.");
    const json r = lineaEditar(grabado_["ops"], false);
    if (!r.value("ok", false)) return QString::fromStdString(r.value("error", std::string()));
    anotar(tr("Los tiempos del ensayo quedaron como duraciones previstas"), "ok");
    grabado_ = nullptr;
    grabar_ = false;
    emit correChanged();
    return {};
}

void EventoViewModel::descartarTiemposGrabados() {
    if (grabado_.is_null()) return;
    grabado_ = nullptr;
    emit correChanged();
}

// --- La escaleta para afuera y desde afuera (L10) -----------------------------------------------

std::string EventoViewModel::lineaExportar(const std::string& formato) const {
    if (formato == "mos") return evento::linea::escaletaMos(linea_, abierto_.toStdString(), e_.nombre);
    const auto hora = [this](int64_t ms) { return horaDe(ms).substr(0, 5); };
    const evento::linea::Resolucion r = corrida_ ? corrida_->resolver(ahoraMs())
                                                 : evento::linea::Corrida(linea_, entradaBase(false)).resolver(ahoraMs());
    return evento::linea::escaletaCsv(linea_, &r, salidaPorOmision(), hora);
}

json EventoViewModel::lineaImportar(const std::string& formato, const std::string& texto, bool probar, const std::string& nombreBloque) {
    const bool mos = formato == "mos" || (formato.empty() && texto.find("<mos") != std::string::npos);
    const evento::linea::Leida leida = mos ? evento::linea::leerMos(texto) : evento::linea::leerCsv(texto, nombreBloque);
    const json lote = evento::linea::importar(linea_, leida);
    if (!lote.value("ok", false)) return lote;
    json r = lineaEditar(lote["ops"], probar);
    if (!r.value("ok", false)) return r;
    r["ops"] = lote["ops"];
    r["avisos"] = lote["avisos"];
    r["bloques"] = lote["bloques"];
    return r;
}

namespace {
/// Una ruta o la URL que da el diálogo de archivos («file:///…»).
QString rutaLocal(const QString& r) {
    const QUrl u(r);
    return u.isLocalFile() ? u.toLocalFile() : r;
}
} // namespace

QString EventoViewModel::exportarEscaleta(const QString& formato, const QString& rutaOUrl) {
    if (abierto_.isEmpty()) return tr("No hay un evento abierto.");
    const QString ruta = rutaLocal(rutaOUrl);
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return tr("No se pudo escribir «%1».").arg(ruta);
    f.write(QByteArray::fromStdString(lineaExportar(formato.toStdString())));
    return {};
}

QVariantMap EventoViewModel::importarEscaleta(const QString& rutaOUrl, bool probar) {
    const QString ruta = rutaLocal(rutaOUrl);
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {{"ok", false}, {"error", tr("No se pudo leer «%1».").arg(ruta)}};
    const std::string formato = ruta.endsWith(".xml", Qt::CaseInsensitive) || ruta.endsWith(".mos", Qt::CaseInsensitive) ? "mos" : "csv";
    const json r = lineaImportar(formato, f.readAll().toStdString(), probar, QFileInfo(ruta).completeBaseName().toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, json::error_handler_t::replace))).object().toVariantMap();
}

} // namespace ba::app
