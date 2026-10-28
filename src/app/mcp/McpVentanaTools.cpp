// ============================================================================
// src/app/mcp/McpVentanaTools.cpp — las ventanas y los monitores, por MCP
//
// PARA QUE. Un operador con dos o tres monitores arma su puesto: el programa a
// pantalla completa en el de al lado, el guion en la mitad izquierda del
// principal, el registro abajo. Eso se hacia a mano, arrastrando, cada vez.
// Con esto se pide hablando —«poné el guion a la izquierda y las escenas a la
// derecha»— y ademas se puede guardar como receta y repetir igual antes de
// cada evento.
//
// POR ZONAS Y NO POR PIXELES. `zona: "izquierda"` en vez de calcular
// x/y/ancho/alto contra la geometria de cada monitor. Un modelo que tiene que
// hacer esa cuenta se equivoca —sobre todo con monitores de distinto tamaño o
// con el area util recortada por una barra del sistema— y el error se ve como
// una ventana a medio salir de la pantalla. Los pixeles siguen estando para
// cuando de verdad hagan falta.
//
// LAS VENTANAS SE ENCUENTRAN POR `objectName`, que les pone el QML
// ("ba:principal", "ba:pagina:<id>", "ba:salida:<n>"). Por titulo no: el
// titulo es para leer, cambia y se repite.
// ============================================================================

#include "arranque/AppRuntime.hpp"
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QNetworkInterface>
#include <QUrl>
#include <QPainter>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <functional>
#include <memory>

#include "mcp/McpService.hpp"
#include "net/CapturaWeb.hpp"
#include "ui/EnlacesViewModel.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"
#include "ui/RielConfig.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// Las ventanas nuestras, en un orden estable (la principal primero).
std::vector<QWindow*> ventanas() {
    std::vector<QWindow*> out;
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (w == nullptr || !w->objectName().startsWith("ba:")) continue;
        // Qt crea ventanas auxiliares (menus, tooltips) que no son de nadie.
        if (!w->isVisible()) continue;
        out.push_back(w);
    }
    std::sort(out.begin(), out.end(), [](QWindow* a, QWindow* b) {
        const bool pa = a->objectName() == "ba:principal";
        const bool pb = b->objectName() == "ba:principal";
        if (pa != pb) return pa;
        return a->objectName() < b->objectName();
    });
    return out;
}

QWindow* porId(const QString& id) {
    for (QWindow* w : ventanas())
        if (w->objectName() == "ba:" + id || w->objectName() == id) return w;
    return nullptr;
}

QString idDe(QWindow* w) {
    QString n = w->objectName();
    return n.startsWith("ba:") ? n.mid(3) : n;
}

const char* estadoDe(QWindow* w) {
    switch (w->visibility()) {
        case QWindow::FullScreen: return "pantalla-completa";
        case QWindow::Maximized: return "maximizada";
        case QWindow::Minimized: return "minimizada";
        default: return "normal";
    }
}

QScreen* monitorPorNombre(const QString& nombre) {
    if (nombre.isEmpty()) return nullptr;
    for (QScreen* s : QGuiApplication::screens())
        if (s->name().compare(nombre, Qt::CaseInsensitive) == 0) return s;
    // Tambien por numero, que es como los nombra la gente ("el 2").
    bool ok = false;
    const int n = nombre.toInt(&ok);
    const QList<QScreen*> ss = QGuiApplication::screens();
    if (ok && n >= 1 && n <= ss.size()) return ss[n - 1];
    return nullptr;
}

/// La zona `z` dentro del area UTIL de `s` (sin la barra del sistema).
/// Devuelve un rectangulo vacio si la zona no existe.
QRect zonaEn(QScreen* s, const QString& z) {
    const QRect a = s->availableGeometry();
    const int mx = a.width() / 2;
    const int my = a.height() / 2;
    const int t = a.width() / 3;
    if (z == "completa") return a;
    if (z == "izquierda") return QRect(a.x(), a.y(), mx, a.height());
    if (z == "derecha") return QRect(a.x() + mx, a.y(), a.width() - mx, a.height());
    if (z == "arriba") return QRect(a.x(), a.y(), a.width(), my);
    if (z == "abajo") return QRect(a.x(), a.y() + my, a.width(), a.height() - my);
    if (z == "sup-izq") return QRect(a.x(), a.y(), mx, my);
    if (z == "sup-der") return QRect(a.x() + mx, a.y(), a.width() - mx, my);
    if (z == "inf-izq") return QRect(a.x(), a.y() + my, mx, a.height() - my);
    if (z == "inf-der") return QRect(a.x() + mx, a.y() + my, a.width() - mx, a.height() - my);
    if (z == "tercio-izq") return QRect(a.x(), a.y(), t, a.height());
    if (z == "tercio-centro") return QRect(a.x() + t, a.y(), t, a.height());
    if (z == "tercio-der") return QRect(a.x() + 2 * t, a.y(), a.width() - 2 * t, a.height());
    if (z == "centro") {
        const int w = a.width() * 7 / 10;
        const int h = a.height() * 8 / 10;
        return QRect(a.x() + (a.width() - w) / 2, a.y() + (a.height() - h) / 2, w, h);
    }
    return {};
}

