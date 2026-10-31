// ============================================================================
// src/app/ui/QrViewModel.cpp — ver QrViewModel.hpp
// ============================================================================
#include "ui/QrViewModel.hpp"
#include "i18n/I18n.hpp"

#include <fstream>
#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QSvgRenderer>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "qr/Iconos.hpp"
#include "qr/Qr.hpp"
#include "sala/EnlacesSala.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;

/// Un campo de un tipo de código.
struct Campo {
    const char* clave;
    const char* etiqueta;
    const char* ejemplo;
    bool largo;
};
/// Qué se puede codificar.
struct Tipo {
    const char* id;
    const char* titulo;
    const char* ayuda;
    std::vector<Campo> campos;
    /// true si el contenido lo arma el aparato (lleva claves que cambian).
    bool delAparato;
};

/// Un texto de los catálogos (marcado con QT_TRANSLATE_NOOP) en el idioma del operador.
QString tq(const char* s) { return *s ? QCoreApplication::translate("qr", s) : QString(); }

const std::vector<Tipo>& catalogo() {
    static const std::vector<Tipo> t = {
        {"enlace",
         QT_TRANSLATE_NOOP("qr", "Un enlace"),
         QT_TRANSLATE_NOOP("qr", "Una dirección web. Lo más usado y lo que menos ocupa."),
         {{"url", QT_TRANSLATE_NOOP("qr", "Dirección"), "https://…", false}},
         false},
        {"wifi",
         QT_TRANSLATE_NOOP("qr", "Red WiFi"),
         QT_TRANSLATE_NOOP("qr", "El teléfono ofrece conectarse solo, sin tipear la clave."),
         {{"red", QT_TRANSLATE_NOOP("qr", "Nombre de la red"), QT_TRANSLATE_NOOP("qr", "Estudio"), false},
          {"clave", QT_TRANSLATE_NOOP("qr", "Clave"), "", false},
          {"seguridad", QT_TRANSLATE_NOOP("qr", "Seguridad"), "WPA", false}},
         false},
        {"contacto",
         QT_TRANSLATE_NOOP("qr", "Un contacto"),
         QT_TRANSLATE_NOOP("qr", "El teléfono ofrece agregarlo a la agenda (vCard)."),
         {{"nombre", QT_TRANSLATE_NOOP("qr", "Nombre y apellido"), QT_TRANSLATE_NOOP("qr", "Ana Pérez"), false},
          {"telefono", QT_TRANSLATE_NOOP("qr", "Teléfono"), "+54 9 11 …", false},
          {"email", QT_TRANSLATE_NOOP("qr", "Correo"), "ana@…", false},
          {"organizacion", QT_TRANSLATE_NOOP("qr", "Organización"), "", false},
          {"cargo", QT_TRANSLATE_NOOP("qr", "Cargo"), "", false},
          {"web", QT_TRANSLATE_NOOP("qr", "Web"), "", false}},
         false},
        {"reunion",
         QT_TRANSLATE_NOOP("qr", "Una reunión"),
         QT_TRANSLATE_NOOP("qr", "Meet, Zoom o Teams. Si ya tenés el enlace entero, pegalo."),
         {{"plataforma", QT_TRANSLATE_NOOP("qr", "Plataforma"), "meet", false},
          {"codigo", QT_TRANSLATE_NOOP("qr", "Código o enlace"), "abc-defg-hij", false},
          {"clave", QT_TRANSLATE_NOOP("qr", "Clave (Zoom)"), "", false}},
         false},
        {"texto",
         QT_TRANSLATE_NOOP("qr", "Un texto"),
         QT_TRANSLATE_NOOP("qr", "Lo que sea. El teléfono lo muestra y ofrece copiarlo."),
         {{"texto", QT_TRANSLATE_NOOP("qr", "Texto"), "", true}},
         false},
        {"telefono",
         QT_TRANSLATE_NOOP("qr", "Llamar"),
         QT_TRANSLATE_NOOP("qr", "Al escanearlo, el teléfono ofrece llamar."),
         {{"numero", QT_TRANSLATE_NOOP("qr", "Número"), "+54 9 11 …", false}},
         false},
        {"correo",
         QT_TRANSLATE_NOOP("qr", "Escribir un correo"),
         QT_TRANSLATE_NOOP("qr", "Abre el correo con el destinatario puesto."),
         {{"direccion", QT_TRANSLATE_NOOP("qr", "Para"), "produccion@…", false},
          {"asunto", QT_TRANSLATE_NOOP("qr", "Asunto"), "", false},
          {"cuerpo", QT_TRANSLATE_NOOP("qr", "Texto"), "", true}},
         false},
        // --- Los del aparato: el contenido lo arma el sistema ----------------
        {"sala", QT_TRANSLATE_NOOP("qr", "Entrar a la conferencia"), QT_TRANSLATE_NOOP("qr", "El enlace de invitados de esta conferencia, con su PIN adentro."), {}, true},
        {"prompter", QT_TRANSLATE_NOOP("qr", "El teleprompter"), QT_TRANSLATE_NOOP("qr", "La pantalla del orador (solo lee, no lleva clave)."), {}, true},
        {"rotulos", QT_TRANSLATE_NOOP("qr", "Rotular desde el celular"), QT_TRANSLATE_NOOP("qr", "Los textos de la escena al aire, con su clave."), {}, true},
        // El que se proyecta o se imprime en el programa: lo escanean todos los
        // que estan en la sala, asi que va con la clave adentro. Pedirle a
        // doscientas personas que tipeen una clave es perder a la mitad.
        {"publico",
         QT_TRANSLATE_NOOP("qr", "Preguntas del publico"),
         QT_TRANSLATE_NOOP("qr", "La pagina para preguntar y votar desde la butaca, con su clave."),
         {},
         true},
    };
    return t;
}

