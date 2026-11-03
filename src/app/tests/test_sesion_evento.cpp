// test_sesion_evento.cpp — una conversación del asistente por evento
//
//   SesionesEvento: la de cada asistente y motor se guarda y se relee; la de un
//   evento COPIADO (otro nombre de carpeta) no se usa; olvidar borra las de un
//   asistente y no las del otro.
//   ColaAutomatica: junta en orden, sin repetir adjuntos, con tope; un lote que no
//   salió vuelve adelante.
//   EnvioOpencode (con un servidor falso, sin red): la sesión guardada se usa si
//   existe, si no (404) se crea otra; vacía, se crea. Con la sesión ocupada no
//   manda nada y lo que llega se junta; libre, sale UN mensaje con todo, en cola
//   (`delivery: queue`) y con los adjuntos como file://. Si el envío falla, se
//   reintenta sin perder ni desordenar nada; tras varios fallos se descarta y avisa.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/EnvioOpencode.hpp"
#include "agent/SesionesEvento.hpp"
#include "persist/ColaEscritura.hpp"

using ba::app::agent::ColaAutomatica;
using ba::app::agent::EnvioOpencode;
using ba::app::agent::SesionesEvento;
using json = nlohmann::json;

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

template <class F> bool esperar(F cond, int ms = 4000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return cond();
}

/// El servidor de OpenCode, de mentira: las sesiones que tiene, si la del evento está
/// ocupada, y lo que le llegó.
struct ServidorFalso {
    std::vector<std::string> sesiones{"ses_vieja"};
    bool ocupada = false;
    int fallarPrompts = 0; ///< cuántos POST …/prompt contestan 500
    int creadas = 0;
    std::vector<json> prompts; ///< los cuerpos que llegaron a …/prompt
    std::vector<std::string> pedidos;

    EnvioOpencode::Http http() {
        return [this](const QByteArray& metodo, const QString& ruta, const QByteArray& cuerpo,
                      std::function<void(int, const QByteArray&)> listo) {
            const std::string m = metodo.toStdString(), r = ruta.toStdString();
            pedidos.push_back(m + " " + r);
            int st = 404;
            json resp = {{"_tag", "SessionNotFoundError"}, {"message", "no está"}};
            if (m == "GET" && r == "/api/session/active") {
                st = 200;
                resp = {{"data", ocupada ? json{{"ses_nueva", {{"type", "running"}}}, {"ses_vieja", {{"type", "running"}}}}
                                         : json::object()}};
            } else if (m == "GET" && r.rfind("/api/session/", 0) == 0) {
                const std::string id = r.substr(13);
                for (const auto& s : sesiones)
                    if (s == id) st = 200, resp = {{"data", {{"id", id}}}};
            } else if (m == "POST" && r == "/api/session") {
                ++creadas;
                sesiones.push_back("ses_nueva");
                st = 200;
                resp = {{"data", {{"id", "ses_nueva"}, {"title", json::parse(cuerpo.toStdString())["title"]}}}};
            } else if (m == "POST" && r.size() > 7 && r.compare(r.size() - 7, 7, "/prompt") == 0) {
                if (fallarPrompts > 0) {
                    --fallarPrompts;
                    st = 500;
                    resp = {{"message", "falló"}};
                } else {
                    prompts.push_back(json::parse(cuerpo.toStdString()));
                    st = 200;
                    resp = {{"data", {{"id", "msg_1"}}}};
                }
            }
            // Como la red de verdad: la respuesta llega después, por el bucle de eventos.
            const QByteArray b = QByteArray::fromStdString(resp.dump());
            QTimer::singleShot(1, [listo, st, b]() { listo(st, b); });
        };
    }
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_sesion_evento ==\n");
    QTemporaryDir tmp;
    const std::filesystem::path raiz = std::filesystem::path(tmp.path().toStdString()) / "Mi evento";