/// Pone el MARCO de `w` exactamente en `objetivo`, midiendo y corrigiendo.
///
/// NO SE MODELA AL GESTOR DE VENTANAS, SE LO MIDE. `setGeometry` ubica el area
/// de cliente, el escritorio le agrega un marco de un grosor que solo el sabe,
/// y ademas recorta contra su area util. Medido acá: pidiendo la mitad derecha
/// cae exacta, pidiendo la izquierda cae 66 px corrida, y siempre sobran 32 px
/// en vertical. Cada gestor tiene su propia mezcla de esas cosas.
///
/// Asi que se pide, se MIRA donde quedo, y se vuelve a pedir corrigiendo por
/// la diferencia. Un intento de correccion alcanza cuando el error es
/// constante —que es el caso de un marco o un recorte— y cuando el escritorio
/// directamente ignora la posicion, tampoco sirve insistir: ahi se avisa.
///
/// :param luego: recibe `true` si quedo donde se pidio (tolerancia de 4 px).
void colocarMarco(QWindow* w, const QRect& objetivo, std::function<void(bool)> luego) {
    const auto aplicar = [w](const QRect& marco) {
        const QRect cg = w->geometry();
        const QRect fg = w->frameGeometry();
        const int ml = cg.x() - fg.x();
        const int mt = cg.y() - fg.y();
        const int mr = fg.right() - cg.right();
        const int mb = fg.bottom() - cg.bottom();
        w->setGeometry(QRect(marco.x() + ml, marco.y() + mt, std::max(320, marco.width() - ml - mr),
                             std::max(200, marco.height() - mt - mb)));
    };
    const auto cerca = [](const QRect& a, const QRect& b) {
        return std::abs(a.x() - b.x()) <= 4 && std::abs(a.y() - b.y()) <= 4 &&
               std::abs(a.width() - b.width()) <= 8 && std::abs(a.height() - b.height()) <= 8;
    };
    aplicar(objetivo);
    QTimer::singleShot(150, w, [w, objetivo, luego, aplicar, cerca]() {
        const QRect real = w->frameGeometry();
        if (cerca(real, objetivo)) {
            luego(true);
            return;
        }
        // UN SOLO REINTENTO, Y NO EMPUJANDO MAS FUERTE. El desvio casi nunca
        // es un error: es un RECORTE. El escritorio no deja que una ventana se
        // meta debajo del dock de la izquierda ni de la barra de arriba, y
        // Qt informa el area del monitor SIN descontarlos —medido aca: 66 px
        // de dock y 32 de barra—. Pedir -66 para compensar no sirve: vuelve a
        // topar contra lo mismo.
        //
        // Lo que se hace es aceptar donde el escritorio la dejo empezar y
        // ajustar el TAMAÑO para que el borde de la otra punta caiga donde
        // tenia que caer. Asi «la mitad izquierda» termina ocupando la mitad
        // izquierda util, que es lo que uno queria, en vez de sobresalir por
        // la derecha justo el ancho del dock.
        const int x = std::max(objetivo.x(), real.x());
        const int y = std::max(objetivo.y(), real.y());
        aplicar(
            QRect(x, y, std::max(320, objetivo.right() - x + 1), std::max(200, objetivo.bottom() - y + 1)));
        QTimer::singleShot(150, w, [w, objetivo, luego]() {
            // Ahora lo que importa es que llegue hasta el borde correcto, no
            // que empiece en el pixel pedido: la esquina la puso el dock.
            const QRect q = w->frameGeometry();
            luego(std::abs(q.right() - objetivo.right()) <= 8 &&
                  std::abs(q.bottom() - objetivo.bottom()) <= 8 && q.x() >= objetivo.x() - 4 &&
                  q.y() >= objetivo.y() - 4);
        });
    });
}

const char* kZonas = "completa, izquierda, derecha, arriba, abajo, sup-izq, sup-der, inf-izq, inf-der, "
                     "tercio-izq, tercio-centro, tercio-der, centro";

json geom(const QRect& r) {
    return json{{"x", r.x()}, {"y", r.y()}, {"ancho", r.width()}, {"alto", r.height()}};
}

/// Si en este escritorio un programa puede decir DONDE va su ventana.
///
/// En Wayland no: ubicar las ventanas es del compositor a proposito, y pedirlo
/// no falla —simplemente no pasa nada—. Eso es lo peor que podia hacer esta
/// herramienta: decir «listo, a la izquierda» y dejar la ventana donde estaba.
/// Asi que se pregunta antes y se avisa.
bool ubicaElPrograma() { return !QGuiApplication::platformName().contains("wayland"); }

// Marcado para traducir: se traduce al usarlo (sobreWayland).
const char* kSobreWayland = QT_TRANSLATE_NOOP(
    "mcp",
    "Este escritorio es Wayland: ubicar las ventanas es del compositor, no del programa. El TAMAÑO se "
    "aplica; la POSICION no. No es un error de la app y no se puede forzar desde adentro. Ojo, tampoco "
    "alcanza con arrancar sobre XWayland (QT_QPA_PLATFORM=xcb): se probo, y GNOME ignora igual la "
    "posicion pedida y coloca las ventanas a su criterio. Con un escritorio que si lo respete (KDE, "
    "XFCE, un gestor de mosaico, o X11 a secas) estas herramientas funcionan enteras. Mientras tanto "
    "sirven para LEER donde esta todo, para el tamaño, y para pantalla completa en un monitor, que es "
    "lo que mas se usa.");
std::string sobreWayland() { return ba::i18n::traducir("mcp", kSobreWayland); }

/// «(x,y) anchoxalto», para decir dónde quedó una ventana y dónde se pidió.
std::string posTam(const QRect& q) {
    return "(" + std::to_string(q.x()) + "," + std::to_string(q.y()) + ") " + std::to_string(q.width()) + "x" +
           std::to_string(q.height());
}