const Tipo* tipoPorId(const QString& id) {
    for (const Tipo& t : catalogo())
        if (id == QString::fromUtf8(t.id)) return &t;
    return nullptr;
}

// --- Traducción entre lo guardado y el estilo del núcleo ---------------------

qr::Puntos puntosDe(const QString& s) {
    if (s == "redondeados") return qr::Puntos::Redondeados;
    if (s == "circulos") return qr::Puntos::Circulos;
    if (s == "separados") return qr::Puntos::Separados;
    if (s == "muy-redondeados") return qr::Puntos::MuyRedondeados;
    return qr::Puntos::Cuadrados;
}
qr::Ojos ojosDe(const QString& s) { return s == "redondeados" ? qr::Ojos::Redondeados : qr::Ojos::Cuadrados; }
qr::Marco marcoDe(const QString& s) {
    if (s == "simple") return qr::Marco::Simple;
    if (s == "redondeado") return qr::Marco::Redondeado;
    return qr::Marco::Ninguno;
}
qr::Correccion correccionDe(const QString& s) {
    if (s == "baja") return qr::Correccion::Baja;
    if (s == "alta") return qr::Correccion::Alta;
    if (s == "maxima") return qr::Correccion::Maxima;
    return qr::Correccion::Media;
}

std::mutex g_delEventoMu;
QVariantMap g_delEvento; ///< bajo g_delEventoMu (ponerEstiloDelEvento)

/// El estilo de fábrica: negro sobre blanco, cuadrado. Aburrido y siempre se lee. Con un
/// evento abierto con tema, sus colores (siempre legibles: ba::tema::coloresQr) y su logo.
QVariantMap estiloDeFabrica() {
    QVariantMap e{{"primario", "#000000"}, {"secundario", ""},    {"fondo", "#FFFFFF"},
                  {"puntos", "cuadrados"}, {"ojos", "cuadrados"}, {"marco", "ninguno"},
                  {"correccion", "media"}, {"zona", 4},           {"logo", 0.0},
                  {"icono", ""},           {"iconoColor", ""}};
    std::lock_guard<std::mutex> l(g_delEventoMu);
    for (auto it = g_delEvento.constBegin(); it != g_delEvento.constEnd(); ++it) e[it.key()] = it.value();
    return e;
}

qr::Estilo aEstilo(const QVariantMap& m) {
    qr::Estilo e;
    e.primario = m.value("primario", "#000000").toString().toStdString();
    e.secundario = m.value("secundario").toString().toStdString();
    e.fondo = m.value("fondo", "#FFFFFF").toString().toStdString();
    e.puntos = puntosDe(m.value("puntos").toString());
    e.ojos = ojosDe(m.value("ojos").toString());
    e.marco = marcoDe(m.value("marco").toString());
    e.correccion = correccionDe(m.value("correccion").toString());
    e.zonaSilenciosa = m.value("zona", 4).toInt();
    e.logo = m.value("logo", 0.0).toDouble();
    e.logoIcono = m.value("icono").toString().toStdString();
    e.logoColor = m.value("iconoColor").toString().toStdString();
    // El logo del evento (lo resuelve el tema: ponerEstiloDelEvento); en la dirección que va
    // al gateway viaja solo el pedido, él pone el suyo.
    e.logoDelEvento = m.value("logoEvento").toBool();
    if (e.logoDelEvento) e.logoSrc = m.value("logoSrc").toString().toStdString();
    // Un ícono sin hueco no se vería: si eligieron uno, se abre lugar.
    if (!e.logoIcono.empty() && e.logo <= 0) e.logo = 0.22;
    return e;
}

