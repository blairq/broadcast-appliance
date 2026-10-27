// ============================================================================
// src/app/ui/RielConfig.cpp — ver RielConfig.hpp
// ============================================================================
#include "ui/RielConfig.hpp"

#include <algorithm>
#include <set>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app {
namespace {

/// El catálogo. `page` es el índice en el StackLayout de Main.qml, que NO es
/// el orden en que se muestran: el orden de acá es el del riel.
struct Item {
    int page;
    const char* id; ///< corto y estable: es como se la pide por MCP
    const char* icon;
    const char* tip;
};
struct Grupo {
    const char* titulo;
    std::vector<Item> items;
};

const std::vector<Grupo>& catalogo() {
    // Por momento de uso: arriba lo que se toca en vivo y bajo presión; abajo lo
    // que se configura una vez. Las salidas (pantallas, multiview, redes) juntas.
    static const std::vector<Grupo> g = {
        {QT_TRANSLATE_NOOP("riel", "En vivo"),
         {{0, "dashboard", "⌂", QT_TRANSLATE_NOOP("riel", "Dashboard")},
          {1, "escenas", "▦", QT_TRANSLATE_NOOP("riel", "Escenas y fuentes")},
          {21, "camaras", "⌖", QT_TRANSLATE_NOOP("riel", "Cámaras PTZ: mover, zoom, presets y mejorar la imagen de cada cámara")},
          {2, "sala", "☎", QT_TRANSLATE_NOOP("riel", "Conferencia (lobby, invitados, chat)")},
          {12, "rotulos", "✎", QT_TRANSLATE_NOOP("riel", "Rótulos en vivo: los textos de la escena que está al aire")},
          {15, "prompter", "▤", QT_TRANSLATE_NOOP("riel", "Teleprompter: los discursos y el control del atril")},
          {19, "publico", "☷", QT_TRANSLATE_NOOP("riel", "Público: las preguntas de la gente y la urna, para moderar en vivo")},
          {26, "asamblea", "⚖",
           QT_TRANSLATE_NOOP("riel", "Asamblea: central de conferencia (micrófonos de banca), recinto, orden del día y votación")}}},
        {QT_TRANSLATE_NOOP("riel", "Evento"),
         {{10, "guion", "≣",
           QT_TRANSLATE_NOOP("riel", "Evento: el abierto (con sus escenas, mezclador, medios y asamblea), su guion, padrón, orden del día y "
           "web; evento nuevo o abrir otro")},
          {20, "medios", "♫", QT_TRANSLATE_NOOP("riel", "Medios: música, videos, imágenes y presentaciones, con vista previa")},
          {11, "graficos", "◈", QT_TRANSLATE_NOOP("riel", "Gráficos animados: zócalos, placas y tickers")},
          {17, "qr", "⊞", QT_TRANSLATE_NOOP("riel", "Códigos QR: la biblioteca y el estudio de diseño")},
          {18, "personas", "☻", QT_TRANSLATE_NOOP("riel", "Personas: quién es quién, para que el sistema los reconozca")},
          {27, "historico", "⌛",
           QT_TRANSLATE_NOOP("riel", "Histórico: todos los eventos, qué pasó en cada uno, con qué configuración, qué se grabó y transmitió")}}},
        {QT_TRANSLATE_NOOP("riel", "Salidas"),
         {{16, "pantallas", "⧉", QT_TRANSLATE_NOOP("riel", "Pantallas: los monitores de esta máquina y las TV de la red, y qué va en cada uno")},
          {28, "multiview", "▥",
           QT_TRANSLATE_NOOP("riel", "Multiview: varias fuentes en un muro, con separación y bordes de color, tally y vúmetros; a otro monitor")},
          {3, "youtube", "▶", QT_TRANSLATE_NOOP("riel", "YouTube (cuenta, videos, en vivo)")},
          {4, "twitch", "◆", QT_TRANSLATE_NOOP("riel", "Twitch (canal, vivo, chat)")},
          {5, "facebook", "f", QT_TRANSLATE_NOOP("riel", "Facebook (Páginas, vivos, videos)")}}},
        // Primero lo que entra (video, audio), después DE DÓNDE sale (los dispositivos de
        // la máquina, con su nombre) y al final qué se le hace a cada señal (Video, Audio).
        {QT_TRANSLATE_NOOP("riel", "Señales y equipos"),
         {{8, "entradas", "◉",
           QT_TRANSLATE_NOOP("riel", "Entradas de video: las cámaras, capturadoras, NDI y señales de red que usa el evento, con su imagen "
           "y en qué escenas están")},
          {31, "entradas_audio", "🎙",
           QT_TRANSLATE_NOOP("riel", "Entradas de audio: los micrófonos, líneas y placas que usa el evento, y si están conectados")},
          {24, "equipos", "⌁",
           QT_TRANSLATE_NOOP("riel", "Dispositivos y equipos: todo lo de audio y video de la máquina con el nombre que le pusiste y si "
           "está conectado; buscar en la red y el USB y manejarlo (ONVIF, claves, PTZ, UVC)")},
          {22, "video", "⧈", QT_TRANSLATE_NOOP("riel", "Video: qué se le hace a cada entrada (croma, color, recorte) y salidas con nombre")},
          {9, "audio", "🎚", QT_TRANSLATE_NOOP("riel", "Audio: el procesamiento de cada entrada de audio (módulos LSP por fuente)")},
          {25, "botonera", "▣", QT_TRANSLATE_NOOP("riel", "Botonera: superficie de control en una tablet (QR), Stream Deck o MIDI")}}},
        {QT_TRANSLATE_NOOP("riel", "Sistema"),
         {{7, "asistente", "✦", QT_TRANSLATE_NOOP("riel", "Asistente: la terminal del agente que trabaja con vos")},
          {14, "agentes", "⚇", QT_TRANSLATE_NOOP("riel", "Agentes: los roles automáticos, qué hacen y qué hicieron")},
          {23, "plugins", "⌬",
           QT_TRANSLATE_NOOP("riel", "Plugins: programas en Python para equipos raros, tarjetas y automatizaciones")},
          {30, "latencias", "⏱",
           QT_TRANSLATE_NOOP("riel", "Latencias del pipeline: cuánto tarda cada cosa, de cada entrada a cada salida (video y audio, "
           "tarjeta por tarjeta, módulo por módulo), y los análisis")},
          {29, "enlaces", "⇄",
           QT_TRANSLATE_NOOP("riel", "Servicios: todos los enlaces web y sus códigos QR (invitados, público, teleprompter, rótulos, "
           "botonera, sesión remota…) con su estado, para activar, copiar, abrir o mostrar en QR")},
          // Ante última: se mira cuando algo anduvo mal, no en el día a día.
          {13, "registro", "❑", QT_TRANSLATE_NOOP("riel", "Logs del sistema: todo lo que pasa, de los tres procesos")},
          {6, "configuracion", "⚙", QT_TRANSLATE_NOOP("riel", "Configuración")}}},
    };
    return g;
}

/// El título corto de una página: la primera parte del tip, sin la aclaración.
QString titulo(const char* tip) {
    QString t = QCoreApplication::translate("riel", tip); // traducido: el título es lo de antes de «:» o « (»
    const int dosPuntos = t.indexOf(':');
    if (dosPuntos > 0) t = t.left(dosPuntos);
    const int parentesis = t.indexOf(" (");
    if (parentesis > 0) t = t.left(parentesis);
    return t.trimmed();
}

constexpr int kMin = 30;
constexpr int kMax = 64;
// Abierta tiene que entrar el nombre mas largo del catalogo sin cortarlo, y no
// tapar media pantalla: es una barra que se corre, no un panel.
constexpr int kAnchoMin = 180;
constexpr int kAnchoMax = 360;
/// Configuración es la única que NO se puede esconder: es por donde se vuelve.
/// Sin esto, esconderla dejaría al operador sin manera de arreglarlo.
constexpr int kPaginaConfig = 6;
// El asistente tiene su botón grande fijo, fuera de la tira (RielNavegacion): sigue en el
// catálogo (su id, su título, sacarlo a ventana) pero no se repite como botón chico.
constexpr int kPaginaAsistente = 7;

} // namespace

