// ============================================================================
// src/app/tests/plano_control_foto.cpp — test_plano_control, 7 y 9: la foto y las copias
//
// Las herramientas de la foto de verdad (McpFoto) con un estado sintético, y lo que solo
// sabe la interfaz (por copia) con las alarmas que no la esperan. Ver test_plano_control.cpp
// (qué prueba cada número) y PlanoControlPrueba.hpp (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "agent/EntradasAlarmas.hpp"
#include "diag/VigiaBucle.hpp"
#include "emision/DocumentoAire.hpp"
#include "mcp/McpFoto.hpp"
#include "persist/ColaEscritura.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace ba::app::pruebas::plano {

using namespace ba::app::mcp;

void probarFoto(Banco& b, const Resto& resto) {
    [[maybe_unused]] auto& app = b.app;
    [[maybe_unused]] const QThread* interfaz = b.interfaz;
    [[maybe_unused]] auto& control = b.control;
    [[maybe_unused]] auto& motor = b.motor;
    [[maybe_unused]] auto& latido = b.latido;
    [[maybe_unused]] auto& plano = b.plano;
    [[maybe_unused]] auto& turno = b.turno;
    [[maybe_unused]] auto& core = b.core;
    [[maybe_unused]] auto& pool = b.pool;
    [[maybe_unused]] const std::string& token = b.token;
    [[maybe_unused]] auto& http = b.http;

    // --- 7. Las herramientas de la foto, con un estado sintético ------------------------
    const fs::path datos = carpetaTemporal("ba-test-plano-control");
    fs::remove_all(datos);
    for (const char* d : {"Config", "Runtime", "Logs"}) fs::create_directories(datos / d);
    std::ofstream(datos / "Config" / "salidas.json") << R"({"salidas":[{"id":"principal","nombre":"Principal"}]})";
    std::ofstream(datos / "Logs" / "app.log") << "linea uno\nlinea dos\n";
    ba::app::salidas::DocumentoAire doc(datos / "Runtime");
    {
        std::map<std::string, ba::app::salidas::Aire> s;
        s["principal"].escena = "Plano general";
        s["principal"].transmitiendo = true;
        s["principal"].placa.placa = true;
        s["principal"].placa.texto = "Cuarto intermedio";
        doc.ponerSalidas(s, false);
        doc.volcar();
    }
    CHECK(ba::persist::cola().vaciar(), "7: el documento de aire está escrito");
    // Una fuente leída en la interfaz o en el control sería una carrera: se anota dónde corre cada una.
    std::atomic<int> fuentesMalLeidas{0}, fuentesLeidas{0};
    const auto leida = [&]() {
        ++fuentesLeidas;
        if (QThread::currentThread() == interfaz || QThread::currentThread() == plano.hilo()) ++fuentesMalLeidas;
    };
    std::atomic<int> copiasStatus{0}, copiasSalidas{0};
    std::atomic<bool> copiaFueraDeLaInterfaz{false};
    ba::app::mcp::FuentesDeFoto f;
    f.aire = [&]() {
        leida();
        return ba::app::salidas::aJson(doc.foto());
    };
    f.hayAire = [&]() {
        leida();
        return doc.hayAire();
    };
    f.latido = [&]() {
        leida();
        return json{{"vivo", true}};
    };
    f.modoSeguro = [&]() {
        leida();
        return json{{"activo", false}};
    };
    f.salidasMotor = [&]() {
        leida();
        const json p{{"andando", true}, {"perdidos", 3}, {"cuadros", 9000}, {"reconectando", false}};
        return json{{"principal", p},
                    {"grabacion", {{"activa", false}}},
                    {"salidas", {{"principal", {{"principal", p}, {"grabacion", {{"activa", false}}}}}}}};
    };
    f.salidasPublicadas = [&]() {
        leida();
        return json::array({{{"id", "principal"}, {"nombre", "Principal"}, {"por_omision", true}}});
    };
    f.recursos = [&](int, int) {
        leida();
        return json{{"muestras", 5}, {"cpu", {{"valor", 12.5}}}, {"termica", {{"nivel", "normal"}}}};
    };
    f.servicio = [&]() {
        leida();
        return json{{"transporte", json::array({{{"canal", "programa"}}})}, {"termica", {{"pasos", 0}}}};
    };
    f.derivaPlacas = [&]() {
        leida();
        return json::array();
    };
    f.elementosGst = [&]() {
        leida();
        return json::array({{{"elemento", "compositor"}, {"esta", true}}});
    };
    f.temas = [&](const std::string&) {
        leida();
        return json::array({{{"tema", "aire/escena"}, {"valor", "Plano general"}}});
    };
    f.ultimoTema = [&](const std::string& t) {
        leida();
        return t == "aire/escena" ? json{{"tema", t}, {"valor", "Plano general"}} : json();
    };
    f.logs = datos / "Logs";
    // Lo de la interfaz (como en la app: lo que solo ella sabe; la armada de cada salida).
    f.statusInterfaz = [&]() {
        ++copiasStatus;
        if (QThread::currentThread() != interfaz) copiaFueraDeLaInterfaz = true;
        return json{{"currentScene", "Plano general"},
                    {"streaming", true},
                    {"room", {{"online", true}}},
                    {"salidas", json::array({{{"id", "principal"}, {"armada", "Entrevista"}}})}};
    };
    f.salidasInterfaz = [&]() {
        ++copiasSalidas;
        if (QThread::currentThread() != interfaz) copiaFueraDeLaInterfaz = true;
        return json{{"tope", 4}, {"salidas", json::array({{{"id", "principal"}, {"armada", "Entrevista"}}})}};
    };
    f.esperaInterfazMs = 100;
    // Lo que la interfaz tenía y ahora da su dueño sin ella: la sala (B), el modo en vivo
    // y la caída (la foto de AppViewModel), los archivos del sistema, los notificadores.
    f.sala = [&](const std::string& ruta) -> std::optional<json> {
        leida();
        if (ruta == "/api/v1/room")
            return json{{"room", "Sala"}, {"participants", json::array({{{"id", "p1"}, {"name", "Ana"}}})}, {"chat", json::array()}};
        if (ruta == "/api/v1/network") return json{{"https", true}};
        if (ruta == "/api/v1/gateway") return json{{"uso", json::object()}, {"fronteras", json::array()}};
        return std::nullopt;
    };
    f.modoVivo = [&]() {
        leida();
        return json{{"en_vivo", true}, {"activo", true}};
    };
    f.caidas = [&](int) {
        leida();
        return json::array({{{"causa", "colgado"}}});
    };
    f.caidaReciente = [&]() {
        leida();
        return json{{"causa", "colgado"}, {"habia_aire", true}};
    };
    f.sistema = [&]() {
        leida();
        return json{{"dataRoot", datos.string()}, {"credentials", json::array()}};
    };
    f.notificadores = [&]() {
        leida();
        return json::array({{{"id", "n-1"}, {"tipo", "webhook"}}});
    };
    core.setHayAire([&]() { return doc.hayAire(); });
    const std::size_t antes = core.tools().size();
    ba::app::mcp::registrarHerramientasDeFoto(core, f);
    bool todasDeLaFoto = core.tools().size() > antes;
    for (std::size_t i = antes; i < core.tools().size(); ++i)
        todasDeLaFoto = todasDeLaFoto && core.tools()[i].clase == Clase::Lectura && core.tools()[i].hilo == Hilo::Foto;
    CHECK(todasDeLaFoto, "7: todas son de lectura y de la foto");

    const auto archivosAntes = fotoDeArchivos(datos);
    const std::string aireAntes = ba::app::salidas::aJson(doc.foto()).dump();
    const std::uint64_t escriturasAntes = doc.escrituras();
    struct Pedido {
        std::string herramienta;
        json args;
        std::string resultado;
        double ms = -1;
    };
    std::vector<Pedido> pedidos;
    for (std::size_t i = antes; i < core.tools().size(); ++i) {
        Pedido p{core.tools()[i].name, json::object(), {}, -1};
        if (p.herramienta == "eventos_ultimo") p.args = {{"tema", "aire/escena"}};
        if (p.herramienta == "sistema_recursos") p.args = {{"historial_s", 30}};
        if (p.herramienta == "log_leer") p.args = {{"minimo", "traza"}};
        pedidos.push_back(std::move(p));
    }
    pedidos.push_back({"status", json::object(), {}, -1}); // otra vez: la copia se pide una sola
    std::thread clienteFoto([&]() {
        for (std::size_t i = 0; i < pedidos.size(); ++i) {
            const auto t = std::chrono::steady_clock::now();
            pedidos[i].resultado = llamar(http.port(), token, pedidos[i].herramienta, 100 + static_cast<int>(i), pedidos[i].args);
            pedidos[i].ms = msDesde(t);
        }
    });
    trabarInterfaz(static_cast<int>(pedidos.size()) * 150 + 1500);
    clienteFoto.join();
    const int copiasEnLaTraba = copiasStatus.load() + copiasSalidas.load();
    bool todasContestaron = true, todasEnElActo = true;
    double peor = 0;
    for (const auto& p : pedidos) {
        const json r = json::parse(p.resultado, nullptr, false);
        const bool bien = r.is_object() && r.contains("result") && !r["result"].value("isError", true);
        if (!bien) std::printf("  %s: %s\n", p.herramienta.c_str(), p.resultado.substr(0, 300).c_str());
        todasContestaron = todasContestaron && bien;
        todasEnElActo = todasEnElActo && p.ms < 600;
        peor = std::max(peor, p.ms);
    }
    std::printf("  medido: %zu pedidos de la foto con la interfaz trabada, el peor %.1f ms; %d fuentes leídas\n",
                pedidos.size(), peor, fuentesLeidas.load());
    CHECK(todasContestaron, "7: con la interfaz trabada, todas contestan sin error");
    CHECK_TIEMPO(todasEnElActo, "7: con la interfaz trabada, todas contestan en el acto (< 600 ms)");
    CHECK(fuentesLeidas.load() > 0 && fuentesMalLeidas.load() == 0, "7: ninguna fuente se leyó en la interfaz ni en el control");
    CHECK(copiasEnLaTraba == 0, "7: la copia de la interfaz no se hizo durante la traba");
    {
        const json r = json::parse(pedidos.back().resultado, nullptr, false);
        const json st = r.is_object() ? r["result"].value("structuredContent", json::object()) : json::object();
        CHECK(!st.value("interfaz", json::object()).value("al_dia", true) && st.value("streaming", false) &&
                  st.value("currentScene", std::string()) == "Plano general" && st.value("droppedFrames", 0) == 3 &&
                  st.value("latido", json::object()).value("vivo", false),
              "7: status trabado: lo que sale, al día (documento y foto de salidas); dice que lo demás es viejo");
        const json sal = st.value("salidas", json::array());
        CHECK(!sal.empty() && sal[0].value("placa", json::object()).value("placa", false) && sal[0].value("streaming", false),
              "7: status trabado: cada salida con su placa y lo que sale");
    }
    {
        // Las que leían objetos de la interfaz: con ella trabada, lo de su dueño.
        std::map<std::string, json> r;
        for (const auto& p : pedidos) {
            const json j = json::parse(p.resultado, nullptr, false);
            if (j.is_object()) r[p.herramienta] = j["result"].value("structuredContent", json::object());
        }
        CHECK(r["room_status"].value("participants", json::array()).size() == 1 && r["network_status"].value("https", false) &&
                  r["modo_vivo_estado"].value("en_vivo", false) && r["supervisor_caidas"].value("recien", json::object()).value("habia_aire", false) &&
                  r["system_info"].contains("dataRoot") && r["notificador_listar"].value("notificadores", json::array()).size() == 1,
              "7: room_status, network_status, modo_vivo_estado, supervisor_caidas, system_info y notificador_listar, trabada, con lo de su dueño");
    }
    atender(300); // la interfaz vuelve: la copia pedida se hace (una por herramienta)
    CHECK(copiasStatus.load() == 1 && copiasSalidas.load() == 1 && !copiaFueraDeLaInterfaz.load(),
          "7: la copia se pidió una sola vez y se hizo en la interfaz al volver");
    {
        std::string r;
        std::thread otraVez([&]() { r = llamar(http.port(), token, "salidas_listar", 300); });
        while (r.empty()) atender(10);
        otraVez.join();
        const json j = json::parse(r, nullptr, false);
        const json st = j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        const json sal = st.value("salidas", json::array());
        CHECK(st.value("interfaz", json::object()).value("al_dia", false) && st.value("tope", 0) == 4 && !sal.empty() &&
                  sal[0].value("armada", std::string()) == "Entrevista",
              "7: con la interfaz libre, la copia está al día");
    }
    CHECK(ba::persist::cola().vaciar(), "7: nada pendiente de escribir");
    CHECK(fotoDeArchivos(datos) == archivosAntes, "7: no cambió ningún archivo de Config/, Runtime/ ni Logs/");
    CHECK(ba::app::salidas::aJson(doc.foto()).dump() == aireAntes && doc.escrituras() == escriturasAntes,
          "7: el documento de aire quedó igual (ni se puso ni se escribió nada)");
    fs::remove_all(datos);

    // --- 9. Lo que solo sabe la interfaz, por copia; y las alarmas que no la esperan ------
    // (a) Una de lectura cuyo dato es SOLO de la interfaz (como linea_estado, linea_ver,
    // stream_salud, monitors): copiarDeLaInterfaz la pasa al pool con una copia por
    // argumentos. Libre: al día. Trabada: la última de esos argumentos, con su edad, en
    // el acto; la de unos argumentos que nunca se copiaron espera la primera vez y dice
    // que no hay copia. Las copias se hacen en la interfaz.
    std::atomic<int> copiasLinea{0};
    std::atomic<bool> copiaLineaFuera{false};
    std::atomic<int> pasoDeLaLinea{1};
    {
        Tool t;
        t.name = "linea_sintetica";
        t.clase = Clase::Lectura;
        copiarDeLaInterfaz(core, t,
                           [&](const json& a) -> LeidoEnLaInterfaz {
                               ++copiasLinea;
                               if (QThread::currentThread() != interfaz) copiaLineaFuera = true;
                               if (a.value("salida", std::string()) == "nadie") return {{}, "No hay esa salida en la línea."};
                               return {json{{"salida", a.value("salida", std::string("principal"))}, {"paso", pasoDeLaLinea.load()}}, {}};
                           },
                           EsperaDeLaCopia{100, 300});
        core.addTool(std::move(t));
    }
    CHECK(core.findTool("linea_sintetica") != nullptr && core.findTool("linea_sintetica")->hilo == Hilo::Foto,
          "9a: la de la copia corre en el pool");
    const auto llamarLibre = [&](const json& args, int id) {
        std::string r;
        std::thread c([&]() { r = llamar(http.port(), token, "linea_sintetica", id, args); });
        while (r.empty()) atender(10);
        c.join();
        return json::parse(r, nullptr, false);
    };
    {
        const json j = llamarLibre(json::object(), 900);
        const json st = j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        CHECK(st.value("paso", 0) == 1 && st.value("interfaz", json::object()).value("al_dia", false),
              "9a: con la interfaz libre, la copia al día");
        const json e = llamarLibre({{"salida", "nadie"}}, 901);
        CHECK(e.is_object() && e["result"].value("isError", false), "9a: el error de la herramienta llega como error");
    }
    pasoDeLaLinea = 2; // cambia en la interfaz, que se traba antes de copiarlo
    {
        std::string rVieja, rNueva;
        double msVieja = -1, msNueva = -1;
        const int antes = copiasLinea.load();
        std::thread c([&]() {
            auto t = std::chrono::steady_clock::now();
            rVieja = llamar(http.port(), token, "linea_sintetica", 902, json::object());
            msVieja = msDesde(t);
            t = std::chrono::steady_clock::now();
            rNueva = llamar(http.port(), token, "linea_sintetica", 903, {{"salida", "otra"}});
            msNueva = msDesde(t);
        });
        trabarInterfaz(1500);
        c.join();
        const int enLaTraba = copiasLinea.load() - antes;
        const json jv = json::parse(rVieja, nullptr, false);
        const json sv = jv.is_object() ? jv["result"].value("structuredContent", json::object()) : json::object();
        std::printf("  medido: copia con la interfaz trabada %.1f ms (espera 100); sin copia previa %.1f ms (espera la primera 300)\n",
                    msVieja, msNueva);
        CHECK(jv.is_object() && !jv["result"].value("isError", true) && sv.value("paso", 0) == 1 &&
                  !sv.value("interfaz", json::object()).value("al_dia", true) &&
                  sv.value("interfaz", json::object()).value("edad_ms", -1) >= 0 && msVieja < 400,
              "9a: trabada: la última copia, con su edad, en el acto");
        const json jn = json::parse(rNueva, nullptr, false);
        CHECK(jn.is_object() && jn["result"].value("isError", false) &&
                  !jn["result"].value("structuredContent", json::object()).value("interfaz", json::object()).value("al_dia", true) &&
                  msNueva >= 250 && msNueva < 900,
              "9a: trabada y sin copia de esos argumentos: espera la primera vez y lo dice");
        CHECK(enLaTraba == 0, "9a: nada se copió durante la traba");
        atender(300);
        CHECK(copiasLinea.load() - antes == 2 && !copiaLineaFuera.load(), "9a: al volver, una copia por argumentos, en la interfaz");
        const json j = llamarLibre(json::object(), 904);
        CHECK(j.is_object() && j["result"].value("structuredContent", json::object()).value("paso", 0) == 2,
              "9a: y la siguiente, al día");
    }

    // (b) Las alarmas de la sala, la energía y el disco con la interfaz trabada. Como en
    // la app (AlarmasViewModel): la sala viene de la foto de la interfaz y, si se queda
    // quieta, de B (el respaldo); la energía y el disco, directas (se miden en el vigía).
    std::mutex bMu;
    bool anaConectada = true;
    std::atomic<bool> enBateria{false};
    std::atomic<double> libreGb{200.0};
    std::atomic<int> preguntasAB{0}, preguntasEnLaInterfaz{0};
    const auto sala = [&]() {
        const std::lock_guard<std::mutex> l(bMu);
        return json{{"serverNowMs", 0},
                    {"participants", json::array({{{"id", "p1"}, {"name", "Ana"}, {"state", "admitted"}, {"connected", anaConectada}}})},
                    {"chat", json::array()}};
    };
    ba::app::agent::AlertInputs deLaInterfaz;
    deLaInterfaz.room = sala; // la interfaz la tiene de los eventos de B: la misma
    ba::app::agent::AlertInputs directas;
    directas.status = []() {
        ba::app::agent::AlertInputs::Status st;
        st.recording = true;
        return st;
    };
    directas.energia = [&]() {
        return json{{"en_bateria", enBateria.load()}, {"porcentaje", 80}, {"minutos", 60}, {"fuente", "ups"}, {"accion", "avisar"}};
    };
    directas.disco = [&]() { return json{{"libre_gb", libreGb.load()}, {"horas", -1}}; };
    ba::app::agent::AlertInputs respaldo;
    respaldo.room = [&]() {
        ++preguntasAB;
        if (QThread::currentThread() == interfaz) ++preguntasEnLaInterfaz;
        return sala();
    };
    // El latido de la interfaz, como en la app (ui/VigiaInterfaz): un temporizador de 50 ms
    // en su hilo da la vuelta del vigía de su bucle. La foto NO se copia con un reloj: solo
    // cuando lo que la da cambia (acá, al empezar).
    ba::diag::VigiaBucle bucleUi({}, 50, 1000);
    bucleUi.alAvisar([](const std::string&) {});
    QTimer latidoUi;
    QObject::connect(&latidoUi, &QTimer::timeout, [&bucleUi]() { bucleUi.tic(); });
    latidoUi.start(50);
    bucleUi.tic();
    ba::app::agent::EntradasAlarmas entradas(deLaInterfaz, directas, respaldo, [&bucleUi]() { return bucleUi.edadMs(); },
                                             500);
    entradas.refrescar();
    ba::app::agent::AlertEngine vigia9(entradas.paraElVigia(), "");
    for (const std::string tipo : {"guest_drop", "energia", "disk"})
        vigia9.create({{"type", tipo}, {"name", tipo}, {"params", tipo == "disk" ? json{{"minGb", 5.0}} : json::object()}});
    std::mutex disparosMu;
    std::map<std::string, const QThread*> disparos9;
    QObject::connect(
        &vigia9, &ba::app::agent::AlertEngine::fired, &vigia9,
        [&](const json& ev) {
            const std::lock_guard<std::mutex> l(disparosMu);
            disparos9[ev.value("type", std::string())] = QThread::currentThread();
        },
        Qt::DirectConnection);
    vigia9.start(100);
    plano.adoptar(&vigia9);
    // La foto no se vuelve a copiar (nada cambió): pasa más que `quietaMs` y, con la
    // interfaz latiendo, el vigía sigue con la foto sin preguntarle a B.
    atender(1200); // el vigía se entera de cómo está todo (Ana conectada)
    const int preguntasAntes = preguntasAB.load();
    {
        const std::lock_guard<std::mutex> l(disparosMu);
        CHECK(disparos9.empty() && preguntasAntes == 0,
              "9b: con la interfaz andando (la foto vieja, sin cambios), nada que avisar y nada que preguntarle a B");
    }
    {
        // Se traba la interfaz; recién ahí Ana se cae, se corta la luz y se llena el disco.
        std::thread cambios([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            {
                const std::lock_guard<std::mutex> l(bMu);
                anaConectada = false;
            }
            enBateria = true;
            libreGb = 1.0;
        });
        trabarInterfaz(2500);
        cambios.join();
    }
    {
        const std::lock_guard<std::mutex> l(disparosMu);
        for (const std::string tipo : {"guest_drop", "energia", "disk"}) {
            const bool salio = disparos9.count(tipo) && disparos9[tipo] == plano.hilo();
            CHECK(salio, ("9b: con la interfaz trabada, la alarma «" + tipo + "» sale en el hilo de control").c_str());
        }
    }
    CHECK(preguntasAB.load() > 0 && preguntasEnLaInterfaz.load() == 0,
          "9b: la sala se le preguntó a B desde el vigía, solo mientras la interfaz no latía");
    {
        // Al volver la interfaz, deja de preguntarle a B (aunque la foto siga sin copiarse).
        atender(300);
        const int alVolver = preguntasAB.load();
        atender(800);
        CHECK(preguntasAB.load() == alVolver, "9b: con la interfaz latiendo de nuevo, no se le pregunta más a B");
    }
    latidoUi.stop();

    // (c) La sala DIRECTA, como en la app desde 2026-11-02: la foto la arma el lector de la
    // sala en su hilo (ConferenceClient::fotoSalaJson) y no pasa por la interfaz. Con su
    // número quieto, el vigía no la vuelve a leer ni a recorrer (O(invitados) cada vuelta en
    // el control); con una foto nueva y la interfaz trabada, la lee y cuenta la novedad. (Vive
    // hasta el final, como vigia9: adoptado por el control, vuelve al detener el plano.)
    std::atomic<std::uint64_t> numero{1};
    std::atomic<int> lecturas{0}, novedades9c{0};
    std::atomic<bool> conectada{true};
    ba::app::agent::AlertInputs ui9c; // nada de la sala en la interfaz
    ba::app::agent::AlertInputs dir9c;
    dir9c.room = [&]() {
        ++lecturas;
        return json{{"participants",
                     json::array({{{"id", "p1"}, {"name", "Ana"}, {"state", "admitted"}, {"connected", conectada.load()}}})}};
    };
    dir9c.roomNumero = [&]() { return numero.load(); };
    ba::app::agent::EntradasAlarmas e9c(ui9c, dir9c);
    ba::app::agent::AlertEngine v9c(e9c.paraElVigia(), ""); // sin reglas: solo las novedades leen la sala
    QObject::connect(&v9c, &ba::app::agent::AlertEngine::novedad, &v9c, [&](const QString&) { ++novedades9c; },
                     Qt::DirectConnection);
    v9c.start(100);
    plano.adoptar(&v9c);
    atender(600);
    const int quieta = lecturas.load();
    atender(600);
    CHECK(quieta > 0 && lecturas.load() == quieta, "9c: con la misma foto de la sala, el vigía no la vuelve a leer");
    conectada = false;
    ++numero;
    trabarInterfaz(800);
    CHECK(lecturas.load() > quieta && novedades9c.load() > 0,
          "9c: foto nueva con la interfaz trabada: el control la lee y cuenta que Ana se desconectó");


    b.vigia9 = &vigia9;
    resto();
}

} // namespace ba::app::pruebas::plano