QString limpiarNombre(QString n) {
    n = n.trimmed();
    for (const QChar malo : QString(R"(<>:"/\|?*)"))
        n.remove(malo);
    while (n.endsWith('.'))
        n.chop(1);
    return n.trimmed();
}

} // namespace

struct QrViewModel::Impl {
    AppRuntime* rt = nullptr;
    /// Los tótems de registro: [{id, nombre, url}] (ui/TotemsViewModel).
    std::function<QVariantList()> totems;
    EventoViewModel* evento = nullptr;
    ConferenceViewModel* sala = nullptr;

    QString ambito = "global";
    /// ámbito -> lista de códigos guardados.
    QMap<QString, QVariantList> biblioteca;

    // --- El editor ---
    QString abierto;
    QString nombre;
    QString tipo = "enlace";
    QVariantMap datos;
    QVariantMap estilo = estiloDeFabrica();
    QString contenido; ///< el texto ya armado
    QString aviso;
    int revision = 0;
    bool sucio = false;
    /// Lo que devolvió el gateway para los tipos del aparato.
    QMap<QString, QString> delAparato;

    std::filesystem::path archivoDe(const QString& a) const {
        if (a == "evento") {
            if (evento == nullptr || evento->carpetaDeDiscursos().isEmpty()) return {};
            // Al lado de Discursos/, dentro del evento.
            return std::filesystem::path(evento->carpetaDeDiscursos().toStdString()).parent_path() / "QR" /
                   "qr.json";
        }
        return rt->dataRoot() / "Config" / "qr.json";
    }

    void leer(const QString& a) {
        QVariantList out;
        const auto p = archivoDe(a);
        if (!p.empty()) {
            std::ifstream f(p, std::ios::binary);
            const json o = f ? json::parse(f, nullptr, false) : json();
            if (o.is_array())
                for (const json& x : o) {
                    if (!x.is_object()) continue;
                    QVariantMap m;
                    m["id"] = QString::fromStdString(x.value("id", ""));
                    m["nombre"] = QString::fromStdString(x.value("nombre", ""));
                    m["tipo"] = QString::fromStdString(x.value("tipo", "enlace"));
                    QVariantMap datos;
                    // Con nombre: el temporal de `value()` no vive lo que dura
                    // el bucle y el proxy de items() quedaria colgado.
                    const json jd = x.value("datos", json::object());
                    for (const auto& [k, v] : jd.items())
                        if (v.is_string()) datos[QString::fromStdString(k)] = QString::fromStdString(v);
                    m["datos"] = datos;
                    QVariantMap est = estiloDeFabrica();
                    const json je = x.value("estilo", json::object());
                    for (const auto& [k, v] : je.items()) {
                        if (v.is_string())
                            est[QString::fromStdString(k)] = QString::fromStdString(v);
                        else if (v.is_number_float())
                            est[QString::fromStdString(k)] = v.get<double>();
                        else if (v.is_number_integer())
                            est[QString::fromStdString(k)] = v.get<int>();
                    }
                    m["estilo"] = est;
                    if (m["id"].toString().isEmpty()) continue;
                    out.append(m);
                }
        }
        biblioteca[a] = out;
    }

    void guardarArchivo(const QString& a) const {
        const auto p = archivoDe(a);
        if (p.empty()) return;
        QDir().mkpath(QString::fromStdString(p.parent_path().string()));
        json arr = json::array();
        for (const QVariant& v : biblioteca.value(a)) {
            const QVariantMap m = v.toMap();
            json datos = json::object();
            const QVariantMap md = m.value("datos").toMap();
            for (auto it = md.begin(); it != md.end(); ++it)
                datos[it.key().toStdString()] = it.value().toString().toStdString();
            json est = json::object();
            const QVariantMap me = m.value("estilo").toMap();
            for (auto it = me.begin(); it != me.end(); ++it) {
                if (it.value().typeId() == QMetaType::Double)
                    est[it.key().toStdString()] = it.value().toDouble();
                else if (it.value().typeId() == QMetaType::Int)
                    est[it.key().toStdString()] = it.value().toInt();
                else
                    est[it.key().toStdString()] = it.value().toString().toStdString();
            }
            arr.push_back(json{{"id", m.value("id").toString().toStdString()},
                               {"nombre", m.value("nombre").toString().toStdString()},
                               {"tipo", m.value("tipo").toString().toStdString()},
                               {"datos", datos},
                               {"estilo", est}});
        }
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << arr.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }
};