struct RielConfig::Impl {
    std::filesystem::path archivo;
    QString posicion = "izquierda";
    int tamano = 42;
    bool separadores = true;
    bool expandir = true;
    int anchoAbierto = 244;
    std::set<int> escondidas;

    void guardar() const {
        if (archivo.empty()) return;
        QDir().mkpath(QString::fromStdString(archivo.parent_path().string()));
        QJsonArray esc;
        for (const int p : escondidas)
            esc.append(p);
        const QJsonObject o{{"posicion", posicion},         {"tamano", tamano},
                            {"separadores", separadores},   {"expandir", expandir},
                            {"anchoAbierto", anchoAbierto}, {"escondidas", esc}};
        QFile f(QString::fromStdString(archivo.string()));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        f.write(QJsonDocument(o).toJson());
    }

    void leer() {
        if (archivo.empty()) return;
        QFile f(QString::fromStdString(archivo.string()));
        if (!f.open(QIODevice::ReadOnly)) return;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QString p = o.value("posicion").toString(posicion);
        if (p == "izquierda" || p == "derecha" || p == "arriba" || p == "abajo") posicion = p;
        tamano = std::clamp(o.value("tamano").toInt(tamano), kMin, kMax);
        separadores = o.value("separadores").toBool(separadores);
        expandir = o.value("expandir").toBool(expandir);
        anchoAbierto = std::clamp(o.value("anchoAbierto").toInt(anchoAbierto), kAnchoMin, kAnchoMax);
        for (const QJsonValue& v : o.value("escondidas").toArray())
            if (v.toInt(-1) != kPaginaConfig) escondidas.insert(v.toInt());
    }
};

RielConfig::RielConfig(std::filesystem::path archivo, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->archivo = std::move(archivo);
    d_->leer();
}

RielConfig::~RielConfig() = default;