    // --- SesionesEvento ---------------------------------------------------------------
    {
        SesionesEvento s = SesionesEvento::delEventoAbierto(raiz);
        CHECK(s.de("evento", "opencode").empty(), "sin nada guardado: ninguna");
        s.poner("evento", "opencode", "ses_a");
        s.poner("evento", "claude", "uuid-a");
        s.poner("automatizacion", "opencode", "ses_b");
        CHECK(ba::persist::cola().vaciar(), "el escritor vacía su cola");
        SesionesEvento otra = SesionesEvento::delEventoAbierto(raiz / ""); // con barra final, igual
        CHECK(otra.de("evento", "opencode") == "ses_a", "se relee la del evento (opencode)");
        CHECK(otra.de("evento", "claude") == "uuid-a", "cada motor la suya");
        CHECK(otra.de("automatizacion", "opencode") == "ses_b", "cada asistente la suya");
        // Un evento copiado trae el mismo archivo: con otro nombre no la usa (y no la pisa al leer).
        SesionesEvento copia(raiz / "Config" / "asistente-sesiones.json", "Mi evento (copia)");
        CHECK(copia.de("evento", "opencode").empty(), "la copia de un evento no usa la conversación del otro");
        copia.poner("evento", "opencode", "ses_copia");
        CHECK(copia.de("evento", "opencode") == "ses_copia" && copia.de("automatizacion", "opencode").empty(),
              "la primera que guarda la copia descarta las del otro");
        SesionesEvento s2 = SesionesEvento::delEventoAbierto(raiz);
        CHECK(s2.de("evento", "opencode").empty(), "y el original ya no la ve como suya");
        s2.poner("evento", "opencode", "ses_a");
        s2.poner("automatizacion", "opencode", "ses_b");
        s2.olvidar("evento");
        CHECK(s2.de("evento", "opencode").empty() && s2.de("automatizacion", "opencode") == "ses_b",
              "olvidar: solo las de ese asistente");
        s2.poner("automatizacion", "opencode", "");
        CHECK(s2.de("automatizacion", "opencode").empty(), "poner vacío la olvida");
    }

    // --- ColaAutomatica ---------------------------------------------------------------
    {
        ColaAutomatica c;
        c.agregar("  ");
        CHECK(c.vacia(), "un texto vacío no se encola");
        c.agregar("uno", {"/a.png"});
        c.agregar("dos", {"/a.png", "/b.png"});
        c.agregar("tres");
        ColaAutomatica::Lote l = c.tomar(2);
        CHECK(l.texto == "uno\n\ndos" && l.adjuntos == QStringList({"/a.png", "/b.png"}) && c.size() == 1,
              "junta en orden, sin repetir adjuntos, con tope");
        c.devolver(l);
        CHECK(c.size() == 3 && c.tomar().texto == "uno\n\ndos\n\ntres", "lo devuelto vuelve adelante, en orden");
    }

    // --- EnvioOpencode: la sesión del evento --------------------------------------------
    {
        ServidorFalso srv;
        EnvioOpencode e;
        e.setHttp(srv.http());
        QString id, err;
        bool listo = false;
        e.asegurarSesion("ses_vieja", "Mi evento · Evento", tmp.path(), [&](const QString& i, const QString& x) {
            id = i, err = x, listo = true;
        });
        CHECK(esperar([&] { return listo; }) && id == "ses_vieja" && err.isEmpty() && srv.creadas == 0,
              "la guardada existe: se usa esa (no se crea otra)");
        listo = false;
        e.asegurarSesion("ses_borrada", "Mi evento · Evento", tmp.path(), [&](const QString& i, const QString& x) {
            id = i, err = x, listo = true;
        });
        CHECK(esperar([&] { return listo; }) && id == "ses_nueva" && srv.creadas == 1 && e.sesion() == "ses_nueva",
              "la guardada ya no está (404): se crea otra");
        listo = false;
        e.asegurarSesion("", "Mi evento · Evento", tmp.path(), [&](const QString& i, const QString&) {
            id = i, listo = true;
        });
        CHECK(esperar([&] { return listo; }) && srv.creadas == 2, "sin guardada: se crea");
    }