namespace {
QrViewModel* gElDeLaApp = nullptr; ///< ver elDeLaApp
}

QrViewModel::QrViewModel(AppRuntime& rt, EventoViewModel* evento, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    gElDeLaApp = this;
    d_->rt = &rt;
    d_->evento = evento;
    d_->sala = sala;
    refrescar();
    if (evento != nullptr) connect(evento, &EventoViewModel::guionChanged, this, [this]() { refrescar(); });
    resolver();
}

QrViewModel::~QrViewModel() {
    if (gElDeLaApp == this) gElDeLaApp = nullptr;
}

// --- Lo que ve la interfaz -----------------------------------------------------

QVariantList QrViewModel::codigos() const {
    QVariantList out;
    for (const QVariant& v : d_->biblioteca.value(d_->ambito)) {
        QVariantMap m = v.toMap();
        const Tipo* t = tipoPorId(m.value("tipo").toString());
        m["tipoTitulo"] = t == nullptr ? m.value("tipo") : tq(t->titulo);
        // Un resumen legible: el primer campo con algo.
        QString resumen;
        if (t != nullptr) {
            if (t->delAparato) {
                resumen = tr("lo arma el sistema");
            } else {
                const QVariantMap datos = m.value("datos").toMap();
                for (const Campo& c : t->campos) {
                    const QString s = datos.value(QString::fromUtf8(c.clave)).toString();
                    if (!s.isEmpty()) {
                        resumen = s.size() > 52 ? s.left(52) + "…" : s;
                        break;
                    }
                }
            }
        }
        m["resumen"] = resumen;
        out.append(m);
    }
    return out;
}

QString QrViewModel::ambito() const { return d_->ambito; }

void QrViewModel::setAmbito(const QString& a) {
    const QString n = a == "evento" ? "evento" : "global";
    if (n == d_->ambito) return;
    d_->ambito = n;
    refrescar();
}

QString QrViewModel::sinEvento() const {
    if (d_->evento == nullptr || d_->evento->carpetaDeDiscursos().isEmpty())
        return tr("No hay ningún evento abierto: los códigos del evento se guardan en su carpeta.");
    return {};
}

QVariantList QrViewModel::tipos() const {
    QVariantList out;
    for (const Tipo& t : catalogo()) {
        QVariantList campos;
        for (const Campo& c : t.campos)
            campos.append(QVariantMap{{"clave", QString::fromUtf8(c.clave)},
                                      {"etiqueta", tq(c.etiqueta)},
                                      {"ejemplo", tq(c.ejemplo)},
                                      {"largo", c.largo}});
        out.append(QVariantMap{{"id", QString::fromUtf8(t.id)},
                               {"titulo", tq(t.titulo)},
                               {"ayuda", tq(t.ayuda)},
                               {"campos", campos},
                               {"delAparato", t.delAparato}});
    }
    return out;
}

QVariantList QrViewModel::iconos() const {
    QVariantList out;
    out.append(QVariantMap{{"id", ""}, {"titulo", tr("Sin ícono")}});
    for (const qr::Icono& i : qr::iconos())
        out.append(QVariantMap{{"id", QString::fromStdString(i.id)},
                               {"titulo", QString::fromStdString(ba::i18n::traducir("iconos", i.titulo.c_str()))},
                               {"trazos", QString::fromStdString(i.trazos)}});
    return out;
}

QString QrViewModel::abierto() const { return d_->abierto; }

QVariantMap QrViewModel::actual() const {
    return QVariantMap{
        {"nombre", d_->nombre}, {"tipo", d_->tipo}, {"datos", d_->datos}, {"estilo", d_->estilo}};
}

QString QrViewModel::contenido() const { return d_->contenido; }
QString QrViewModel::aviso() const { return d_->aviso; }
int QrViewModel::revision() const { return d_->revision; }
bool QrViewModel::sucio() const { return d_->sucio; }

int QrViewModel::modulos() const {
    return d_->contenido.isEmpty() ? 0
                                   : qr::lado(d_->contenido.toStdString(),
                                              correccionDe(d_->estilo.value("correccion").toString()));
}

// --- Armar el contenido -----------------------------------------------------