QString RielConfig::posicion() const { return d_->posicion; }

void RielConfig::setPosicion(const QString& p) {
    if (p != "izquierda" && p != "derecha" && p != "arriba" && p != "abajo") return;
    if (p == d_->posicion) return;
    d_->posicion = p;
    d_->guardar();
    emit cambio();
}

int RielConfig::tamano() const { return d_->tamano; }

void RielConfig::setTamano(int px) {
    const int v = std::clamp(px, kMin, kMax);
    if (v == d_->tamano) return;
    d_->tamano = v;
    d_->guardar();
    emit cambio();
}

bool RielConfig::vertical() const { return d_->posicion == "izquierda" || d_->posicion == "derecha"; }

int RielConfig::grosor() const {
    // El botón más el aire de los costados. El riel flota, así que el margen
    // exterior lo pone quien lo dibuja.
    return d_->tamano + 22;
}

bool RielConfig::expandir() const { return d_->expandir; }

void RielConfig::setExpandir(bool e) {
    if (e == d_->expandir) return;
    d_->expandir = e;
    d_->guardar();
    emit cambio();
}

int RielConfig::anchoAbierto() const { return d_->anchoAbierto; }

void RielConfig::setAnchoAbierto(int px) {
    const int v = std::clamp(px, kAnchoMin, kAnchoMax);
    if (v == d_->anchoAbierto) return;
    d_->anchoAbierto = v;
    d_->guardar();
    emit cambio();
}

bool RielConfig::separadores() const { return d_->separadores; }

void RielConfig::setSeparadores(bool s) {
    if (s == d_->separadores) return;
    d_->separadores = s;
    d_->guardar();
    emit cambio();
}

QVariantList RielConfig::grupos() const {
    QVariantList out;
    for (const Grupo& g : catalogo()) {
        QVariantList items;
        for (const Item& i : g.items) {
            if (d_->escondidas.count(i.page) > 0 || i.page == kPaginaAsistente) continue;
            items.append(QVariantMap{{"page", i.page},
                                     {"id", QString::fromUtf8(i.id)},
                                     {"icon", QString::fromUtf8(i.icon)},
                                     {"tip", QCoreApplication::translate("riel", i.tip)},
                                     {"titulo", titulo(i.tip)}});
        }
        // Un grupo sin páginas a la vista no aporta ni su separador.
        if (items.isEmpty()) continue;
        out.append(QVariantMap{{"titulo", QCoreApplication::translate("riel", g.titulo)}, {"items", items}});
    }
    return out;
}

int RielConfig::paginaDeId(const QString& id) const {
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items)
            if (id == QString::fromUtf8(i.id)) return i.page;
    return -1;
}

QString RielConfig::idDePagina(int pagina) const {
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items)
            if (i.page == pagina) return QString::fromUtf8(i.id);
    return {};
}

QString RielConfig::tituloDePagina(int pagina) const {
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items)
            if (i.page == pagina) return titulo(i.tip);
    return {};
}

QStringList RielConfig::idsDePaginas() {
    QStringList out;
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items)
            out << QString::fromUtf8(i.id);
    return out;
}

QVariantList RielConfig::paginas() const {
    QVariantList out;
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items) {
            if (i.page == kPaginaAsistente) continue; // no se esconde: es el botón fijo
            out.append(QVariantMap{{"page", i.page},
                                   {"id", QString::fromUtf8(i.id)},
                                   {"icon", QString::fromUtf8(i.icon)},
                                   {"tip", QCoreApplication::translate("riel", i.tip)},
                                   {"titulo", titulo(i.tip)},
                                   {"grupo", QCoreApplication::translate("riel", g.titulo)},
                                   {"visible", d_->escondidas.count(i.page) == 0},
                                   {"fija", i.page == kPaginaConfig}});
        }
    return out;
}

QVariantList RielConfig::todasLasPaginas() {
    QVariantList out;
    for (const Grupo& g : catalogo())
        for (const Item& i : g.items)
            out.append(QVariantMap{{"page", i.page},
                                   {"id", QString::fromUtf8(i.id)},
                                   {"icon", QString::fromUtf8(i.icon)},
                                   {"titulo", titulo(i.tip)}});
    return out;
}

int RielConfig::escondidas() const { return static_cast<int>(d_->escondidas.size()); }

bool RielConfig::visible(int pagina) const { return d_->escondidas.count(pagina) == 0; }

void RielConfig::setVisible(int pagina, bool visible) {
    if (pagina == kPaginaConfig && !visible) return; // por donde se vuelve
    const bool antes = d_->escondidas.count(pagina) == 0;
    if (antes == visible) return;
    if (visible)
        d_->escondidas.erase(pagina);
    else
        d_->escondidas.insert(pagina);
    d_->guardar();
    emit cambio();
}

void RielConfig::restaurar() {
    d_->escondidas.clear();
    d_->posicion = "izquierda";
    d_->tamano = 42;
    d_->separadores = true;
    d_->expandir = true;
    d_->anchoAbierto = 244;
    d_->guardar();
    emit cambio();
}

} // namespace ba::app