/// Guarda una captura para un manual: `archivo` relativo a <Datos>/Manuales (sin «..»,
/// .jpg o .png), achicada a `ancho` si es más grande, con `recuadros` marcados
/// ({x, y, w, h, color: rojo|amarillo} en fracciones). Devuelve la ruta, o "" y `error`.
QString guardarEnManuales(const QImage& img, const json& a, int anchoDeFabrica, std::string& error) {
    const QString q = QDir::cleanPath(QString::fromStdString(a.value("archivo", std::string())));
    const QString ext = q.section('.', -1).toLower();
    if (q.isEmpty() || q.startsWith("..") || QDir::isAbsolutePath(q) || (ext != "jpg" && ext != "jpeg" && ext != "png")) {
        error = ba::i18n::traducir("mcp", "`archivo` va relativo a la carpeta Manuales, sin «..», y termina en .jpg o .png.");
        return {};
    }
    const QDir raiz(QString::fromStdU16String((ba::platform::dataRoot() / "Manuales").u16string()));
    const QString destino = raiz.absoluteFilePath(q);
    const int ancho = std::clamp(a.value("ancho", anchoDeFabrica), 320, 3840);
    QImage guardar = img.width() > ancho ? img.scaledToWidth(ancho, Qt::SmoothTransformation) : img;
    if (a.contains("recuadros") && a["recuadros"].is_array()) {
        guardar = guardar.convertToFormat(QImage::Format_RGB32);
        QPainter pintor(&guardar);
        for (const auto& b : a["recuadros"]) {
            const QColor color = b.value("color", std::string("rojo")) == "amarillo" ? QColor(255, 210, 0, 242) : QColor(230, 20, 20, 242);
            pintor.setPen(QPen(color, 3));
            pintor.drawRect(QRectF(b.value("x", 0.0) * guardar.width(), b.value("y", 0.0) * guardar.height(),
                                   b.value("w", 0.0) * guardar.width(), b.value("h", 0.0) * guardar.height()));
        }
    }
    QDir().mkpath(QFileInfo(destino).absolutePath());
    if (!guardar.save(destino, nullptr, ext == "png" ? -1 : 90)) {
        error = ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo guardar «%1»."), {destino.toStdString()});
        return {};
    }
    return destino;
}

json esquemaRecuadros() {
    return {{"type", "array"},
            {"items",
             {{"type", "object"},
              {"properties",
               {{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"w", {{"type", "number"}}},
                {"h", {{"type", "number"}}}, {"color", {{"type", "string"}, {"enum", {"rojo", "amarillo"}}}}}}}}};
}

} // namespace