namespace {
/// El texto que va adentro del código, según el tipo y los datos.
QString armar(const QString& tipo, const QVariantMap& d) {
    const auto v = [&d](const char* k) { return d.value(k).toString().toStdString(); };
    if (tipo == "enlace") return QString::fromStdString(v("url"));
    if (tipo == "texto") return QString::fromStdString(v("texto"));
    if (tipo == "wifi")
        return QString::fromStdString(
            qr::wifi(v("red"), v("clave"), d.value("seguridad", "WPA").toString().toStdString()));
    if (tipo == "contacto")
        return QString::fromStdString(
            qr::contacto(v("nombre"), v("telefono"), v("email"), v("organizacion"), v("cargo"), v("web")));
    if (tipo == "reunion")
        return QString::fromStdString(
            qr::reunion(d.value("plataforma", "meet").toString().toStdString(), v("codigo"), v("clave")));
    if (tipo == "telefono") return QString::fromStdString(qr::telefono(v("numero")));
    if (tipo == "correo") return QString::fromStdString(qr::correo(v("direccion"), v("asunto"), v("cuerpo")));
    return {};
}
} // namespace

void QrViewModel::resolver() {
    // Los del aparato llevan claves que cambian: se los pide a la sala (el
    // modelo sala::EnlacesSala) en vez de guardarlos armados. Se piden todos
    // de una y se cachean.
    if (d_->sala == nullptr) return;
    const auto guardar = [this](const QString& tipo) {
        return [this, tipo](const sala::Enlace& e) {
            if (!e.ok) return;
            d_->delAparato[tipo] = QString::fromStdString(e.url);
            if (d_->tipo == tipo) {
                d_->contenido = d_->delAparato[tipo];
                d_->aviso.clear();
                ++d_->revision;
                emit editorCambio();
            }
        };
    };
    sala::EnlacesSala& enlaces = d_->sala->enlaces();
    // Los invitados: el de la red local; si la sala no los recibe por la red, el de afuera.
    {
        sala::EnlacesSala* en = &enlaces;
        const auto alGuardar = guardar("sala");
        enlaces.ingreso(false, true, [en, alGuardar](const sala::Enlace& e) {
            if (e.ok) return alGuardar(e);
            en->ingreso(true, true, alGuardar);
        });
    }
    enlaces.prompter(false, "orador", true, guardar("prompter"));
    enlaces.rotulos(false, true, guardar("rotulos"));
    enlaces.publico(false, guardar("publico"));
}

namespace {
/// Un id corto y estable para un código nuevo.
QString nuevoId() {
    return "q" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36) +
           QString::number(QRandomGenerator::global()->bounded(1000), 36);
}
} // namespace

void QrViewModel::refrescar() {
    d_->leer("global");
    d_->leer("evento");
    emit bibliotecaCambio();
}

void QrViewModel::nuevo(const QString& tipo) {
    d_->abierto.clear();
    d_->nombre.clear();
    d_->datos.clear();
    d_->estilo = estiloDeFabrica();
    d_->sucio = false;
    setTipo(tipo.isEmpty() ? "enlace" : tipo);
}

void QrViewModel::abrir(const QString& id) {
    for (const QVariant& v : d_->biblioteca.value(d_->ambito)) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() != id) continue;
        d_->abierto = id;
        d_->nombre = m.value("nombre").toString();
        d_->tipo = m.value("tipo").toString();
        d_->datos = m.value("datos").toMap();
        d_->estilo = m.value("estilo").toMap();
        d_->sucio = false;
        setTipo(d_->tipo); // recalcula el contenido
        return;
    }
}

void QrViewModel::setTipo(const QString& tipo) {
    const Tipo* t = tipoPorId(tipo);
    if (t == nullptr) return;
    if (d_->tipo != tipo) {
        d_->tipo = tipo;
        d_->sucio = true;
    }
    if (t->delAparato) {
        d_->contenido = d_->delAparato.value(tipo);
        d_->aviso = d_->contenido.isEmpty()
                        ? tr("Todavía no tengo ese enlace: hace falta que el servidor esté corriendo.")
                        : QString();
    } else {
        d_->contenido = armar(tipo, d_->datos);
        d_->aviso.clear();
    }
    // Se prueba a generarlo: así el aviso de "no entra" sale ACÁ y no cuando
    // alguien va a imprimirlo.
    if (!d_->contenido.isEmpty()) {
        std::string err;
        if (qr::svg(d_->contenido.toStdString(), aEstilo(d_->estilo), &err).empty())
            d_->aviso = QString::fromStdString(err);
    }
    ++d_->revision;
    emit editorCambio();
}

void QrViewModel::setDato(const QString& clave, const QString& valor) {
    if (d_->datos.value(clave).toString() == valor) return;
    d_->datos[clave] = valor;
    d_->sucio = true;
    setTipo(d_->tipo);
}

void QrViewModel::ponerEstiloDelEvento(const QVariantMap& estilo) {
    std::lock_guard<std::mutex> l(g_delEventoMu);
    g_delEvento = estilo;
}

