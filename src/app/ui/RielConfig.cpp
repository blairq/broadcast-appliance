// ============================================================================
// src/app/ui/RielConfig.cpp — ver RielConfig.hpp
// ============================================================================
#include "ui/RielConfig.hpp"

#include <algorithm>
#include <set>
#include <vector>

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
    static const std::vector<Grupo> g = {
        {"Producción",
         {{0, "dashboard", "⌂", "Dashboard"},
          {1, "escenas", "▦", "Escenas y fuentes"},
          {8, "entradas", "◉", "Entradas de video (cámaras, capturadoras, red)"},
          {24, "equipos", "⌁",
           "Equipos: buscar lo que hay en la red, la máquina y el USB, y manejarlo (ONVIF, claves, PTZ, "
           "UVC)"},
          {21, "camaras", "⌖", "Cámaras PTZ: mover, zoom, presets y mejorar la imagen de cada cámara"},
          {22, "video", "⧈", "Video: tarjetas por entrada (croma, color, recorte) y salidas con nombre"},
          {9, "audio", "🎚", "Audio: patchbay de módulos LSP por fuente"},
          {25, "botonera", "▣", "Botonera: superficie de control en una tablet (QR), Stream Deck o MIDI"}}},
        {"Contenido",
         {{2, "sala", "☎", "Sala (lobby, invitados, chat)"},
          {10, "guion", "≣", "Guion del evento: bloques, cues y tiempos"},
          {27, "historico", "⌛",
           "Histórico: todos los eventos, qué pasó en cada uno, con qué configuración, qué se grabó y transmitió"},
          {11, "graficos", "◈", "Gráficos animados: zócalos, placas y tickers"},
          {12, "rotulos", "✎", "Rótulos en vivo: los textos de la escena que está al aire"},
          {15, "prompter", "▤", "Teleprompter: los discursos y el control del atril"},
          {16, "pantallas", "⧉", "Pantallas externas: televisores de la sala y qué mostrar en cada uno"},
          {17, "qr", "⊞", "Códigos QR: la biblioteca y el estudio de diseño"},
          {18, "personas", "☻", "Personas: quién es quién, para que el sistema los reconozca"},
          {26, "asamblea", "⚖",
           "Asamblea: central de conferencia (micrófonos de banca), recinto, orden del día y votación"},
          {19, "publico", "☷", "Público: las preguntas de la gente y la urna, para moderar en vivo"},
          {20, "medios", "♫", "Medios: música, videos, imágenes y presentaciones, con vista previa"}}},
        {"Publicar",
         {{3, "youtube", "▶", "YouTube (cuenta, videos, en vivo)"},
          {4, "twitch", "◆", "Twitch (canal, vivo, chat)"},
          {5, "facebook", "f", "Facebook (Páginas, vivos, videos)"}}},
        {"Sistema",
         {{7, "asistente", "✦", "Asistente: la terminal del agente que trabaja con vos"},
          {14, "agentes", "⚇", "Agentes: los roles automáticos, qué hacen y qué hicieron"},
          {13, "registro", "❑", "Registro: todo lo que pasa, de los tres procesos"},
          {23, "plugins", "⌬",
           "Plugins: programas en Python para equipos raros, tarjetas y automatizaciones"},
          {6, "configuracion", "⚙", "Configuración"}}},
    };
    return g;
}

/// El título corto de una página: la primera parte del tip, sin la aclaración.
QString titulo(const char* tip) {
    QString t = QString::fromUtf8(tip);
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
            if (d_->escondidas.count(i.page) > 0) continue;
            items.append(QVariantMap{{"page", i.page},
                                     {"id", QString::fromUtf8(i.id)},
                                     {"icon", QString::fromUtf8(i.icon)},
                                     {"tip", QString::fromUtf8(i.tip)},
                                     {"titulo", titulo(i.tip)}});
        }
        // Un grupo sin páginas a la vista no aporta ni su separador.
        if (items.isEmpty()) continue;
        out.append(QVariantMap{{"titulo", QString::fromUtf8(g.titulo)}, {"items", items}});
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
        for (const Item& i : g.items)
            out.append(QVariantMap{{"page", i.page},
                                   {"id", QString::fromUtf8(i.id)},
                                   {"icon", QString::fromUtf8(i.icon)},
                                   {"tip", QString::fromUtf8(i.tip)},
                                   {"titulo", titulo(i.tip)},
                                   {"grupo", QString::fromUtf8(g.titulo)},
                                   {"visible", d_->escondidas.count(i.page) == 0},
                                   {"fija", i.page == kPaginaConfig}});
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
