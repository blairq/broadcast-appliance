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
    std::string aireSolo;          // con algo: solo esa salida sale (la confirmación es de la del aparato)
    std::vector<std::string> aireDe; // la salida con la que se preguntó si cambia el aire
    Botonera::Ganchos g;
    g.escenas = [] { return std::vector<std::string>{"Cámara 1", "Cámara 2"}; };
    g.fuentesAudio = [] { return std::vector<std::string>{"Mic 1"}; };
    g.herramientas = [] {
        return std::set<std::string>{"scene_preview", "preview_al_aire", "aire_ftb", "record_start", "stream_start",
                                     "audio_set", "banca_al_aire", "recinto_orador_terminar", "prompter_control", "receta_correr"};
    };
    g.deAireAhora = [&](const std::string& h, const json&, const std::string& salida) {
        aireDe.push_back(salida);
        return hayAire && h == "preview_al_aire" && (aireSolo.empty() || salida == aireSolo);
    };
    std::vector<std::string> salidasLlamadas; // la salida (CallContext::salida) de cada llamada
    g.ejecutar = [&](const std::string& h, const json& a, const std::string&, const std::string&, const std::string& sal,
                     Botonera::Listo l) {
        llamadas.emplace_back(h, a);
        salidasLlamadas.push_back(sal);
        l(true, "");
    };
    g.recinto = [] {
        json banca{{"banca", "7"}, {"fila", 1}, {"columna", 3}, {"integrante", {{"nombre", "Ana"}}}};
        return json{{"filas", 2}, {"columnas", 5}, {"bancas", json::array({banca})}};
    };
    Botonera bot(dir.path().toStdString(), bus, g);
    {
        std::string idRec;
        const std::string e = bot.crearPerfil({{"nombre", "Sala"}}, "recinto", &idRec);
        const json pr = bot.perfil(idRec);
        bool enSuLugar = false;
        for (const auto& c : pr["paginas"][0]["celdas"])
            if (c.value("x", -1) == 3 && c.value("y", -1) == 1 &&
                c.value("accion", json::object()).value("args", json::object()).value("banca", std::string()) == "7")
                enSuLugar = true;
        CHECK(e.empty() && pr["paginas"][0]["filas"] == 3 && enSuLugar, "plantilla Recinto: cada banca en su lugar de la grilla");
        bot.borrarPerfil(idRec);
    }
    {
        // La de la presidencia, fuera de la grilla: en una fila propia, al centro.
        const auto antes = g.recinto;
        g.recinto = [] {
            json banca{{"banca", "7"}, {"fila", 1}, {"columna", 3}};
            return json{{"filas", 2}, {"columnas", 5}, {"bancas", json::array({banca})},
                        {"presidencia", {{"banca", "1"}, {"integrante", {{"nombre", "Presi"}}}}}};
        };
        Botonera botP((dir.path() + "/bp").toStdString(), bus, g);
        std::string idRec;
        CHECK(botP.crearPerfil({{"nombre", "Sala"}}, "recinto", &idRec).empty(), "plantilla Recinto con presidencia");
        const json pr = botP.perfil(idRec);
        bool presi = false;
        for (const auto& c : pr["paginas"][0]["celdas"])
            if (c.value("x", -1) == 2 && c.value("y", -1) == 2 &&
                c.value("accion", json::object()).value("args", json::object()).value("banca", std::string()) == "1")
                presi = true;
        CHECK(pr["paginas"][0]["filas"] == 4 && presi, "la presidencia en su fila, antes de los comandos");
        g.recinto = antes;
    }
    {
        // Micrófonos de bancas: caras, borde que titila, insignia del turno, atenuado.
        g.recinto = [] {
            json b5{{"banca", "5"}, {"fila", 0}, {"columna", 1}, {"integrante", {{"nombre", "Ana Pérez"}}}};
            return json{{"filas", 1}, {"columnas", 4}, {"bancas", json::array({b5})}};
        };
        Botonera bot2((dir.path() + "/b2").toStdString(), bus, g);
        std::string idMic;
        CHECK(bot2.crearPerfil({{"nombre", "Mics"}}, "microfonos", &idMic).empty(), "plantilla Micrófonos de bancas");
        bus.publicar("plugin/bosch-dcn/dcn/estado",
                     {{"enlace", "ok"}, {"abiertos", json::array()}, {"siguiente", "5"},
                      {"pedidos", json::array({json{{"banca", "5"}, {"turno", 1}}})},
                      {"unidades", json::array({json{{"banca", "5"}, {"conectada", true}}})}});
        bot2.registrarLocal("hw2", "Deck", "plugin", json::object());
        bot2.asignar("hw2", idMic, "p1");
        json rr = bot2.render("hw2");
        json c5;
        for (const auto& c : rr["celdas"])
            if (c.value("x", -1) == 1 && c.value("y", -1) == 0) c5 = c;
        CHECK(c5.value("borde", std::string()) == "#ff1744" && c5.value("borde_titilar", false), "le toca: borde rojo que titila");
        CHECK(c5.value("insignia", std::string()) == "1", "insignia con el turno (filtro pedidos[banca=5].turno)");
        CHECK(!c5.value("atenuar", true), "con unidad conectada no se atenúa");
        bus.publicar("plugin/bosch-dcn/dcn/estado", {{"enlace", "caido"}, {"abiertos", json::array({"5"})}});
        rr = bot2.render("hw2");
        for (const auto& c : rr["celdas"])
            if (c.value("x", -1) == 1 && c.value("y", -1) == 0) c5 = c;
        CHECK(c5.value("atenuar", false) && c5.value("fondo", std::string()) == "#c62828" && c5.value("activo", false),
              "abierto: rojo y activo; sin central: atenuado");
    }
    std::vector<json> mensajes;
    QObject::connect(&bot, &Botonera::mensaje, [&](const QString&, const json& m) { mensajes.push_back(m); });

    const json perfiles = bot.perfiles();
    CHECK(perfiles.size() == 1 && perfiles[0]["nombre"] == "Director", "de fábrica, un perfil Director");
    const std::string pid = perfiles[0]["id"];
    const std::string pag = perfiles[0]["paginas"][0]["id"];
    const json perm = perfiles[0]["herramientas_permitidas"];
    CHECK(std::find(perm.begin(), perm.end(), "preview_al_aire") != perm.end(), "la plantilla trae sus herramientas");

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
    CHECK(llamadas.size() == 1 && llamadas[0].first == "preview_al_aire", "confirmado, corre");
    hayAire = false;

    // Una celda con una herramienta que no está en el perfil: no corre.
    CHECK(bot.setCelda(pid, pag, {{"x", 3}, {"y", 3}, {"tipo", "boton"},
                                  {"accion", {{"tipo", "herramienta"}, {"herramienta", "recinto_orador_terminar"}}}}).empty(),
          "celda nueva");
    llamadas.clear();
    bot.entrada("hw1", {{"type", "presionar"}, {"x", 3}, {"y", 3}});
    CHECK(llamadas.empty(), "fuera de herramientas_permitidas no corre");
    CHECK(!bot.setCelda(pid, pag, {{"x", 3}, {"y", 3}, {"accion", {{"herramienta", "no_existe"}}}}).empty(),
          "una herramienta que no existe no se acepta");
    CHECK(!bot.setCelda(pid, pag, {{"x", 30}, {"y", 3}}).empty(), "fuera de la grilla no");

    // El asistente no amplía permisos; una persona sí.
    CHECK(!bot.editarPerfil(pid, {{"herramientas_permitidas", {"recinto_orador_terminar"}}}, false).empty(),
          "el asistente no suma herramientas");
    json nuevas = perm;
    nuevas.push_back("recinto_orador_terminar");
    nuevas.push_back("audio_set");
    CHECK(bot.editarPerfil(pid, {{"herramientas_permitidas", nuevas}}, true).empty(), "una persona sí");
    // Con un error en el medio no queda nada cambiado a medias.
    {
        const std::string nombreAntes = bot.perfil(pid).value("nombre", std::string());
        CHECK(!bot.editarPerfil(pid, {{"nombre", "Otro"}, {"herramientas_permitidas", "no-lista"}}, true).empty(),
              "perfil con un dato mal: error");
        CHECK(bot.perfil(pid).value("nombre", std::string()) == nombreAntes, "y el nombre no cambió");
        const int colAntes = bot.perfil(pid)["paginas"][0].value("columnas", 0);
        CHECK(!bot.editarPagina(pid, pag, {{"columnas", 8}, {"filas", 0}}).empty(), "página con filas 0: error");
        CHECK(bot.perfil(pid)["paginas"][0].value("columnas", 0) == colAntes, "y las columnas no cambiaron");
    }
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

    // Los botones del guion: la plantilla «director» los trae, y un perfil hecho antes los recibe
    // (completarGuion), una sola vez y con la herramienta permitida.
    {
        const auto cuantosCue = [&bot](const std::string& id) {
            int n = 0;
            const json perfil = bot.exportar(id); // guardado: un for sobre un pedazo de un temporal es UB
            for (const auto& c : perfil["paginas"][0]["celdas"])
                if (c.contains("accion") && c["accion"].value("herramienta", std::string()) == "evento_correr") ++n;
            return n;
        };
        std::string dir;
        const std::string eDir = bot.crearPerfil({{"nombre", "Escenas"}}, "director", &dir);
        CHECK(eDir.empty() && cuantosCue(dir) == 2,
              "la plantilla director trae «◀ Cue» y «Cue ▶»");
        std::string viejo;
        CHECK(bot.crearPerfil({{"nombre", "Viejo"}}, "", &viejo).empty() && cuantosCue(viejo) == 0, "un perfil sin guion");
        CHECK(bot.completarGuion(viejo).empty() && cuantosCue(viejo) == 2, "completarGuion le suma los dos botones");
        CHECK(bot.completarGuion(viejo).empty() && cuantosCue(viejo) == 2, "otra vez no los duplica");
    }

    // Al aprobar, la persona puede elegir otro perfil que el del código (Servicios).
    {
        std::string otro;
        CHECK(bot.crearPerfil({{"nombre", "Otro puesto"}}, "", &otro).empty() && !otro.empty(), "un segundo perfil");
        const std::string c2 = bot.nuevoCodigo(pid, "iPad 2", 30)["codigo"];
        CHECK(bot.solicitud("s3", c2, "iPad 2", "10.0.0.4").empty(), "pide con el perfil del código");
        const QVariantList sol = bot.solicitudesUi();
        CHECK(sol.size() == 1 && sol[0].toMap().value("perfilId").toString().toStdString() == pid &&
                  sol[0].toMap().value("ms").toDouble() > 0,
              "la solicitud trae el id del perfil y cuándo pidió");
        CHECK(bot.resolverSolicitud("s3", true, "no-existe").contains("error") && bot.solicitudesUi().size() == 1,
              "un perfil que no existe no aprueba (y el pedido sigue)");
        const json ap2 = bot.resolverSolicitud("s3", true, otro);
        CHECK(ap2["aprobada"] == true, "aprobada con otro perfil");
        bool conOtro = false;
        for (const auto& d : bot.dispositivos())
            if (d.value("id", std::string()) == ap2.value("dispositivo", std::string())) conOtro = d.value("perfil", std::string()) == otro;
        CHECK(conOtro, "el aparato queda con el perfil elegido al aprobar");
        CHECK(bot.revocar(ap2["dispositivo"]) == 1, "revocar el segundo");
    }

    // API remota HTTP (T59): el acceso es un aparato más; el gateway solo acepta su
    // token con la API prendida, y la app rechaza otro perfil u otra página.
    {
        const json acc = bot.crearAccesoHttp(pid, "Companion");
        CHECK(acc.contains("token") && acc["token"].get<std::string>().size() == 64, "acceso HTTP con token de 256 bits");
        const auto http = [&]() {
            for (const auto& c : bot.credencialesParaGateway())
                if (c["id"] == acc["dispositivo"]) return c.value("http", false);
            return false;
        };
        CHECK(!http(), "con la API apagada, el token no sirve para HTTP");
        bot.setApiRemotaActiva(true);
        CHECK(http(), "con la API prendida, sí");
        CHECK(bot.accesosHttp().size() == 1, "aparece en la lista de accesos");
        const std::string dev = acc["dispositivo"];
        CHECK(!bot.entradaRemota(dev, "otro-perfil", pag, 0, 0, "tocar", "1.2.3.4").empty(), "otro perfil: no");
        CHECK(!bot.entradaRemota(dev, pid, "no-existe", 0, 0, "tocar", "1.2.3.4").empty(), "otra página: no");
        CHECK(bot.entradaRemota(dev, pid, pag, 3, 3, "tocar", "1.2.3.4").empty(), "su perfil y su página: sí");
        bot.setApiRemotaActiva(false);
        CHECK(!bot.entradaRemota(dev, pid, pag, 3, 3, "tocar", "1.2.3.4").empty(), "apagada: no");
        CHECK(bot.revocar(dev) == 1 && bot.accesosHttp().empty(), "se revoca como cualquier aparato");
    }

    // Operaciones de una celda (las del panel del escritorio y del MCP): el
    // formulario, la cara y «al tocar, poner este medio al aire».
    {
        auto herrAntes = bot.ganchos().herramientas;
        bot.ganchos().herramientas = [herrAntes] {
            auto s = herrAntes();
            s.insert("medio_poner");
            return s;
        };
        bot.ganchos().medioDeCara = [](const std::string& id) {
            // «cam»: un video de cámara (no tiene medio para poner al aire).
            return id == "cam" ? json{{"medio", ""}, {"video", true}} : json{{"medio", "Caras/" + id + "/medio.mp4"}, {"video", true}};
        };
        std::string pg;
        CHECK(bot.crearPagina(pid, "Celdas", 4, 3, &pg).empty(), "página para probar celdas");
        CHECK(!bot.validarLugar(pid, pg, 4, 0).empty() && !bot.validarLugar(pid, "no-existe", 0, 0).empty() &&
                  bot.validarLugar(pid, pg, 3, 2).empty(),
              "validarLugar: fuera de la grilla o sin página, no");
        CHECK(!bot.formularioCelda(pid, pg, 0, 0)["existe"].get<bool>(), "formulario de una casilla vacía");

        CHECK(bot.editarCelda(pid, pg, 0, 0, {{"tipo", "boton"}, {"texto", "Cam 1"}, {"fondo", "#123456"},
                                               {"herramienta", "preview_al_aire"}, {"args", {{"name", "Cámara 1"}}}})
                  .empty(),
              "editarCelda crea la celda");
        json f = bot.formularioCelda(pid, pg, 0, 0);
        CHECK(f["existe"] == true && f["texto"] == "Cam 1" && f["fondo"] == "#123456" && f["herramienta"] == "preview_al_aire" &&
                  json::parse(f["args"].get<std::string>())["name"] == "Cámara 1",
              "el formulario muestra lo guardado");
        CHECK(bot.editarCelda(pid, pg, 0, 0, {{"fondo", ""}, {"herramienta", ""}}).empty(), "editarCelda: vaciar");
        f = bot.formularioCelda(pid, pg, 0, 0);
        CHECK(f["fondo"] == "" && f["herramienta"] == "" && f["texto"] == "Cam 1", "fondo vacío se saca, sin herramienta no hay acción");
        CHECK(bot.setCeldaEn(pid, pg, 1, 0, {{"accion", {{"tipo", "pagina"}, {"ir_a", pg}}}, {"x", 9}}).empty(),
              "setCeldaEn: manda el lugar, no el x de adentro");
        CHECK(bot.editarCelda(pid, pg, 1, 0, {{"texto", "Ir"}, {"herramienta", ""}}).empty() &&
                  json::parse(bot.formularioCelda(pid, pg, 1, 0)["json"].get<std::string>())["accion"]["tipo"] == "pagina",
              "el formulario no borra una acción de página (no la muestra)");

        CHECK(bot.ponerCara(pid, pg, 2, 0, "c1", false).empty(), "ponerCara sin celda crea el botón");
        f = bot.formularioCelda(pid, pg, 2, 0);
        CHECK(f["imagen"] == "medio:c1" && f["herramienta"] == "" && !f["al_tocar"].get<bool>(), "la cara, sin acción");
        CHECK(bot.setAlTocarCara(pid, pg, 2, 0, true).empty(), "al tocar: prender");
        f = bot.formularioCelda(pid, pg, 2, 0);
        CHECK(f["al_tocar"] == true && f["herramienta"] == "medio_poner" &&
                  json::parse(f["args"].get<std::string>())["path"] == "Caras/c1/medio.mp4",
              "la acción es medio_poner con el medio de la cara");
        CHECK(bot.ponerCara(pid, pg, 2, 0, "c2", false).empty() && !bot.formularioCelda(pid, pg, 2, 0)["al_tocar"].get<bool>(),
              "otra cara sin al_tocar: la acción queda, pero ya no es de esta cara");
        CHECK(bot.ponerCara(pid, pg, 2, 0, "c2", true).empty() && bot.formularioCelda(pid, pg, 2, 0)["al_tocar"] == true,
              "ponerCara con al_tocar (como botonera_cara)");
        CHECK(bot.setAlTocarCara(pid, pg, 2, 0, false).empty() && bot.formularioCelda(pid, pg, 2, 0)["herramienta"] == "",
              "al tocar: apagar saca la acción");
        std::string aviso;
        CHECK(bot.ponerCara(pid, pg, 3, 0, "cam", true, &aviso).empty() && !aviso.empty() &&
                  bot.formularioCelda(pid, pg, 3, 0)["herramienta"] == "",
              "un video de cámara: la cara sí, al aire no (con aviso)");
        CHECK(!bot.setAlTocarCara(pid, pg, 3, 0, true).empty(), "al tocar con una cara de cámara: error");
        CHECK(bot.quitarCara(pid, pg, 3, 0).empty() && bot.formularioCelda(pid, pg, 3, 0)["imagen"] == "", "quitarCara");

        CHECK(!bot.avisoPermisos(pid, {{"accion", {{"tipo", "herramienta"}, {"herramienta", "medio_poner"}}}}).empty(),
              "avisoPermisos: medio_poner no está en el perfil Director");
        bot.borrarPagina(pid, pg);
        bot.ganchos().herramientas = herrAntes;
    }

    // --- Muchas salidas (PLAN-SALIDAS, etapa 4) ------------------------------------
    {
        // Temas del bus por salida.
        CHECK(BusEventos::temaDeSalida("aire/tally", "cam2", false) == "aire/cam2/tally", "temaDeSalida: aire/tally de otra salida");
        CHECK(BusEventos::temaDeSalida("salida/stream", "cam2", false) == "salida/cam2/stream", "temaDeSalida: salida/stream");
        CHECK(BusEventos::temaDeSalida("aire/tally", "principal", true) == "aire/tally", "la por omisión: los temas de siempre");
        CHECK(BusEventos::temaDeSalida("audio/loudness", "cam2", false) == "audio/loudness", "lo que no es de una salida no cambia");
        CHECK(BusEventos::temaDeSalida("aire/dsk", "cam2", false) == "aire/cam2/dsk" &&
                  BusEventos::temaDeSalida("aire/medios", "cam2", false) == "aire/cam2/medios" &&
                  BusEventos::temaDeSalida("aire/tbar", "cam2", false) == "aire/cam2/tbar",
              "temaDeSalida: dsk, medios y T-bar de otra salida");
        CHECK(BusEventos::temaDeSalida("audio/nivel/Mic 1", "cam2", false) == "audio/cam2/nivel/Mic 1",
              "temaDeSalida: el fader de una fuente en otra salida");
        CHECK(BusEventos::temaDeSalida("audio/nivel/Mic 1", "principal", true) == "audio/nivel/Mic 1", "en la por omisión, el de siempre");
        CHECK(BusEventos::temaDeSalida("salida/{{salida}}/estado", "cam2", false) == "salida/cam2/estado", "{{salida}} en el tema");
        bus.publicarSalidas(json::array(
            {json{{"id", "principal"}, {"nombre", "Principal"}, {"color", "#c62828"}, {"por_omision", true},
                  {"escena", "Cámara 1"}, {"armada", "Cámara 2"}, {"transmitiendo", true}},
             json{{"id", "cam2"}, {"nombre", "Segunda"}, {"color", "#1565c0"}, {"escena", "Cámara 2"}, {"armada", "Cámara 1"},
                  {"grabando", true}}}));
        CHECK(bus.ultimo("aire/cam2/tally")["valor"]["programa"] == "Cámara 2", "aire/<salida>/tally");
        CHECK(bus.ultimo("salida/cam2/grabacion")["valor"]["activo"] == true, "salida/<salida>/grabacion");
        CHECK(bus.ultimo("salida/principal/estado")["valor"]["transmitiendo"] == true, "salida/<salida>/estado");
        CHECK(bus.ultimo("salidas")["valor"]["lista"].size() == 2, "la lista de salidas");
        bus.publicarSalidas(json::array(
            {json{{"id", "principal"}, {"nombre", "Principal"}, {"color", "#c62828"}, {"por_omision", true},
                  {"escena", "Cámara 1"}, {"armada", "Cámara 2"}, {"transmitiendo", true}},
             json{{"id", "cam2"}, {"nombre", "Segunda"}, {"color", "#1565c0"}, {"escena", "Cámara 2"}, {"armada", "Cámara 1"},
                  {"grabando", true}, {"tbar", {{"hacia", "Cámara 1"}, {"posicion", 0.5}}},
                  {"dsk", json::array({json{{"capa", 1}, {"visible", true}}})},
                  {"niveles", {{"Mic 1", {{"pico_db", -20}, {"volumen_db", -6}, {"mudo", false}}}}}}}));
        CHECK(bus.ultimo("aire/cam2/tbar")["valor"]["posicion"] == 0.5, "aire/<salida>/tbar");
        CHECK(bus.ultimo("aire/cam2/dsk")["valor"][0]["visible"] == true, "aire/<salida>/dsk");
        CHECK(bus.ultimo("audio/cam2/nivel/Mic 1")["valor"]["volumen_db"] == -6, "audio/<salida>/nivel/<fuente>");

        // Una botonera con dos salidas: cada aparato maneja la suya.
        Botonera::Ganchos g2 = g;
        g2.salidas = [] {
            return json::array({json{{"id", "principal"}, {"nombre", "Principal"}, {"color", "#c62828"}, {"por_omision", true}},
                                json{{"id", "cam2"}, {"nombre", "Segunda"}, {"color", "#1565c0"}, {"por_omision", false}}});
        };
        Botonera bs((dir.path() + "/salidas").toStdString(), bus, g2);
        const std::string pd = bs.perfiles()[0]["id"];
        int sx = -1, sy = -1;
        const json celdasDir = bs.perfil(pd)["paginas"][0]["celdas"]; // con nombre: iterar un temporal es UB
        for (const auto& c : celdasDir)
            if (c.value("accion", json::object()).value("tipo", std::string()) == "salida") {
                sx = c["x"];
                sy = c["y"];
            }
        CHECK(sx >= 0, "plantilla Director con varias salidas: la celda «Salida»");
        bs.registrarLocal("d1", "Deck A", "plugin", json::object());
        bs.registrarLocal("d2", "Deck B", "plugin", json::object());
        CHECK(bs.salidaDe("d1").empty() && bs.render("d1")["salida"]["id"] == "principal", "sin elegir: la por omisión");
        CHECK(bs.elegirSalida("d2", "Segunda").empty() && bs.salidaDe("d2") == "cam2", "elegir por nombre");
        CHECK(!bs.elegirSalida("d2", "no-hay").empty() && bs.salidaDe("d2") == "cam2", "una que no existe: error, queda la de antes");
        // El tally de cada aparato es el de SU salida: Cámara 1 al aire en la principal, Cámara 2 en la otra.
        bus.publicar("aire/tally", {{"programa", "Cámara 1"}, {"preview", "Cámara 2"}});
        const auto rojaEn = [&](const std::string& dev) {
            const json r = bs.render(dev);
            for (const auto& c : r["celdas"])
                if (c["fondo"] == "#c62828" && (c["texto"] == "Cámara 1" || c["texto"] == "Cámara 2"))
                    return c["texto"].get<std::string>();
            return std::string();
        };
        CHECK(rojaEn("d1") == "Cámara 1" && rojaEn("d2") == "Cámara 2", "el tally de cada aparato sigue a su salida");
        const json r2 = bs.render("d2");
        CHECK(r2["salida"]["color"] == "#1565c0" && r2["salidas"].size() == 2, "el estado lleva la salida (con su color) y la lista");
        bool nombre = false;
        for (const auto& c : r2["celdas"])
            if (c["texto"] == "Segunda" && c["fondo"] == "#1565c0") nombre = true;
        CHECK(nombre, "la celda «Salida» con el nombre y el color de la suya (superficie/salida)");
        // Lo que corre va con la salida del aparato.
        salidasLlamadas.clear();
        bs.entrada("d1", {{"type", "presionar"}, {"x", 0}, {"y", 0}});
        bs.entrada("d2", {{"type", "presionar"}, {"x", 0}, {"y", 0}});
        CHECK(salidasLlamadas.size() == 2 && salidasLlamadas[0].empty() && salidasLlamadas[1] == "cam2",
              "cada aparato corre con su salida");
        // La confirmación de aire es de la salida del aparato: sale solo cam2, así que el
        // TAKE de d1 (la principal) corre derecho y el de d2 pide confirmar.
        int tx = -1, ty = -1;
        for (const auto& c : celdasDir)
            if (c.value("accion", json::object()).value("herramienta", std::string()) == "preview_al_aire") {
                tx = c["x"];
                ty = c["y"];
            }
        CHECK(tx >= 0, "el Director tiene el TAKE");
        hayAire = true;
        aireSolo = "cam2";
        aireDe.clear();
        llamadas.clear();
        bs.entrada("d1", {{"type", "presionar"}, {"x", tx}, {"y", ty}});
        bs.entrada("d2", {{"type", "presionar"}, {"x", tx}, {"y", ty}});
        CHECK(aireDe.size() == 2 && aireDe[0].empty() && aireDe[1] == "cam2", "se pregunta con la salida de cada aparato");
        CHECK(llamadas.size() == 1 && llamadas[0].first == "preview_al_aire", "sin aire en su salida no pide confirmar; con aire, sí");
        hayAire = false;
        aireSolo.clear();
        // Cambiar de salida desde el aparato: el mensaje y la celda «Salida» (siguiente, da la vuelta).
        bs.entrada("d2", {{"type", "salida"}, {"id", ""}});
        CHECK(bs.salidaDe("d2").empty(), "{type: salida, id: \"\"}: vuelve a la del perfil");
        bs.entrada("d1", {{"type", "presionar"}, {"x", sx}, {"y", sy}});
        CHECK(bs.salidaDe("d1") == "cam2", "la celda «Salida» pasa a la siguiente");
        bs.entrada("d1", {{"type", "presionar"}, {"x", sx}, {"y", sy}});
        CHECK(bs.salidaDe("d1") == "principal", "y da la vuelta");
        // La salida del perfil: la de los aparatos que no eligieron.
        CHECK(bs.editarPerfil(pd, {{"salida", "cam2"}}, false).empty() && bs.salidaDe("d2") == "cam2", "salida del perfil");
        CHECK(!bs.editarPerfil(pd, {{"salida", "no-hay"}}, true).empty(), "perfil con una salida que no existe: error");
        CHECK(bs.perfiles()[0]["salida"] == "cam2", "perfiles() dice la salida");
        const std::string pg0 = bs.perfil(pd)["paginas"][0]["id"];
        CHECK(!bs.setCelda(pd, pg0, {{"x", 0}, {"y", 0}, {"accion", {{"tipo", "salida"}, {"ir_a", "nada"}}}}).empty(),
              "acción salida con un id que no existe: no se acepta");
        // Plantilla «Salidas»: una celda por salida, y el perfil con su salida.
        std::string ps;
        CHECK(bs.crearPerfil({{"nombre", "Salidas"}, {"salida", "cam2"}}, "salidas", &ps).empty() && bs.perfil(ps)["salida"] == "cam2",
              "plantilla Salidas, con la salida del perfil");
        // Plantilla «Línea de tiempo» (L8): GO, HOLD… y el feedback de linea/<su salida>/estado.
        std::string pl;
        CHECK(bs.crearPerfil({{"nombre", "Línea"}, {"salida", "cam2"}}, "linea", &pl).empty(), "plantilla Línea de tiempo");
        {
            bus.publicar("linea/cam2/estado", {{"estado", "corriendo"}, {"hold", true}, {"alAire", {{"paso", "p1"}, {"nombre", "Apertura"}}},
                                               {"proximo", nullptr}, {"overUnderSeg", 42}});
            const json pg = bs.perfil(pl)["paginas"][0];
            const json cs = bs.celdas(pl, pg.value("id", std::string()));
            bool go = false, aire = false, hold = false;
            for (const json& c : cs) {
                const json acc = c.value("accion", json::object());
                go = go || acc.value("herramienta", std::string()) == "linea_go";
                const json r = c.value("render", json::object());
                aire = aire || r.value("texto", std::string()).find("Apertura") != std::string::npos;
                hold = hold || (acc.value("herramienta", std::string()) == "linea_hold" && r.value("activo", false));
            }
            CHECK(go && aire && hold, "Línea: el GO, lo que está al aire y el HOLD prendido, de la salida del perfil");
        }
        // Un aparato local guarda su salida aunque se vaya y vuelva (y tras reiniciar la app).
        bs.elegirSalida("d1", "cam2");
        bs.quitarLocal("d1");
        bs.registrarLocal("d1", "Deck A", "plugin", json::object());
        CHECK(bs.salidaDe("d1") == "cam2", "el local vuelve con su salida");
        Botonera bs2((dir.path() + "/salidas").toStdString(), bus, g2);
        bs2.registrarLocal("d1", "Deck A", "plugin", json::object());
        CHECK(bs2.salidaDe("d1") == "cam2", "y también tras reiniciar");
        // Una salida que se borró: el aparato vuelve a la por omisión.
        bs2.ganchos().salidas = [] { return json::array({json{{"id", "principal"}, {"nombre", "Principal"}, {"por_omision", true}}}); };
        CHECK(bs2.salidaDe("d1").empty(), "salida borrada: la por omisión");
        // Los temas de una salida borrada se olvidan.
        bus.publicarSalidas(json::array({json{{"id", "principal"}, {"nombre", "Principal"}, {"por_omision", true}}}));
        CHECK(bus.ultimo("aire/cam2/tally").is_null() && bus.ultimo("audio/cam2/nivel/Mic 1").is_null(),
              "los temas de una salida borrada se olvidan");
    }

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