void QrViewModel::estiloSimple() {
    setEstilo("primario", "#000000");
    setEstilo("secundario", "");
    setEstilo("puntos", "cuadrados");
    setEstilo("ojos", "cuadrados");
    setEstilo("marco", "ninguno");
    setEstilo("icono", "");
}

void QrViewModel::setEstilo(const QString& clave, const QVariant& valor) {
    if (d_->estilo.value(clave) == valor) return;
    d_->estilo[clave] = valor;
    d_->sucio = true;
    setTipo(d_->tipo);
}

// --- La biblioteca ------------------------------------------------------------

QString QrViewModel::guardar(const QString& nombre) {
    const QString n = limpiarNombre(nombre.isEmpty() ? d_->nombre : nombre);
    if (n.isEmpty()) return tr("Ponele un nombre.");
    if (d_->ambito == "evento" && !sinEvento().isEmpty()) return sinEvento();
    QVariantList lista = d_->biblioteca.value(d_->ambito);
    QVariantMap m;
    m["id"] = d_->abierto.isEmpty() ? nuevoId() : d_->abierto;
    m["nombre"] = n;
    m["tipo"] = d_->tipo;
    m["datos"] = d_->datos;
    m["estilo"] = d_->estilo;
    bool reemplazado = false;
    for (QVariant& v : lista) {
        if (v.toMap().value("id").toString() != m["id"].toString()) continue;
        v = m;
        reemplazado = true;
        break;
    }
    if (!reemplazado) lista.append(m);
    d_->biblioteca[d_->ambito] = lista;
    d_->guardarArchivo(d_->ambito);
    d_->abierto = m["id"].toString();
    d_->nombre = n;
    d_->sucio = false;
    diag::registro().linea(diag::Nivel::Info, "qr", 0,
                           ba::i18n::llenar(ba::i18n::traducir("qr", "guardado «%1» (%2, %3)"),
                                            {n.toStdString(), d_->tipo.toStdString(), d_->ambito.toStdString()}));
    emit bibliotecaCambio();
    emit editorCambio();
    return {};
}

QString QrViewModel::borrar(const QString& id) {
    QVariantList lista = d_->biblioteca.value(d_->ambito);
    const auto antes = lista.size();
    for (int i = 0; i < lista.size(); ++i)
        if (lista[i].toMap().value("id").toString() == id) {
            lista.removeAt(i);
            break;
        }
    if (lista.size() == antes) return tr("No lo encuentro.");
    d_->biblioteca[d_->ambito] = lista;
    d_->guardarArchivo(d_->ambito);
    diag::registro().linea(diag::Nivel::Info, "qr", 0, ba::i18n::llenar(ba::i18n::traducir("qr", "borrado %1"), {id.toStdString()}));
    if (d_->abierto == id) {
        d_->abierto.clear();
        d_->nombre.clear();
        emit editorCambio();
    }
    emit bibliotecaCambio();
    return {};
}

QString QrViewModel::copiarA(const QString& id, const QString& ambito) {
    const QString destino = ambito == "evento" ? "evento" : "global";
    if (destino == d_->ambito) return tr("Ya está en ese ámbito.");
    if (destino == "evento" && !sinEvento().isEmpty()) return sinEvento();
    for (const QVariant& v : d_->biblioteca.value(d_->ambito)) {
        QVariantMap m = v.toMap();
        if (m.value("id").toString() != id) continue;
        m["id"] = nuevoId(); // copia, no el mismo: se pueden editar por separado
        QVariantList lista = d_->biblioteca.value(destino);
        lista.append(m);
        d_->biblioteca[destino] = lista;
        d_->guardarArchivo(destino);
        emit bibliotecaCambio();
        emit notify(destino == "evento" ? tr("Copiado al evento") : tr("Copiado a la biblioteca general"),
                    "ok");
        return {};
    }
    return tr("No lo encuentro.");
}

// --- Salidas --------------------------------------------------------------------

QString QrViewModel::svgActual() const {
    if (d_->contenido.isEmpty()) return {};
    return QString::fromStdString(qr::svg(d_->contenido.toStdString(), aEstilo(d_->estilo)));
}

QString QrViewModel::svgDe(const QString& id) const {
    if (id == "actual" || id.isEmpty()) return svgActual();
    for (const QString& a : {QString("global"), QString("evento")})
        for (const QVariant& v : d_->biblioteca.value(a)) {
            const QVariantMap m = v.toMap();
            if (m.value("id").toString() != id) continue;
            const QString tipo = m.value("tipo").toString();
            const Tipo* t = tipoPorId(tipo);
            const QString texto = t != nullptr && t->delAparato ? d_->delAparato.value(tipo)
                                                                : armar(tipo, m.value("datos").toMap());
            if (texto.isEmpty()) return {};
            return QString::fromStdString(qr::svg(texto.toStdString(), aEstilo(m.value("estilo").toMap())));
        }
    return {};
}

