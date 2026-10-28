// test_terminal.cpp — la terminal de la pestaña Asistente (Pty + libvterm)
//
//   1. Un programa real en la pseudo-terminal: su salida (con colores ANSI)
//      llega a la grilla con el texto y el color correctos; termina con su codigo.
//   2. El programa ve el tamaño de la terminal (stty size) y un cambio de tamaño.
//   3. Entrada: lo que se "tipea" y lo que se pega le llega al programa (cat).
//   4. Historial: lo que sale por arriba queda en el scrollback y se puede leer.
//   5. Caracteres anchos y acentos (UTF-8).
//   6. Una pantalla REAL de Claude Code, byte por byte: que lo vacio quede
//      vacio (ver el bloque del final: es una regresion con nombre y apellido).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QThread>

#include <cstdio>
#include <functional>

#include "term/TerminalSession.hpp"

using ba::app::term::Cell;
using ba::app::term::Pty;
using ba::app::term::TerminalSession;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", msg, __LINE__);                                            \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

bool waitFor(const std::function<bool()>& ok, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!ok() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return ok();
}

QString screenText(const TerminalSession& s) { return s.text(0, 0, s.rows() - 1, s.cols() - 1, 0); }

Pty::Options sh(const QString& script) {
    Pty::Options o;
    o.program = "/bin/sh";
    o.args = {"-c", script};
    return o;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    std::printf("== test_terminal ==\n");

    // --- 1) salida con colores y codigo de salida ---------------------------------------------
    {
        TerminalSession s;
        s.resize(80, 24);
        int code = -1;
        QObject::connect(&s, &TerminalSession::finished, [&](int c) { code = c; });
        CHECK(s.start(sh("printf '\\033[31mHOLA\\033[0m mundo\\n'; exit 3")), "arranca el programa");
        CHECK(waitFor([&] { return code >= 0; }), "termina");
        CHECK(code == 3, "codigo de salida");
        CHECK(screenText(s).startsWith("HOLA mundo"), "el texto llega a la grilla");
        std::vector<Cell> l;
        s.line(0, 0, l);
        CHECK(qRed(l[0].fg) > 150 && qGreen(l[0].fg) < 150, "HOLA en rojo");
        CHECK(l[6].fg == s.defaultFg(), "mundo con el color por defecto");
    }
    // --- 1a) pegar un texto largo no traba: el programa devuelve todo lo que lee (cat) y,
    //         mientras se pega, nadie lee lo suyo; antes la escritura esperaba en este mismo
    //         hilo y se trababan los dos (el encargo de los manuales colgó la app). ---------
    {
        TerminalSession s;
        s.resize(80, 24);
        CHECK(s.start(sh("stty raw -echo; exec cat")), "arranca cat");
        QThread::msleep(200);
        const QString largo = QString(50000, QChar('a')) + "FIN"; // (50.000 es justo 625 renglones de 80: FIN abre uno)
        QElapsedTimer t;
        t.start();
        s.paste(largo);
        CHECK(t.elapsed() < 1000, "pegar 50.000 letras vuelve enseguida (no espera a que el programa lea)");
        CHECK(waitFor([&] { return screenText(s).contains("FIN"); }, 15000), "y el texto llega entero");
    }
    // --- 1b) ratón: el programa lo pide (1000 + SGR 1006) y le llegan los clicks ---------------
    {
        TerminalSession s;
        s.resize(80, 24);
        CHECK(s.start(sh("printf '\\033[?1000h\\033[?1006h'; stty -icanon -echo; exec cat -v")), "arranca con ratón");
        CHECK(waitFor([&] { return s.mouseMode(); }), "el programa pidió el ratón");
        CHECK(!s.mouseMoveMode(), "1000: sin movimiento");
        s.mouseButton(2, 5, 1, true, 0);
        s.mouseButton(2, 5, 1, false, 0);
        CHECK(waitFor([&] { return screenText(s).contains("^[[<0;6;3M^[[<0;6;3m"); }),
              "el click llega al programa (SGR: apretar y soltar)");
        s.stop();
    }
    // --- 2) tamaño ----------------------------------------------------------------------------
    {
        TerminalSession s;
        s.resize(100, 30);
        bool done = false;
        QObject::connect(&s, &TerminalSession::finished, [&](int) { done = true; });
        s.start(sh("stty size; sleep 0.4; stty size"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
        waitFor([&] { return screenText(s).contains("30 100"); }, 2000);
        s.resize(120, 40);
        CHECK(waitFor([&] { return done; }), "termina");
        const QString t = screenText(s);
        CHECK(t.contains("30 100"), "el programa ve 30x100");
        CHECK(t.contains("40 120"), "y el cambio a 40x120");
    }
    // --- 3) entrada: teclas y pegado -----------------------------------------------------------
    {
        TerminalSession s;
        s.resize(80, 24);
        Pty::Options o;
        o.program = "/bin/cat";
        CHECK(s.start(o), "arranca cat");
        for (char32_t c : QString("hola").toUcs4())
            s.unichar(c, 0);
        s.key(1 /* VTERM_KEY_ENTER */, 0);
        CHECK(waitFor([&] { return screenText(s).count("hola") >= 2; }), "lo tipeado vuelve (eco + cat)");
        s.paste("pegado largo con ñ y á");
        s.key(1, 0);
        CHECK(waitFor([&] { return screenText(s).count("pegado largo con ñ y á") >= 2; }),
              "el pegado llega con acentos");
        s.submit("mensaje de alerta");
        CHECK(waitFor([&] { return screenText(s).count("mensaje de alerta") >= 2; }),
              "submit escribe y manda Enter");
        s.stop();
        CHECK(!s.running(), "se detiene");
    }
    // --- 4) historial -------------------------------------------------------------------------
    {
        TerminalSession s;
        s.resize(80, 10);
        bool done = false;
        QObject::connect(&s, &TerminalSession::finished, [&](int) { done = true; });
        s.start(sh("i=1; while [ $i -le 60 ]; do echo linea-$i; i=$((i+1)); done"));
        CHECK(waitFor([&] { return done; }), "termina");
        CHECK(s.scrollbackSize() >= 50, "lo que se fue por arriba queda en el historial");
        const QString all = s.text(-s.scrollbackSize(), 0, s.rows() - 1, s.cols() - 1, 0);
        CHECK(all.contains("linea-1\n") && all.contains("linea-60"), "el historial se lee completo");
    }
    // --- 5) anchos y acentos --------------------------------------------------------------------
    {
        TerminalSession s;
        s.resize(40, 5);
        bool done = false;
        QObject::connect(&s, &TerminalSession::finished, [&](int) { done = true; });
        s.start(sh("printf 'año 界x\\n'"));
        CHECK(waitFor([&] { return done; }), "termina");
        std::vector<Cell> l;
        s.line(0, 0, l);
        CHECK(l[1].ch == U'ñ', "ñ en su celda");
        if (!(l[4].ch == U'界' && l[4].width == 2 && l[5].ch == 0))
            std::printf("   celdas: %x/%d %x/%d %x/%d\n", unsigned(l[4].ch), l[4].width, unsigned(l[5].ch),
                        l[5].width, unsigned(l[6].ch), l[6].width);
        CHECK(l[4].ch == U'界' && l[4].width == 2 && l[5].ch == 0, "caracter ancho ocupa dos celdas");
        CHECK(l[6].ch == U'x', "lo que sigue queda en su lugar");
    }

    // --- 6) una pantalla real, byte por byte --------------------------------
    // Lo que grabo BA_TERM_DUMP de una sesion de Claude Code de verdad.
    //
    // QUE SE ROMPIO. `vterm_screen_get_cell` copia `chars` solo hasta el
    // primer 0 INCLUSIVE y deja el resto como estaba. Para una celda vacia
    // escribe chars[0]=0 y corta, sin tocar chars[1..5]. Nosotros pediamos
    // todas las celdas de una fila en la MISMA variable sin inicializar, asi
    // que cada celda en blanco heredaba los caracteres combinantes de la
    // ultima que si tenia —⚠️ y ⏸️ traen un selector de variacion— y de ahi en
    // adelante la fila entera se dibujaba con una capa de glifos encima.
    //
    // Lo dificil era relacionarlo con su causa: el TEXTO estaba bien (copiar
    // esas zonas daba renglones en blanco, porque lo que sobraba era lo que le
    // colgabamos al dibujar), y solo aparece con una interfaz que use emojis
    // con selector de variacion. Por eso la prueba es el volcado real y no un
    // caso inventado.
    {
        QFile f(QStringLiteral(BA_TEST_TERM_DUMP));
        CHECK(f.open(QIODevice::ReadOnly), "se puede leer el volcado de Claude Code");
        const QByteArray bytes = f.readAll();
        TerminalSession s;
        s.resize(122, 35);
        s.alimentarParaPruebas(bytes);

        std::vector<Cell> l;
        int filasSucias = 0;
        int conExtra = 0;
        for (int r = 0; r < s.rows(); ++r) {
            s.line(r, 0, l);
            bool sucia = false;
            for (const Cell& c : l) {
                if (c.extra.isEmpty()) continue;
                ++conExtra;
                // Un combinante SOLO puede colgar de un caracter de verdad.
                if (c.ch == 0 || c.ch == U' ') sucia = true;
            }
            if (sucia) ++filasSucias;
        }
        CHECK(filasSucias == 0, "ninguna celda vacia arrastra caracteres combinantes");
        // En ESTE volcado no hay ni un combinante legitimo: libvterm no guarda
        // el selector de variacion de ⚠️/⏸️. O sea que los 122 por fila que
        // aparecian antes del arreglo no venian de otra celda —venian
        // directamente de la pila—. Que no se arreglo tirando los combinantes
        // de verdad se prueba abajo, con un caso hecho a proposito.
        CHECK(conExtra == 0, "esta pantalla no tiene combinantes de verdad");

        s.line(1, 0, l);
        QString cab;
        for (const Cell& c : l)
            cab += QString::fromUcs4(&c.ch, 1);
        CHECK(cab.contains("Claude Code"), "la cabecera se lee");
        s.line(15, 0, l);
        bool vacia = true;
        for (const Cell& c : l)
            if (c.ch != U' ' || !c.extra.isEmpty()) vacia = false;
        CHECK(vacia, "una fila vacia esta realmente vacia");
    }
    // --- 7) y los combinantes DE VERDAD siguen llegando ----------------------
    // La otra mitad del arreglo: no alcanza con que lo vacio quede vacio si de
    // paso se perdio una tilde. Una `e` con acento combinante (U+0301) tiene
    // que llegar entera, y la celda de al lado tiene que quedar limpia.
    {
        TerminalSession s;
        s.resize(20, 5);
        s.alimentarParaPruebas(QByteArray("e\xcc\x81x"));
        std::vector<Cell> l;
        s.line(0, 0, l);
        CHECK(l[0].ch == U'e' && l[0].extra == QString(QChar(0x0301)), "la e se lleva su acento");
        CHECK(l[1].ch == U'x' && l[1].extra.isEmpty(), "y la de al lado no lo hereda");
        CHECK(l[2].ch == U' ' && l[2].extra.isEmpty(), "ni la vacia que sigue");
    }

    if (g_failures == 0) std::printf("test_terminal: OK\n");
    return g_failures == 0 ? 0 : 1;
}
