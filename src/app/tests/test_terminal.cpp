// test_terminal.cpp — la terminal de la pestaña Asistente (Pty + libvterm)
//
//   1. Un programa real en la pseudo-terminal: su salida (con colores ANSI)
//      llega a la grilla con el texto y el color correctos; termina con su codigo.
//   2. El programa ve el tamaño de la terminal (stty size) y un cambio de tamaño.
//   3. Entrada: lo que se "tipea" y lo que se pega le llega al programa (cat).
//   4. Historial: lo que sale por arriba queda en el scrollback y se puede leer.
//   5. Caracteres anchos y acentos (UTF-8).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>

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

    if (g_failures == 0) std::printf("test_terminal: OK\n");
    return g_failures == 0 ? 0 : 1;
}