QString QrViewModel::contenidoDe(const QString& tipo, const QVariantMap& datos) const {
    const Tipo* t = tipoPorId(tipo);
    if (t == nullptr) return {};
    return t->delAparato ? d_->delAparato.value(tipo) : armar(tipo, datos);
}

QString QrViewModel::svgSuelto(const QString& tipo, const QVariantMap& datos,
                               const QVariantMap& estilo) const {
    const QString texto = contenidoDe(tipo, datos);
    if (texto.isEmpty()) return {};
    QVariantMap e = estiloDeFabrica();
    for (auto it = estilo.begin(); it != estilo.end(); ++it)
        e[it.key()] = it.value();
    return QString::fromStdString(qr::svg(texto.toStdString(), aEstilo(e)));
}

QVariantMap QrViewModel::paraProyectar(const QString& id) const {
    for (const QVariant& v : todos()) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() != id) continue;
        const QString texto = m.value("contenido").toString();
        if (texto.isEmpty()) return {};
        QVariantMap estilo = estiloDeFabrica();
        const QVariantMap guardado = m.value("estilo").toMap();
        for (auto it = guardado.constBegin(); it != guardado.constEnd(); ++it)
            estilo[it.key()] = it.value();
        return QVariantMap{{"texto", texto},
                           {"nombre", m.value("nombre")},
                           {"estilo", QString::fromStdString(qr::aConsulta(aEstilo(estilo)))}};
    }
    return {};
}

namespace {
struct DelAparato {
    const char* tipo;
    const char* etiqueta;
};
const DelAparato kDelAparato[] = {{"publico", QT_TRANSLATE_NOOP("qr", "Público: preguntar y votar (con la clave adentro)")},
                                  {"sala", QT_TRANSLATE_NOOP("qr", "Conferencia: entrar como invitado (con el PIN adentro)")},
                                  {"prompter", QT_TRANSLATE_NOOP("qr", "Prompter: la vista del orador")},
                                  {"rotulos", QT_TRANSLATE_NOOP("qr", "Rótulos: cargarlos desde el teléfono")}};
} // namespace

QrViewModel* QrViewModel::elDeLaApp() { return gElDeLaApp; }

void QrViewModel::setTotems(std::function<QVariantList()> f) { d_->totems = std::move(f); }

namespace {
/// La url de un tótem de registro por su id ("" si no está o está apagado).
QString urlDeTotem(const std::function<QVariantList()>& totems, const QString& id) {
    if (!totems) return {};
    for (const QVariant& v : totems())
        if (v.toMap().value("id").toString() == id) return v.toMap().value("url").toString();
    return {};
}
} // namespace

QVariantList QrViewModel::paraElegir() const {
    QVariantList out;
    for (const auto& x : kDelAparato)
        out << QVariantMap{{"value", QString("aparato:") + x.tipo}, {"label", tq(x.etiqueta)}};
    // Los tótems de registro del evento: el QR que se escanea para registrarse.
    if (d_->totems)
        for (const QVariant& v : d_->totems()) {
            const QVariantMap m = v.toMap();
            out << QVariantMap{{"value", "registro:" + m.value("id").toString()},
                               {"label", tr("Registro de personas: %1").arg(m.value("nombre").toString())}};
        }
    for (const QVariant& v : todos()) {
        const QVariantMap m = v.toMap();
        out << QVariantMap{{"value", m.value("id")}, {"label", tr("%1 (guardado)").arg(m.value("nombre").toString())}};
    }
    return out;
}

QVariantMap QrViewModel::paraProyectarElegido(const QString& elegido) {
    if (elegido.startsWith("registro:")) {
        const QString url = urlDeTotem(d_->totems, elegido.mid(9));
        if (url.isEmpty()) return {};
        return QVariantMap{{"texto", url},
                           {"nombre", tr("Registro de personas")},
                           {"estilo", QString::fromStdString(qr::aConsulta(aEstilo(estiloDeFabrica())))}};
    }
    if (!elegido.startsWith("aparato:")) return paraProyectar(elegido);
    const QString tipo = elegido.mid(8);
    const QString texto = d_->delAparato.value(tipo);
    QString etiqueta;
    for (const auto& x : kDelAparato)
        if (tipo == x.tipo) etiqueta = QString::fromUtf8(x.etiqueta).section(':', 0, 0);
    if (texto.isEmpty()) {
        resolver(); // la sala lo da en un momento
        return QVariantMap{{"pendiente", true}, {"nombre", etiqueta}};
    }
    return QVariantMap{{"texto", texto},
                       {"nombre", etiqueta},
                       {"estilo", QString::fromStdString(qr::aConsulta(aEstilo(estiloDeFabrica())))}};
}

