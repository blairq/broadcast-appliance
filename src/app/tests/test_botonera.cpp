// test_botonera.cpp — bus de eventos y botonera (control/) con ganchos falsos
//
//   patrones del bus (+, #, *), último valor y sin repetir; plantillas; celdas
//   validadas; render con ligaduras (tally, texto con {{v}}, activo); permisos
//   del perfil (lo que no está, no corre); confirmación de aire en el aparato;
//   {{valor}} y {{no_activo}}; secuencias; emparejamiento (código de un uso,
//   token con hash y sal); revocar; el asistente no amplía permisos.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "control/Botonera.hpp"
#include "control/BusEventos.hpp"

using ba::app::control::Botonera;
using ba::app::control::BusEventos;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                    \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);        \
            ++g_fallas;                                                \
        } else {                                                       \
            std::printf("ok: %s\n", m);                                \
        }                                                              \
    } while (0)

void esperar(int ms) {
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;

    // --- Bus ---------------------------------------------------------------------
    BusEventos bus;
    CHECK(BusEventos::coincide("aire/#", "aire/tally"), "# es el resto");
    CHECK(BusEventos::coincide("audio/nivel/+", "audio/nivel/Mic 1"), "+ es un nivel");
    CHECK(!BusEventos::coincide("audio/nivel/+", "audio/nivel/a/b"), "+ no es dos niveles");
    CHECK(BusEventos::coincide("alarma/*", "alarma/x/y"), "* al final es el resto");
    CHECK(!BusEventos::coincide("aire/tally", "aire/escena"), "exacto");
    int avisos = 0;
    bus.suscribir("aire/#", [&](const std::string&, const json&) { ++avisos; });
    bus.publicar("aire/escena", {{"escena", "A"}});
    bus.publicar("aire/escena", {{"escena", "A"}});
    CHECK(avisos == 1, "el mismo valor no avisa dos veces");
    CHECK(bus.ultimo("aire/escena")["valor"]["escena"] == "A", "guarda el último valor");

    // --- Botonera ---------------------------------------------------------------
    std::vector<std::pair<std::string, json>> llamadas;
    bool hayAire = false;
    Botonera::Ganchos g;
    g.escenas = [] { return std::vector<std::string>{"Cámara 1", "Cámara 2"}; };
    g.fuentesAudio = [] { return std::vector<std::string>{"Mic 1"}; };
    g.herramientas = [] {
        return std::set<std::string>{"scene_preview", "scene_take", "aire_ftb", "record_start", "stream_start",
                                     "audio_set", "banca_al_aire", "banca_terminar", "prompter_control", "receta_correr"};
    };
    g.deAireAhora = [&](const std::string& h, const json&) { return hayAire && h == "scene_take"; };
    g.ejecutar = [&](const std::string& h, const json& a, const std::string&, const std::string&, Botonera::Listo l) {
        llamadas.emplace_back(h, a);
        l(true, "");
    };
    Botonera bot(dir.path().toStdString(), bus, g);
    std::vector<json> mensajes;
    QObject::connect(&bot, &Botonera::mensaje, [&](const QString&, const json& m) { mensajes.push_back(m); });

    const json perfiles = bot.perfiles();
    CHECK(perfiles.size() == 1 && perfiles[0]["nombre"] == "Director", "de fábrica, un perfil Director");
    const std::string pid = perfiles[0]["id"];
    const std::string pag = perfiles[0]["paginas"][0]["id"];
    const json perm = perfiles[0]["herramientas_permitidas"];
    CHECK(std::find(perm.begin(), perm.end(), "scene_take") != perm.end(), "la plantilla trae sus herramientas");

    // Un aparato local (como el de un plugin) con el perfil Director.
    bot.registrarLocal("hw1", "Deck", "plugin", json::object());
    CHECK(bot.dispositivos().size() == 1, "aparato local registrado");
    bot.entrada("hw1", {{"type", "hola"}});
    CHECK(!mensajes.empty() && mensajes.back()["type"] == "estado", "hola -> estado completo");

    // Tally: «Cámara 2» al aire se pinta de rojo.
    bus.publicar("aire/tally", {{"programa", "Cámara 2"}, {"preview", "Cámara 1"}});
    json r = bot.render("hw1");
    bool rojo = false, verde = false;
    for (const auto& c : r["celdas"]) {
        if (c["texto"] == "Cámara 2" && c["fondo"] == "#c62828") rojo = true;
        if (c["texto"] == "Cámara 1" && c["fondo"] == "#1f8a3c") verde = true;
    }
    CHECK(rojo && verde, "ligaduras de tally");

    // Apretar una escena: scene_preview con su nombre.
    llamadas.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 1}, {"y", 0}});
    CHECK(llamadas.size() == 1 && llamadas[0].first == "scene_preview" && llamadas[0].second["name"] == "Cámara 2",
          "presionar ejecuta la acción de la celda");

    // TAKE al aire pide confirmación en el aparato.
    hayAire = true;
    llamadas.clear();
    mensajes.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 0}, {"y", 3}});
    CHECK(llamadas.empty(), "acción de aire: no corre sin confirmar");
    bool pidio = false;
    for (const auto& m : mensajes) if (m["type"] == "confirmar" && m["celda"] == "0,3") pidio = true;
    CHECK(pidio, "pide confirmar en el aparato");
    bot.entrada("hw1", {{"type", "confirmar"}, {"x", 0}, {"y", 3}});
    CHECK(llamadas.size() == 1 && llamadas[0].first == "scene_take", "confirmado, corre");
    hayAire = false;

    // Una celda con una herramienta que no está en el perfil: no corre.
    CHECK(bot.setCelda(pid, pag, {{"x", 3}, {"y", 3}, {"tipo", "boton"},
                                  {"accion", {{"tipo", "herramienta"}, {"herramienta", "banca_terminar"}}}}).empty(),
          "celda nueva");
    llamadas.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 3}, {"y", 3}});
    CHECK(llamadas.empty(), "fuera de herramientas_permitidas no corre");
    CHECK(!bot.setCelda(pid, pag, {{"x", 3}, {"y", 3}, {"accion", {{"herramienta", "no_existe"}}}}).empty(),
          "una herramienta que no existe no se acepta");
    CHECK(!bot.setCelda(pid, pag, {{"x", 30}, {"y", 3}}).empty(), "fuera de la grilla no");

    // El asistente no amplía permisos; una persona sí.
    CHECK(!bot.editarPerfil(pid, {{"herramientas_permitidas", {"banca_terminar"}}}, false).empty(),
          "el asistente no suma herramientas");
    json nuevas = perm;
    nuevas.push_back("banca_terminar");
    nuevas.push_back("audio_set");
    CHECK(bot.editarPerfil(pid, {{"herramientas_permitidas", nuevas}}, true).empty(), "una persona sí");
    llamadas.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 3}, {"y", 3}});
    CHECK(llamadas.size() == 1, "ahora corre");

    // Fader con {{valor}}, coalescido.
    CHECK(bot.setCelda(pid, pag, {{"x", 2}, {"y", 3}, {"tipo", "fader"},
                                  {"valor", {{"min", -60}, {"max", 6}, {"paso", 0.5}}},
                                  {"accion", {{"herramienta", "audio_set"},
                                              {"args", {{"name", "Mic 1"}, {"volumeDb", "{{valor}}"}}}}}}).empty(),
          "fader");
    llamadas.clear();
    for (double v : {-30.0, -20.0, -10.2}) bot.entrada("hw1", {{"type", "valor"}, {"x", 2}, {"y", 3}, {"v", v}});
    esperar(80);
    CHECK(llamadas.size() == 1 && llamadas[0].second["volumeDb"] == -10.0, "el último valor gana, redondeado al paso");

    // Mudo con {{no_activo}} y ligadura activo.
    CHECK(bot.setCelda(pid, pag, {{"x", 5}, {"y", 2}, {"tipo", "boton"},
                                  {"accion", {{"herramienta", "audio_set"}, {"args", {{"name", "Mic 1"}, {"muted", "{{no_activo}}"}}}}},
                                  {"estado", {{{"tema", "audio/nivel/Mic 1"}, {"ruta", "mudo"}, {"op", "=="}, {"valor", true}, {"efecto", "activo"}}}}}).empty(),
          "mudo");
    bus.publicar("audio/nivel/Mic 1", {{"mudo", true}});
    llamadas.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 5}, {"y", 2}});
    CHECK(llamadas.size() == 1 && llamadas[0].second["muted"] == false, "alternar: si está mudo, lo desmuta");

    // Texto con {{v}}.
    CHECK(bot.setCelda(pid, pag, {{"x", 4}, {"y", 2}, {"tipo", "texto"},
                                  {"estado", {{{"tema", "audio/loudness"}, {"ruta", "i"}, {"efecto", "texto"}, {"dato", "I {{v}}"}}}}}).empty(),
          "texto");
    bus.publicar("audio/loudness", {{"i", -23.4}});
    bool txt = false;
    const json rt = bot.render("hw1"); // en una variable: el temporal no vive durante el for
    for (const auto& c : rt["celdas"]) if (c["x"] == 4 && c["y"] == 2 && c["texto"] == "I -23.4") txt = true;
    CHECK(txt, "texto con el valor del tema");

    // --- Emparejamiento ------------------------------------------------------------
    const json cod = bot.nuevoCodigo(pid, "iPad", 30);
    const std::string codigo = cod["codigo"];
    CHECK(codigo.size() == 32, "código de 128 bits");
    CHECK(!bot.solicitud("s0", "ffffffffffffffffffffffffffffffff", "x", "10.0.0.2").empty(), "código inválido");
    CHECK(bot.solicitud("s1", codigo, "iPad Dirección", "10.0.0.2").empty(), "código válido");
    CHECK(!bot.solicitud("s2", codigo, "otra", "10.0.0.3").empty(), "el código es de un solo uso");
    const json ap = bot.resolverSolicitud("s1", true);
    CHECK(ap["aprobada"] == true && ap["token"].get<std::string>().size() == 64, "aprobada, token de 256 bits");
    const json cred = bot.credencialesParaGateway();
    CHECK(cred.size() == 1, "una credencial para el gateway");
    const std::string esperado =
        QCryptographicHash::hash(QByteArray::fromStdString(cred[0]["sal"].get<std::string>() + ap["token"].get<std::string>()),
                                 QCryptographicHash::Sha256).toHex().toStdString();
    CHECK(cred[0]["hash"] == esperado, "solo se guarda sha256(sal + token)");
    for (const auto& d : bot.dispositivos()) CHECK(!d.contains("hash") && !d.contains("sal"), "la lista no muestra hashes");
    CHECK(bot.revocar(ap["dispositivo"]) == 1 && bot.credencialesParaGateway().empty(), "revocar");

    // Persistencia del modelo.
    {
        Botonera otra(dir.path().toStdString(), bus, g);
        bool hay = false;
        for (const auto& p : otra.perfiles()) if (p["id"] == pid) hay = true;
        CHECK(hay, "el perfil queda guardado");
    }

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