void McpService::registerVentanaTools() {
    core_.addCategory("ventanas", ba::i18n::traducir("mcp", "Ventanas y monitores"),
                      ba::i18n::traducir("mcp", "Dónde está cada ventana y cómo acomodarlas: mover, redimensionar, mandar a otro "
                      "monitor, pantalla completa, cerrar. Para armar el puesto del operador en varias "
                      "pantallas."));

    {
        Tool t;
        t.name = "ventana_listar";
        t.title = ba::i18n::traducir("mcp", "Que ventanas hay abiertas y donde");
        t.category = "ventanas";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "El inventario de ventanas abiertas, con su id, "
            "que tienen adentro, su tamaño y posicion, en que "
            "monitor estan y como estan (normal, maximizada, pantalla completa). Tambien los monitores.");
        t.details =
            ba::i18n::traducir("mcp", "Los `id` son los que piden `ventana_mover` y `ventana_cerrar`:\n"
            "  - `principal`: la ventana grande, la que siempre esta.\n"
            "  - `pagina:<id>`: una pestaña sacada a su ventana (ver ui_page_window).\n"
            "  - `salida:<n>`: una ventana de video (ver output_window).\n"
            "  - `panel:<id>`: un panel o una tarjeta sacados a su ventana (ver ui_panel_window).\n"
            "  - `monitor:<id>`: lo que esta FIJO en un monitor (Salidas → Pantallas; ver monitor_listar).\n\n"
            "`monitores[].util` es el area SIN la barra del sistema: es la que se usa para las zonas, y "
            "por eso puede ser mas chica que `total`.");
        t.run = [](const json&, Done done) {
            json vs = json::array();
            for (QWindow* w : ventanas()) {
                // `geometria` es lo que ocupa DE VERDAD en el escritorio (con
                // la barra de titulo y los bordes): es lo que se ve y lo que
                // hay que comparar contra una zona. El area de cliente va
                // aparte, para cuando haga falta.
                vs.push_back(
                    json{{"id", idDe(w).toStdString()},
                         {"titulo", w->title().toStdString()},
                         {"monitor", w->screen() != nullptr ? w->screen()->name().toStdString() : ""},
                         {"estado", estadoDe(w)},
                         {"geometria", geom(w->frameGeometry())},
                         {"interior", geom(w->geometry())}});
            }
            json ms = json::array();
            for (QScreen* s : QGuiApplication::screens()) {
                ms.push_back(json{{"nombre", s->name().toStdString()},
                                  {"principal", s == QGuiApplication::primaryScreen()},
                                  {"total", geom(s->geometry())},
                                  {"util", geom(s->availableGeometry())}});
            }
            json r{{"ventanas", vs}, {"monitores", ms}, {"zonas", kZonas}};
            r["ubicarPorSoftware"] = ubicaElPrograma();
            if (!ubicaElPrograma()) {
                r["ojo"] = sobreWayland();
                // `monitor` sale de lo que dice Qt, y en Wayland Qt informa la
                // pantalla que se le PIDIO aunque la ventana no se haya movido.
                // Decirlo es la diferencia entre un dato y una suposicion.
                r["ojoMonitor"] = ba::i18n::traducir(
                    "mcp", "En este escritorio `monitor` es lo que cree Qt, no necesariamente donde se ve la "
                           "ventana: pedir un cambio de monitor no falla y tampoco la mueve. Para mandarla de "
                           "verdad, pantalla completa (`estado: \"pantalla-completa\"` con `monitor`).");
            }
            done(ToolResult::ok(r.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "ventana_mover";
        t.title = ba::i18n::traducir("mcp", "Mover, redimensionar o mandar a otro monitor");
        t.category = "ventanas";
        t.description = ba::i18n::traducir("mcp", "Manda una ventana de la app a otro MONITOR de esta computadora, o a una zona de la "
                        "pantalla. Es para acomodar el puesto de trabajo. Acomoda una ventana: a otro "
                        "monitor, a una zona de ese monitor, a un tamaño y posicion "
                        "exactos, o a pantalla completa. Los ids salen de ventana_listar.");
        t.details = ba::i18n::llenar(
            ba::i18n::traducir("mcp", "USA `zona` SIEMPRE QUE PUEDAS, no x/y/ancho/alto. La zona se calcula contra el area util "
            "del monitor (sin la barra del sistema) y sale bien con monitores de distinto tamaño; "
            "calcular los pixeles a mano termina en ventanas a medio salir de la pantalla.\n\n"
            "Zonas: %1.\n\n"
            "`monitor` es el nombre que da ventana_listar (\"DP-1\", \"HDMI-1\") o su numero (1, 2...). "
            "Sin `monitor`, la zona se aplica en el monitor donde ya esta.\n\n"
            "`estado`: normal, maximizada, pantalla-completa, minimizada. Pantalla completa ignora la "
            "zona (ocupa todo), asi que para volver a acomodarla hay que ponerla en normal primero.\n\n"
            "Para armar un puesto de varios monitores, una llamada por ventana. `ventana_listar` "
            "despues confirma como quedo."),
            {kZonas});
        t.inputSchema = {
            {"type", "object"},
            {"required", {"ventana"}},
            {"properties",
             {{"ventana",
               {{"type", "string"},
                {"description", ba::i18n::traducir("mcp", "id de ventana_listar (p. ej. \"principal\", \"pagina:guion\")")}}},
              {"monitor", {{"type", json::array({"string", "integer"})}, {"description", ba::i18n::traducir("mcp", "nombre o numero del monitor")}}},
              {"zona", {{"type", "string"}, {"description", ba::i18n::llenar(ba::i18n::traducir("mcp", "una de: %1"), {kZonas})}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"ancho", {{"type", "integer"}}},
              {"alto", {{"type", "integer"}}},
              {"estado",
               {{"type", "string"}, {"enum", {"normal", "maximizada", "pantalla-completa", "minimizada"}}}},
              {"alFrente", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "traerla adelante de las demas")}}}}}};
        t.examples = {R"({"ventana": "principal", "monitor": "DP-1", "zona": "completa"})",
                      R"({"ventana": "pagina:guion", "zona": "izquierda"})",
                      R"({"ventana": "salida:1", "monitor": 2, "estado": "pantalla-completa"})"};
        t.run = [](const json& a, Done done) {
            const QString id = QString::fromStdString(a["ventana"].get<std::string>());
            QWindow* w = porId(id);
            if (w == nullptr)
                return done(ToolResult::error(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "No hay una ventana \"%1\" abierta. ventana_listar dice cuales hay."),
                    {id.toStdString()})));
            std::string hecho;

            // 1) El monitor primero: la zona se calcula contra el que va a quedar.
            QScreen* destino = w->screen();
            if (a.contains("monitor")) {
                const QString n = a["monitor"].is_string()
                                      ? QString::fromStdString(a["monitor"].get<std::string>())
                                      : QString::number(a["monitor"].get<int>());
                QScreen* s = monitorPorNombre(n);
                if (s == nullptr)
                    return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "No conozco el monitor \"%1\". ventana_listar los lista."),
                        {n.toStdString()})));
                destino = s;
            }

            // 2) Salir de pantalla completa o maximizada antes de mover: si no,
            //    la geometria se pide y el gestor de ventanas la ignora.
            const bool acomoda =
                a.contains("zona") || a.contains("x") || a.contains("y") || a.contains("ancho") ||
                a.contains("alto") ||
                (a.contains("monitor") && a.value("estado", std::string()) != "pantalla-completa");
            if (acomoda && w->visibility() != QWindow::Windowed) w->setVisibility(QWindow::Windowed);

            QRect r = w->geometry();
            if (a.contains("zona")) {
                const QString z = QString::fromStdString(a["zona"].get<std::string>());
                const QRect zr = zonaEn(destino, z);
                if (zr.isEmpty())
                    return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "No conozco la zona \"%1\". Las que hay: %2."), {z.toStdString(), kZonas})));
                r = zr;
                hecho += ba::i18n::llenar(ba::i18n::traducir("mcp", " zona %1"), {z.toStdString()});
            } else if (destino != w->screen()) {
                // Sin zona pero con monitor: se lleva el mismo tamaño, pegada a
                // la esquina util, para que no quede partida entre los dos.
                const QRect u = destino->availableGeometry();
                r.moveTo(u.x() + std::min(r.x() - w->screen()->availableGeometry().x(),
                                          std::max(0, u.width() - r.width())),
                         u.y() + std::min(r.y() - w->screen()->availableGeometry().y(),
                                          std::max(0, u.height() - r.height())));
            }
            if (a.contains("ancho")) r.setWidth(std::max(320, a["ancho"].get<int>()));
            if (a.contains("alto")) r.setHeight(std::max(200, a["alto"].get<int>()));
            if (a.contains("x")) r.moveLeft(a["x"].get<int>());
            if (a.contains("y")) r.moveTop(a["y"].get<int>());

            if (destino != w->screen()) {
                w->setScreen(destino);
                hecho += ba::i18n::llenar(ba::i18n::traducir("mcp", " a %1"), {destino->name().toStdString()});
            }

            if (acomoda && !a.contains("estado")) {
                if (hecho.empty()) hecho = ba::i18n::traducir("mcp", " movida");
                const std::string titulo2 = w->title().toStdString();
                const std::string h = hecho;
                const QRect obj = r;
                // Se pide, se MIDE donde quedo y se corrige (ver colocarMarco).
                // Y se dice la verdad: una herramienta que contesta «listo» con
                // la ventana en otro lado es peor que una que no puede.
                colocarMarco(w, r, [w, done, titulo2, h, obj](bool ok) {
                    if (ok)
                        return done(ToolResult::ok(ba::i18n::llenar(
                            ba::i18n::traducir("mcp", "«%1»:%2. ventana_listar confirma como quedo."), {titulo2, h})));
                    const QRect q = w->frameGeometry();
                    done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "«%1»:%2, PERO quedo en %3 y se habia pedido %4. %5"),
                        {titulo2, h, posTam(q), posTam(obj),
                         ubicaElPrograma() ? ba::i18n::traducir("mcp", "El escritorio la ubico por su cuenta.")
                                           : sobreWayland()})));
                });
                return;
            }
            if (acomoda) {
                // Con `estado` explicito manda el estado: la geometria se
                // aplica y no se verifica (pantalla completa la pisa igual).
                const QRect cg = w->geometry();
                const QRect fg = w->frameGeometry();
                const int ml = cg.x() - fg.x(), mt = cg.y() - fg.y();
                const int mr = fg.right() - cg.right(), mb = fg.bottom() - cg.bottom();
                w->setGeometry(QRect(r.x() + ml, r.y() + mt, std::max(320, r.width() - ml - mr),
                                     std::max(200, r.height() - mt - mb)));
                if (hecho.empty()) hecho = ba::i18n::traducir("mcp", " movida");
            }

            if (a.contains("estado")) {
                const std::string e = a["estado"].get<std::string>();
                // Las ventanas sueltas (VentanaSuelta: video, páginas, paneles) tienen su
                // estado en QML y en el modelo que las recuerda: por su función, así
                // no quedan desparejos. La principal y los monitores no la tienen.
                if (w->metaObject()->indexOfMethod("ponerEstado(QVariant)") >= 0 &&
                    QMetaObject::invokeMethod(w, "ponerEstado", Q_ARG(QVariant, QVariant(QString::fromStdString(e))))) {
                    done(ToolResult::ok("«" + w->title().toStdString() + "»:" + hecho + " (" + e + ")."));
                    return;
                }
                if (e == "pantalla-completa") {
                    // Salir y volver a entrar (ver la pausa de mas arriba): si
                    // ya estaba en pantalla completa en OTRO monitor, pedirla
                    // de nuevo sin salir no mueve nada.
                    w->setVisibility(QWindow::Windowed);
                    const std::string titulo2 = w->title().toStdString();
                    QTimer::singleShot(180, w, [w, done, titulo2, hecho]() {
                        w->setVisibility(QWindow::FullScreen);
                        done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1»:%2 (pantalla completa)."),
                                                             {titulo2, hecho})));
                    });
                    return;
                }
                w->setVisibility(e == "maximizada"   ? QWindow::Maximized
                                 : e == "minimizada" ? QWindow::Minimized
                                                     : QWindow::Windowed);
                hecho += " (" + e + ")";
            }
            if (a.value("alFrente", false)) {
                w->raise();
                w->requestActivate();
            }

            // SE VERIFICA LO QUE QUEDO, y DESPUES de darle tiempo al escritorio.
            //
            // Pedir una geometria no falla nunca: el gestor de ventanas hace lo
            // que quiere y contesta cuando quiere. Preguntando al toque, Qt
            // devuelve lo que uno acaba de pedir y la herramienta contesta
            // «listo» aunque la ventana termine en otro lado —que es justo lo
            // que pasa en este escritorio—. Se espera un momento y se mira de
            // verdad.
            const std::string titulo = w->title().toStdString();
            const QRect pedido = r;
            const bool verifica = acomoda;
            QTimer::singleShot(250, w, [w, done, titulo, hecho, pedido, verifica]() {
                std::string aviso;
                if (verifica) {
                    const QRect q = w->frameGeometry();
                    const bool ok = std::abs(q.x() - pedido.x()) <= 8 && std::abs(q.y() - pedido.y()) <= 8;
                    if (!ok) {
                        aviso = ba::i18n::llenar(
                            ba::i18n::traducir("mcp", " PERO quedo en %1, y se habia pedido %2. %3"),
                            {posTam(q), posTam(pedido),
                             ubicaElPrograma()
                                 ? ba::i18n::traducir("mcp", "La ubico el escritorio por su cuenta. Hay gestores de "
                                                             "ventanas —GNOME entre ellos, tambien sobre X11— que "
                                                             "no respetan la posicion que pide un programa.")
                                 : sobreWayland()});
                    }
                }
                done(aviso.empty()
                         ? ToolResult::ok(
                               hecho.empty()
                                   ? ba::i18n::llenar(ba::i18n::traducir(
                                                          "mcp", "«%1» sin cambios. ventana_listar confirma como quedo."),
                                                      {titulo})
                                   : ba::i18n::llenar(
                                         ba::i18n::traducir("mcp", "«%1»:%2. ventana_listar confirma como quedo."),
                                         {titulo, hecho}))
                         : ToolResult::error("«" + titulo + "»:" + hecho + "," + aviso));
            });
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "ventanas_ordenar";
        t.title = ba::i18n::traducir("mcp", "Acomodar todas las ventanas en los monitores");
        t.category = "ventanas";
        t.description =
            ba::i18n::traducir("mcp", "Reparte las ventanas abiertas entre los monitores para que no queden una encima de otra. "
            "La principal se queda donde esta; las sueltas se distribuyen.");
        t.details =
            ba::i18n::traducir("mcp", "QUE HACE SEGUN EL ESCRITORIO, porque no todos dejan lo mismo:\n"
            "  - Si se pueden ubicar ventanas: reparte por zonas (una sola ocupa el monitor entero, dos "
            "se parten por la mitad, tres en tercios, cuatro en cuadrantes).\n"
            "  - Si no (GNOME/Wayland): una a PANTALLA COMPLETA por monitor libre, que es lo unico que "
            "el compositor respeta. Las que sobran quedan donde estan y se dice cuales.\n\n"
            "`dejarLibre` sirve para no tocar el monitor donde esta lo que se esta mirando.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"dejarLibre",
               {{"type", "string"},
                {"description", ba::i18n::traducir("mcp", "nombre de un monitor que no hay que usar (p. ej. el del operador)")}}}}}};
        t.run = [](const json& a, Done done) {
            const QString libre = QString::fromStdString(a.value("dejarLibre", std::string()));
            // Los monitores con algo FIJO (Salidas → Pantallas, «monitor:<id>») no
            // se usan ni se tocan: ya tienen dueño y se recuerdan solos.
            QList<QScreen*> fijos;
            for (QWindow* w : ventanas())
                if (idDe(w).startsWith("monitor:") && w->screen() != nullptr) fijos.push_back(w->screen());
            QList<QScreen*> pantallas;
            for (QScreen* s : QGuiApplication::screens())
                if (s->name().compare(libre, Qt::CaseInsensitive) != 0 && !fijos.contains(s)) pantallas.push_back(s);
            if (pantallas.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No queda ningun monitor para usar.")));

            // La principal no se toca: es donde el operador esta trabajando, y
            // moverle el piso mientras acomoda lo demas es lo contrario de
            // ordenar.
            std::vector<QWindow*> sueltas;
            for (QWindow* w : ventanas())
                if (idDe(w) != "principal" && !idDe(w).startsWith("monitor:")) sueltas.push_back(w);
            if (sueltas.empty())
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Solo esta la ventana principal: no hay nada que acomodar.")));

            // SE INTENTA OCUPAR TODA LA PANTALLA, SIEMPRE. Repartir por
            // zonas es lo que se pidio: dos ventanas media pantalla cada una,
            // tres en tercios, cuatro en cuadrantes. Antes esto ni se probaba
            // en un escritorio «que no deja»: se iba derecho a una sola a
            // pantalla completa por monitor, que es resignar de entrada.
            //
            // Se prueba, se MIDE, y recien si de verdad no quedaron donde se
            // pidieron se cae a pantalla completa —que es lo unico que ese
            // escritorio respeta— y se dice cuales no entraron.
            static const char* kPorCantidad[5][4] = {
                {},
                {"completa"},
                {"izquierda", "derecha"},
                {"tercio-izq", "tercio-centro", "tercio-der"},
                {"sup-izq", "sup-der", "inf-izq", "inf-der"},
            };
            const int nPant = pantallas.size();
            const int porPantalla = std::min<int>(4, static_cast<int>((sueltas.size() + nPant - 1) / nPant));
            struct Puesto {
                QWindow* w;
                QScreen* p;
                std::string zona;
            };
            auto puestos = std::make_shared<std::vector<Puesto>>();
            for (std::size_t i = 0; i < sueltas.size(); ++i) {
                QScreen* p = pantallas[static_cast<int>(i / static_cast<std::size_t>(porPantalla)) % nPant];
                puestos->push_back(Puesto{
                    sueltas[i], p, kPorCantidad[porPantalla][i % static_cast<std::size_t>(porPantalla)]});
            }

            auto pendientes = std::make_shared<int>(static_cast<int>(puestos->size()));
            auto fallaron = std::make_shared<std::vector<std::string>>();
            auto listas = std::make_shared<std::vector<std::string>>();
            for (const Puesto& q : *puestos) {
                QWindow* w = q.w;
                const std::string zona = q.zona;
                const std::string etiqueta =
                    idDe(w).toStdString() + " -> " + q.p->name().toStdString() + " " + zona;
                w->setVisibility(QWindow::Windowed);
                w->setScreen(q.p);
                colocarMarco(w, zonaEn(q.p, QString::fromStdString(zona)),
                             [etiqueta, pendientes, fallaron, listas, done, puestos](bool ok) {
                                 (ok ? listas : fallaron)->push_back(etiqueta);
                                 if (--*pendientes > 0) return;

                                 const auto juntar = [](const std::vector<std::string>& v) {
                                     std::string o;
                                     for (const std::string& x : v)
                                         o += (o.empty() ? "" : ", ") + x;
                                     return o;
                                 };
                                 if (fallaron->empty())
                                     return done(ToolResult::ok(ba::i18n::llenar(
                                         ba::i18n::traducir("mcp", "Acomodadas: %1. ventana_listar confirma como quedo."),
                                         {juntar(*listas)})));
                                 // No se pudieron repartir: se hace lo unico
                                 // que queda, una por monitor a pantalla
                                 // completa, y se nombra lo que sobro.
                                 std::string full;
                                 std::string sobra;
                                 std::vector<QScreen*> usados;
                                 for (const Puesto& r2 : *puestos) {
                                     if (std::find(usados.begin(), usados.end(), r2.p) != usados.end()) {
                                         sobra += (sobra.empty() ? "" : ", ") + idDe(r2.w).toStdString();
                                         continue;
                                     }
                                     usados.push_back(r2.p);
                                     QWindow* v = r2.w;
                                     v->setVisibility(QWindow::Windowed);
                                     v->setScreen(r2.p);
                                     QTimer::singleShot(180, v,
                                                        [v]() { v->setVisibility(QWindow::FullScreen); });
                                     full += (full.empty() ? "" : ", ") + idDe(v).toStdString() + " -> " +
                                             r2.p->name().toStdString();
                                 }
                                 std::string msg = ba::i18n::llenar(
                                     ba::i18n::traducir("mcp", "Este escritorio no dejo repartirlas por zonas (se probo y quedaron "
                                                               "en otro lado), asi que va una por monitor a PANTALLA COMPLETA: %1."),
                                     {full});
                                 if (!sobra.empty())
                                     msg += ba::i18n::llenar(
                                         ba::i18n::traducir("mcp", " NO entraron (%1): sin poder ubicar ventanas no hay forma de "
                                                                   "poner dos en un mismo monitor. Quedaron donde estaban."),
                                         {sobra});
                                 done(ToolResult::ok(msg));
                             });
            }
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitor_identificar";
        t.title = ba::i18n::traducir("mcp", "Mostrar en cada monitor cual es");
        t.category = "ventanas";
        t.description =
            ba::i18n::traducir("mcp", "Pone unos segundos un cartel grande en CADA monitor (o solo en `monitor`) con su numero y su "
            "nombre, los mismos de la pagina Pantallas. Para saber cual es \"DP-1\" sin mandar una ventana a "
            "ver adonde va.");
        t.details = ba::i18n::traducir("mcp", "Usalo antes de acomodar el puesto: los nombres de los monitores no le dicen nada a "
                    "nadie hasta que se los ve puestos.\n\n"
                    "Tambien sirve de prueba: aparece con PANTALLA COMPLETA en cada monitor, que es lo "
                    "unico que un escritorio como GNOME respeta. Si los carteles salen donde tienen que "
                    "salir, mandar una ventana a pantalla completa a un monitor tambien va a andar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"monitor", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "solo este: su numero, su nombre o su id (vacio = todos)")}}}}}};
        t.run = [this](const json& a, Done done) {
            const QString cual = QString::fromStdString(a.value("monitor", std::string()));
            const QString err = vm_.identificarPantallas(cual);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(cual.isEmpty() ? ba::i18n::traducir("mcp", "Cartel puesto en cada monitor unos segundos.")
                                               : ba::i18n::traducir("mcp", "Cartel puesto en ese monitor unos segundos.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ventana_captura";
        t.title = ba::i18n::traducir("mcp", "Ver la interfaz");
        t.category = "interfaz";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Una foto de la INTERFAZ del aparato, no de lo que sale al aire (eso es "
            "`foto_programa`). Es la unica forma de mirar una pagina: si algo se ve mal —una "
            "tarjeta encima de otra, un texto cortado, un boton que no aparece— esto lo muestra; "
            "describirlo de memoria no. Por defecto la ventana principal; `ventana_listar` da las "
            "otras (los paneles sueltos son `panel:<id>`).\n\n"
            "Para mirar una pagina puntual, primero `ui_show_page` y despues esto.\n\n"
            "`archivo` la guarda además (para un manual): una ruta relativa dentro de la carpeta "
            "Manuales de los datos (p. ej. «es/capturas/01-dashboard.jpg»), en `.jpg` o `.png`, al `ancho` "
            "pedido. `recuadros` marca ahí lo importante: [{x, y, w, h, color: rojo|amarillo}] en fracciones "
            "de la imagen (0..1); el original sin marcas se guarda aparte si se pide con otro `archivo`.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"ventana",
                            {{"type", "string"},
                             {"default", "principal"},
                             {"description", ba::i18n::traducir("mcp", "id de ventana_listar; `principal` si no se dice")}}},
                           {"ancho",
                            {{"type", "integer"},
                             {"minimum", 320},
                             {"maximum", 3840},
                             {"description", ba::i18n::traducir("mcp", "a cuanto achicarla. 1280 si no se dice")}}},
                           {"archivo",
                            {{"type", "string"},
                             {"description", ba::i18n::traducir("mcp", "guardarla en Manuales/<archivo> (.jpg o .png)")}}},
                           {"recuadros", esquemaRecuadros()}}}};
        t.examples = {R"({})", R"({"ventana": "panel:carrusel"})",
                      R"({"ancho": 1600, "archivo": "es/capturas/01-dashboard.jpg"})",
                      R"({"ancho": 1600, "archivo": "es/capturas/01b-dashboard-anotada.jpg", "recuadros": [{"x": 0.8, "y": 0.02, "w": 0.15, "h": 0.06, "color": "rojo"}]})"};
        t.run = [this](const json& a, Done done) {
            const QString id = QString::fromStdString(a.value("ventana", std::string("principal")));
            QWindow* w = porId(id);
            if (w == nullptr) {
                done(ToolResult::error(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "No hay ninguna ventana «%1». `ventana_listar` dice cuales hay."),
                    {id.toStdString()})));
                return;
            }
            auto* qw = qobject_cast<QQuickWindow*>(w);
            if (qw == nullptr) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "Esa ventana no se puede fotografiar.")));
                return;
            }
            // grabWindow SOLO corre en el hilo de la interfaz. Si ya estamos en
            // el, llamarla derecho: una conexion bloqueante a nuestro propio
            // hilo es un abrazo mortal.
            QImage img;
            const auto sacar = [qw, &img]() { img = qw->grabWindow(); };
            if (QThread::currentThread() == qw->thread()) {
                sacar();
            } else {
                QMetaObject::invokeMethod(qw, sacar, Qt::BlockingQueuedConnection);
            }
            if (img.isNull()) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo sacar la foto de la ventana.")));
                return;
            }
            ToolResult r;
            r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Interfaz de «%1»."), {id.toStdString()}));
            // Guardarla para un manual: solo dentro de <Datos>/Manuales (nada de rutas sueltas).
            if (a.contains("archivo")) {
                std::string err;
                const QString destino = guardarEnManuales(img, a, 1600, err);
                if (destino.isEmpty()) return done(ToolResult::error(err));
                r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Guardada en %1."), {destino.toStdString()}));
                r.structured = json{{"archivo", destino.toStdString()}};
            }
            addImage(r, img, a, 1280, core_, rt_.dataRoot());
            done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "web_captura";
        t.title = ba::i18n::traducir("mcp", "Ver una página web del aparato");
        t.category = "interfaz";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Una foto de una PÁGINA WEB del aparato tal como la ve quien la abre: la web del evento, la de "
                        "los invitados, el público, el teleprompter, los rótulos, la botonera, la sesión remota, la consola de la "
                        "presidencia, el tablero… (los enlaces salen de `enlaces_listar`: `local` o `remoto`). Con un Chrome sin "
                        "ventana que viene en la instalación. `ancho` × `alto`: 430×900 para cómo se ve en un celular, 1440×900 en "
                        "una computadora, 1024×1366 en una tablet. `espera_ms` deja correr la página antes de la foto (2500). "
                        "`archivo` y `recuadros`, como en ventana_captura (para un manual). Solo direcciones del propio aparato.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"url"}},
                         {"properties",
                          {{"url", {{"type", "string"}}},
                           {"ancho", {{"type", "integer"}, {"minimum", 320}, {"maximum", 3840}}},
                           {"alto", {{"type", "integer"}, {"minimum", 320}, {"maximum", 4000}}},
                           {"espera_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20000}}},
                           {"archivo", {{"type", "string"}}},
                           {"recuadros", esquemaRecuadros()}}}};
        t.examples = {R"({"url": "http://127.0.0.1:8787/prompter/", "ancho": 430, "alto": 900})",
                      R"({"url": "<local de enlaces_listar>", "ancho": 1440, "alto": 900, "archivo": "es/capturas/web-01-evento.png"})"};
        StudioViewModel* vmp = &vm_;
        t.run = [this, vmp](const json& a, Done done) {
            const QUrl url(QString::fromStdString(a.value("url", std::string())));
            if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "`url` tiene que ser http:// o https://.")));
            // Solo las del aparato: esta máquina (sus direcciones) o las de sus servicios (Servicios).
            QStringList hosts{"127.0.0.1", "localhost", "::1"};
            for (const QHostAddress& h : QNetworkInterface::allAddresses()) hosts << h.toString();
            if (EnlacesViewModel* e = vmp->enlacesVm())
                for (const QVariant& v : e->servicios())
                    for (const char* k : {"local", "remoto"})
                        if (const QUrl u(v.toMap().value(k).toString()); u.isValid() && !u.host().isEmpty()) hosts << u.host();
            if (!hosts.contains(url.host(), Qt::CaseInsensitive))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Solo páginas del propio aparato (las de enlaces_listar o de esta "
                                                                        "máquina): esto no es un navegador para cualquier sitio.")));
            if (const QString e = CapturaWeb::disponible(); !e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            CapturaWeb::Pedido p{url.toString(), a.value("ancho", 1440), a.value("alto", 900), a.value("espera_ms", 2500)};
            // QProcess vive en el hilo de la interfaz (tiene su bucle de eventos).
            QMetaObject::invokeMethod(qApp, [this, p, a, done]() {
                auto* c = new CapturaWeb(qApp);
                c->capturar(p, [this, c, a, done, p](const QImage& img, const QString& err) {
                    c->deleteLater();
                    if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                    ToolResult r;
                    r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Página %1 (%2x%3)."),
                                            {p.url.toStdString(), std::to_string(img.width()), std::to_string(img.height())}));
                    if (a.contains("archivo")) {
                        std::string e;
                        const QString destino = guardarEnManuales(img, a, img.width(), e);
                        if (destino.isEmpty()) return done(ToolResult::error(e));
                        r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Guardada en %1."), {destino.toStdString()}));
                        r.structured = json{{"archivo", destino.toStdString()}};
                    }
                    addImage(r, img, a, 1280, core_, rt_.dataRoot());
                    done(r);
                });
            });
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "ventana_cerrar";
        t.title = ba::i18n::traducir("mcp", "Cerrar una ventana");
        t.category = "ventanas";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Cierra una ventana suelta (una pestaña sacada o una salida de video).");
        t.details = ba::i18n::traducir("mcp", "La ventana PRINCIPAL no se cierra por aca: cerrarla es cerrar la app, y eso se pide "
                    "con `app_quit`, que avisa si hay algo al aire.\n\n"
                    "Cerrar una pestaña sacada no pierde nada: la pagina sigue en la ventana principal.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"ventana"}},
                         {"properties", {{"ventana", {{"type", "string"}}}}}};
        t.run = [](const json& a, Done done) {
            const QString id = QString::fromStdString(a["ventana"].get<std::string>());
            if (id == "principal" || id == "ba:principal")
                return done(ToolResult::error(ba::i18n::traducir(
                    "mcp", "La ventana principal no se cierra por aca: eso es cerrar la app (app_quit).")));
            QWindow* w = porId(id);
            if (w == nullptr)
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una ventana \"%1\" abierta."),
                                                               {id.toStdString()})));
            const std::string titulo = w->title().toStdString();
            w->close();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» cerrada."), {titulo})));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