QString QrViewModel::svgElegido(const QString& elegido, const QString& textoLibre) {
    if (elegido.startsWith("registro:")) {
        const QString url = urlDeTotem(d_->totems, elegido.mid(9));
        return url.isEmpty() ? QString() : QString::fromStdString(qr::svg(url.toStdString(), aEstilo(estiloDeFabrica())));
    }
    if (elegido.startsWith("aparato:")) {
        const QString texto = d_->delAparato.value(elegido.mid(8));
        if (texto.isEmpty()) {
            resolver();
            return {};
        }
        return QString::fromStdString(qr::svg(texto.toStdString(), aEstilo(estiloDeFabrica())));
    }
    if (!elegido.isEmpty()) return svgDe(elegido);
    if (textoLibre.trimmed().isEmpty()) return {};
    return QString::fromStdString(qr::svg(textoLibre.trimmed().toStdString(), aEstilo(estiloDeFabrica())));
}

QVariantList QrViewModel::todos() const {
    QVariantList out;
    for (const QString& a : {QString("global"), QString("evento")})
        for (const QVariant& v : d_->biblioteca.value(a)) {
            QVariantMap m = v.toMap();
            m["ambito"] = a;
            const Tipo* t = tipoPorId(m.value("tipo").toString());
            m["contenido"] =
                t == nullptr ? QString()
                             : (t->delAparato ? d_->delAparato.value(m.value("tipo").toString())
                                              : armar(m.value("tipo").toString(), m.value("datos").toMap()));
            out.append(m);
        }
    return out;
}

QString QrViewModel::escribir(const QString& svgTexto, const QString& nombre, const QString& formato,
                              int px) const {
    if (svgTexto.isEmpty()) return {};
    const QString dir = QString::fromStdWString(d_->rt->dataRoot().wstring()) + "/Exportado";
    QDir().mkpath(dir);
    const QString base = limpiarNombre(nombre.isEmpty() ? "codigo-qr" : nombre);
    const QString ruta = dir + "/" + base + (formato == "png" ? ".png" : ".svg");
    if (formato == "png") {
        QImage img(px, px, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        QSvgRenderer r(svgTexto.toUtf8());
        r.render(&p);
        p.end();
        return img.save(ruta) ? ruta : QString();
    }
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(svgTexto.toUtf8());
    return ruta;
}

QString QrViewModel::exportar(const QString& formato, int px) {
    const QString svgTexto = svgActual();
    if (svgTexto.isEmpty()) {
        emit notify(d_->aviso.isEmpty() ? tr("No hay nada que exportar.") : d_->aviso, "error");
        return {};
    }
    // Se rasteriza el MISMO SVG que se guarda como .svg: no hay dos dibujos
    // que puedan diferir.
    const QString ruta = escribir(svgTexto, d_->nombre, formato, px);
    if (ruta.isEmpty()) {
        emit notify(tr("No pude escribir el archivo."), "error");
        return {};
    }
    diag::registro().linea(diag::Nivel::Info, "qr", 0, ba::i18n::llenar(ba::i18n::traducir("qr", "exportado a %1"), {ruta.toStdString()}));
    emit notify(tr("Guardado en %1").arg(ruta), "ok");
    return ruta;
}

// --- La vista previa ---------------------------------------------------------------

QImage QrImageProvider::requestImage(const QString& id, QSize* size, const QSize& pedido) {
    // "actual?rev=3" o "<id>?rev=3": el `rev` solo sirve para que QML no
    // muestre la imagen vieja de su cache.
    const QString cual = id.section('?', 0, 0);
    const int lado = pedido.width() > 0 ? pedido.width() : 512;
    QImage img(lado, lado, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    // "texto/<texto con %>": el QR de cualquier texto o URL (lo usa qr_mostrar:
    // el asistente le muestra al operador un enlace que no puede copiar).
    QString s;
    if (cual.startsWith("texto/")) {
        const QString texto = QUrl::fromPercentEncoding(cual.mid(6).toUtf8());
        s = QString::fromStdString(ba::qr::svg(texto.toStdString(), ba::qr::Estilo{}));
    } else {
        s = vm_.svgDe(cual);
    }
    if (!s.isEmpty()) {
        QPainter p(&img);
        QSvgRenderer r(s.toUtf8());
        r.render(&p);
    }
    if (size != nullptr) *size = img.size();
    return img;
}

} // namespace ba::app