    // --- EnvioOpencode: encolar y agrupar -----------------------------------------------
    {
        ServidorFalso srv;
        EnvioOpencode e;
        e.setHttp(srv.http());
        e.setEsperaMs(30);
        e.setSesion("ses_vieja");
        int enviados = 0, avisos = 0;
        QObject::connect(&e, &EnvioOpencode::enviado, [&](const QString&, int n) { ++enviados, avisos += n; });

        // Respondiendo (u operador con un turno en curso): no sale nada, se junta.
        srv.ocupada = true;
        e.encolar("[AVISO AUTOMATICO de Broadcast Appliance] uno");
        e.encolar("[AVISO AUTOMATICO de Broadcast Appliance] dos", {tmp.path() + "/foto.png"});
        e.encolar("[NOVEDADES del evento] tres");
        esperar([&] { return false; }, 200);
        CHECK(srv.prompts.empty() && e.pendientes() == 3, "con la sesión ocupada no se manda nada");
        // Se libera: UN mensaje con los tres, en orden, en cola y con el adjunto.
        srv.ocupada = false;
        CHECK(esperar([&] { return enviados == 1; }), "libre: sale");
        esperar([&] { return false; }, 100);
        CHECK(srv.prompts.size() == 1 && avisos == 3 && e.pendientes() == 0, "una ráfaga es un solo mensaje");
        if (srv.prompts.size() == 1) {
            const json& p = srv.prompts[0];
            const std::string t = p.value("text", "");
            CHECK(t.find("uno") < t.find("dos") && t.find("dos") < t.find("tres"), "en el orden en que llegaron");
            CHECK(t.rfind("[AVISO AUTOMATICO", 0) == 0, "con su marca: se distingue en la conversación");
            CHECK(p.value("delivery", "") == "queue", "en cola: no corta un turno en curso");
            CHECK(p.contains("files") && p["files"].size() == 1 &&
                      p["files"][0].value("uri", "") ==
                          QUrl::fromLocalFile(tmp.path() + "/foto.png").toString().toStdString() &&
                      p["files"][0].value("name", "") == "foto.png",
                  "el adjunto va como archivo del mensaje (file://)");
        }
        CHECK(std::count(srv.pedidos.begin(), srv.pedidos.end(), "POST /api/session/ses_vieja/prompt") == 1,
              "a la sesión del evento, no a una nueva");

        // El envío falla: se reintenta sin perder ni desordenar.
        srv.fallarPrompts = 2;
        e.encolar("cuatro");
        CHECK(esperar([&] { return enviados == 2; }), "tras dos fallos, sale");
        CHECK(srv.prompts.size() == 2 && srv.prompts[1].value("text", "") == "cuatro", "lo que falló se mandó entero");

        // Falla siempre: tras varios intentos se descarta y se avisa (no crece sin fin).
        QString aviso;
        QObject::connect(&e, &EnvioOpencode::fallo, [&](const QString& t) { aviso = t; });
        srv.fallarPrompts = 100;
        e.encolar("cinco");
        CHECK(esperar([&] { return !aviso.isEmpty(); }) && e.pendientes() == 0, "tras varios fallos: se descarta y avisa");

        // Desconectado (el asistente se detuvo): lo pendiente se descarta y nada sale.
        srv.fallarPrompts = 0;
        srv.ocupada = true;
        e.encolar("seis");
        e.desconectar();
        srv.ocupada = false;
        esperar([&] { return false; }, 150);
        CHECK(srv.prompts.size() == 2 && !e.conectado() && e.sesion().isEmpty(), "desconectado: no sale nada");
    }

    if (g_failures == 0) {
        std::printf("test_sesion_evento: OK\n");
        return 0;
    }
    std::printf("test_sesion_evento: %d fallo(s)\n", g_failures);
    return 1;
}
