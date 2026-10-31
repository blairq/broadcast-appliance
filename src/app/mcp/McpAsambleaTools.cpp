// ============================================================================
// src/app/mcp/McpAsambleaTools.cpp — revisar y preparar una sesión de asamblea
//
// asamblea_revisar: qué falta para sesionar y cómo se arregla (ver McpAsamblea.hpp).
// asamblea_escenas_base: propone las escenas de una asamblea armadas con las
// cámaras y entradas que hay (plano general, una por cámara de bancas, el remoto
// con la palabra, los debates); con `crear: true` las crea y las deja en la
// configuración del debate. Nunca pisa una escena que ya existe.
// ============================================================================

#include "mcp/McpAsamblea.hpp"

#include "arranque/AppRuntime.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/Debate.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/SesionRemota.hpp"
#include "emision/BroadcastEngine.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ptz/ControlCamaras.hpp"
#include "ui/StudioViewModel.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace ba::app::mcp {

namespace {
const char* const kRemoto1 = "Remoto con la palabra";
// La fuente «Banca con la palabra» (BroadcastEngine::kTipoBanca), ranuras 1 y 2.
const char* const kBanca1 = "Banca con la palabra";
const char* const kBanca2 = "Banca con la palabra 2";
const char* const kRemoto2 = "Remoto con la palabra 2";

bool tiene(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

/// Las escenas que muestran una fuente.
std::vector<std::string> escenasCon(BroadcastEngine& eng, const std::string& fuente) {
    std::vector<std::string> out;
    for (const auto& sc : eng.sceneNames())
        if (tiene(eng.sceneSources(sc), fuente)) out.push_back(sc);
    return out;
}

/// Cámara -> cuántas bancas la usan (del preset de cada integrante).
std::map<std::string, int> camarasDeBancas(const asamblea::Cuerpo& cuerpo) {
    std::map<std::string, int> out;
    for (const auto& i : cuerpo.integrantes()) {
        const std::string c = i.value("preset_ptz", nlohmann::json::object()).value("camara", std::string());
        if (!c.empty() && !i.value("banca", std::string()).empty()) ++out[c];
    }
    return out;
}
} // namespace

nlohmann::json revisarAsamblea(StudioViewModel& vm, AppRuntime& rt) {
    using json = nlohmann::json;
    json faltas = json::array(), avisos = json::array();
    const auto falta = [&](const std::string& que, const std::string& detalle, const std::string& como) {
        faltas.push_back({{"que", que}, {"detalle", detalle}, {"como", como}});
    };
    const auto aviso = [&](const std::string& que, const std::string& detalle, const std::string& como) {
        avisos.push_back({{"que", que}, {"detalle", detalle}, {"como", como}});
    };
    BroadcastEngine& eng = rt.engine();
    const auto& cuerpo = vm.cuerpo().cuerpo();
    const json integrantes = cuerpo.integrantes();

    // 1. El Cuerpo.
    int conBanca = 0;
    for (const auto& i : integrantes)
        if (!i.value("banca", std::string()).empty()) ++conBanca;
    if (integrantes.empty())
        falta("cuerpo", ba::i18n::traducir("mcp", "No hay integrantes cargados."),
              ba::i18n::traducir("mcp", "Cargalos con cuerpo_importar (planilla) o integrante_editar, cada uno con su banca."));
    else if (conBanca < static_cast<int>(integrantes.size()))
        aviso("cuerpo",
              ba::i18n::traducir("mcp", "%n integrante sin banca: no aparece en el recinto.", nullptr,
                                 static_cast<int>(integrantes.size()) - conBanca),
              "integrante_editar {id, banca}.");

    // 2. El recinto.
    const json rec = cuerpo.recinto(true);
    if (rec.value("filas", 0) == 0 && conBanca > 0)
        falta("recinto", ba::i18n::traducir("mcp", "El recinto no tiene grilla: las bancas no se ven para tocarlas."),
              ba::i18n::traducir("mcp", "recinto_editar {filas, columnas, auto: true} (o Asamblea → Recinto → «Acomodar solas»)."));
    else if (!rec.value("sin_lugar", json::array()).empty())
        aviso("recinto",
              ba::i18n::traducir("mcp", "%n banca sin lugar en la grilla.", nullptr,
                                 static_cast<int>(rec["sin_lugar"].size())),
              "recinto_editar {mover: ...}.");

    // 3. Las cámaras de las bancas (preset PTZ de cada una).
    std::vector<std::string> sinPreset, camaraRara, posicionRara;
    auto* cc = qobject_cast<ptz::ControlCamaras*>(vm.camaras());
    std::map<std::string, std::set<std::string>> posiciones;
    for (const auto& i : integrantes) {
        const std::string banca = i.value("banca", std::string());
        if (banca.empty()) continue;
        const json p = i.value("preset_ptz", json::object());
        const std::string c = p.value("camara", std::string()), pos = p.value("posicion", std::string());
        if (c.empty() || pos.empty()) {
            sinPreset.push_back(banca);
            continue;
        }
        if (!eng.sourceExists(c)) {
            camaraRara.push_back(banca + " (" + c + ")");
            continue;
        }
        if (cc != nullptr && !posiciones.count(c))
            for (const QVariant& v : cc->posiciones(QString::fromStdString(c))) {
                const QVariantMap m = v.toMap();
                posiciones[c].insert(m.value("id").toString().toStdString());
                posiciones[c].insert(m.value("nombre").toString().toStdString());
            }
        if (cc != nullptr && !posiciones[c].count(pos)) posicionRara.push_back(banca + " (" + c + "/" + pos + ")");
    }
    if (!sinPreset.empty())
        falta("camaras",
              ba::i18n::traducir("mcp", "%n banca sin toma de cámara: al darle la palabra no se muestra a nadie.",
                                 nullptr, static_cast<int>(sinPreset.size())),
              ba::i18n::traducir("mcp", "Apuntá la PTZ a cada banca, guardá la posición (ptz_posicion_crear) y asociala a la banca "
              "(banca_toma {banca, camara, posicion})."));
    if (!camaraRara.empty())
        falta("camaras",
              ba::i18n::llenar(camaraRara.size() > 1 ? ba::i18n::traducir("mcp", "Bancas con una cámara que no existe: %1 y otras.")
                                      : ba::i18n::traducir("mcp", "Bancas con una cámara que no existe: %1."),
                {camaraRara.front()}),
              ba::i18n::traducir("mcp", "Corregí la cámara en el Cuerpo o agregá esa entrada."));
    // Muchas bancas en la MISMA posición: al darle la palabra a una no se la encuadra a ella.
    {
        std::map<std::string, std::vector<std::string>> porPosicion;
        for (const auto& i : integrantes) {
            const json p = i.value("preset_ptz", json::object());
            const std::string c = p.value("camara", std::string()), pos = p.value("posicion", std::string());
            if (!c.empty() && !pos.empty() && !i.value("banca", std::string()).empty())
                porPosicion[c + " / " + pos].push_back(i.value("banca", std::string()));
        }
        for (const auto& [cp, bancas] : porPosicion)
            if (bancas.size() > 2)
                aviso("camaras",
                      ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 bancas usan la misma posición («%2»): al darle la palabra a una no se la encuadra a "
                          "ella, se ve el plano entero."),
                        {std::to_string(bancas.size()), cp}),
                      ba::i18n::traducir("mcp", "Una posición por banca (o por grupo chico de bancas vecinas): apuntá la PTZ, ptz_posicion_crear "
                      "{entrada, nombre: \"Banca N\"} y banca_toma {banca, camara, posicion}."));
    }
    if (!posicionRara.empty())
        aviso("camaras",
              ba::i18n::llenar(posicionRara.size() > 1 ? ba::i18n::traducir("mcp", "Bancas con una posición PTZ que la cámara no tiene: %1 y otras.")
                                        : ba::i18n::traducir("mcp", "Bancas con una posición PTZ que la cámara no tiene: %1."),
                {posicionRara.front()}),
              ba::i18n::traducir("mcp", "ptz_posicion_crear en esa cámara con ese nombre."));

    // 4. LAS ESCENAS: la del orador (la fuente «Banca con la palabra»; si no, cada cámara de
    // bancas en alguna escena), el remoto, el debate.
    const auto camaras = camarasDeBancas(cuerpo);
    if (!eng.escenaDeRanura(1).empty()) {
        // Dar la palabra arma la escena con la ranura 1: no hace falta una por cámara.
    } else if (!camaras.empty()) {
        aviso("escenas", ba::i18n::traducir("mcp", "No hay una escena con la fuente «Banca con la palabra»: dar la palabra arma la primera escena "
                         "que tenga esa cámara (puede ser cualquiera) y, con una cámara digital, mueve el recorte de "
                         "todas sus escenas (también el plano general)."),
              ba::i18n::traducir("mcp", "asamblea_escenas_base {crear: true}: crea «Asamblea · Orador» y las del debate con esa fuente."));
        for (const auto& [c, n] : camaras)
            if (eng.sourceExists(c) && escenasCon(eng, c).empty())
                falta("escenas",
                      ba::i18n::llenar(ba::i18n::traducir("mcp", "Ninguna escena muestra la cámara «%1» (la de %n banca): al darle "
                                                  "la palabra no hay qué armar.",
                                           nullptr, n),
                        {c}),
                      ba::i18n::traducir("mcp", "asamblea_escenas_base (las propone y las crea con crear: true)."));
    }
    auto* sr = vm.sesionRemota();
    bool hayRemotos = false;
    if (sr != nullptr)
        for (const auto& p : sr->participantes()) hayRemotos = hayRemotos || p.value("credencial", false);
    const auto conRemoto = escenasCon(eng, kRemoto1);
    bool remotoVisible = false;
    for (const auto& sc : conRemoto) remotoVisible = remotoVisible || sc != eng.sceneNames().front();
    if (hayRemotos && !remotoVisible)
        falta("escenas", ba::i18n::traducir("mcp", "Ninguna escena (además de la primera, donde está oculto) muestra «Remoto con la palabra»: al remoto que habla no se lo ve."),
              ba::i18n::traducir("mcp", "asamblea_escenas_base, o una escena con «Remoto con la palabra» a pantalla completa (scene_define)."));
    if (asamblea::Debate* deb = vm.debate()) {
        const json esc = deb->config().value("escenas", json::object());
        const std::map<std::string, std::string> tipos{{"sala_sala", ba::i18n::traducir("mcp", "dos de la sala")}, {"sala_remoto", ba::i18n::traducir("mcp", "sala y remoto")},
                                                       {"remoto_remoto", ba::i18n::traducir("mcp", "dos remotos")}};
        const auto nombres = eng.sceneNames();
        for (const auto& [k, txt] : tipos) {
            const std::string sc = esc.value(k, std::string());
            if (sc.empty()) {
                if (k == "sala_sala" || hayRemotos)
                    aviso("debate", ba::i18n::llenar(ba::i18n::traducir("mcp", "El debate entre %1 no tiene escena."), {txt}),
                          ba::i18n::llenar(ba::i18n::traducir("mcp", "debate_config {escenas: {%1: \"…\"}} (asamblea_escenas_base las crea y las configura)."), {k}));
            } else if (!tiene(nombres, sc)) {
                falta("debate", ba::i18n::llenar(ba::i18n::traducir("mcp", "La escena del debate entre %1 («%2») ya no existe."), {txt, sc}), "debate_config.");
            } else if (k == "remoto_remoto" && (!tiene(eng.sceneSources(sc), kRemoto1) || !tiene(eng.sceneSources(sc), kRemoto2))) {
                falta("debate",
                      ba::i18n::llenar(ba::i18n::traducir("mcp", "La escena del debate entre dos remotos («%1») no muestra a los dos: necesita «%2» y «%3»."),
                        {sc, kRemoto1, kRemoto2}),
                      ba::i18n::traducir("mcp", "Agregá las dos entradas a esa escena (crear_fuente con esos nombres: se comparten)."));
            } else if (k == "sala_remoto" && !tiene(eng.sceneSources(sc), kRemoto1)) {
                falta("debate", ba::i18n::llenar(ba::i18n::traducir("mcp", "La escena del debate sala-remoto («%1») no muestra «%2»."), {sc, kRemoto1}),
                      ba::i18n::traducir("mcp", "Agregá «Remoto con la palabra» a esa escena."));
            }
        }
    }

    // 5. La central, la banca virtual y el orden del día.
    asamblea::ConferenciaDcn& dcn = vm.dcn();
    if (dcn.conectada()) {
        const auto sin = dcn.estado().value("bancas_sin_unidad", json::array());
        if (!sin.empty())
            falta("central",
                  ba::i18n::traducir("mcp", "%n banca sin unidad de la central: no se le abre el micrófono.", nullptr,
                                     static_cast<int>(sin.size())),
                  ba::i18n::traducir("mcp", "dcn_unidad_asignar {aprender_todas: true} y que alguien apriete el botón de cada banca."));
    } else {
        aviso("central", ba::i18n::traducir("mcp", "La central de conferencia no está conectada."),
              ba::i18n::traducir("mcp", "dcn_conectar (o {transporte: simulador} para probar). Sin ella, los micrófonos de banca son los de la mezcla."));
    }
    if (sr != nullptr) {
        int conLink = 0;
        for (const auto& p : sr->participantes())
            if (p.value("credencial", false)) ++conLink;
        if (conLink == 0)
            aviso("remotos", ba::i18n::traducir("mcp", "Nadie tiene link para entrar desde afuera (banca virtual)."),
                  ba::i18n::traducir("mcp", "sesion_credencial {banca} a los que sesionan a distancia (o también de respaldo en la sala)."));
        for (const auto& v : sr->vistas()) {
            const std::string f = v.value("fuente", std::string());
            if (f != "programa" && !tiene(eng.sceneNames(), f))
                falta("remotos", ba::i18n::llenar(ba::i18n::traducir("mcp", "Lo que ven en el celular usa la escena «%1», que ya no existe."), {f}), "sesion_vistas.");
        }
    }
    if (vm.ordenDelDia().puntos().empty())
        aviso("orden", ba::i18n::traducir("mcp", "El orden del día está vacío."),
              ba::i18n::traducir("mcp", "orden_del_dia_cargar {texto} con una línea por punto (se ve en los celulares y en la consola)."));

    return {{"listo", faltas.empty()},
            {"faltas", faltas},
            {"avisos", avisos},
            {"resumen",
             {{"integrantes", integrantes.size()},
              {"bancas", conBanca},
              {"camaras_de_bancas", camaras.size()},
              {"central", dcn.conectada()},
              {"escenas", eng.sceneNames().size()}}}};
}

void McpService::registerAsambleaTools() {
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    core_.addCategory("asamblea", ba::i18n::traducir("mcp", "Asamblea (preparar la sesión)"),
                      ba::i18n::traducir("mcp", "Revisar lo que hace falta para sesionar (Cuerpo, recinto, cámaras, escenas, central, banca "
                      "virtual) y armar las escenas de una asamblea."));
    {
        Tool t;
        t.name = "asamblea_revisar";
        t.title = ba::i18n::traducir("mcp", "¿Está todo para sesionar?");
        t.category = "asamblea";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Revisa lo que necesita una sesión de asamblea y dice qué falta y cómo se arregla: integrantes con "
                        "banca, la grilla del recinto, el preset de cámara de cada banca, ESCENAS que muestren esas "
                        "cámaras y al remoto con la palabra, las escenas del debate, la central (unidades por banca), la "
                        "banca virtual y el orden del día. Correla antes de dar por armado un evento de asamblea y "
                        "contale al operador lo que salga.");
        t.run = [&vm, &rt](const json&, Done done) { done(ToolResult().data(revisarAsamblea(vm, rt))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "asamblea_escenas_base";
        t.title = ba::i18n::traducir("mcp", "Escenas de una asamblea");
        t.category = "asamblea";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Propone las escenas que necesita una asamblea, armadas con las cámaras y entradas que hay: «Asamblea · "
            "Plano general» (la cámara `general`), «Asamblea · Orador» (la fuente «Banca con la palabra»: la toma de la "
            "banca de quien habla; dar la palabra arma esta), «Asamblea · Remoto» (el remoto con la palabra) y las del "
            "debate: dos remotos lado a lado, sala y remoto lado a lado, y dos de la sala lado a lado (ranuras 1 y 2 de "
            "la fuente de banca). Sin `crear` solo dice lo que haría; con `crear: true` crea las que falten "
            "(nunca pisa una que existe) y deja las del debate en debate_config. Mostrale al operador la propuesta antes "
            "de crear.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"general", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "La cámara del plano general (de fábrica, la que más bancas tiene).")}}},
                           {"crear", {{"type", "boolean"}}}}}};
        t.run = [&vm, &rt](const json& a, Done done) {
            BroadcastEngine& eng = rt.engine();
            const auto& cuerpo = vm.cuerpo().cuerpo();
            const auto camaras = camarasDeBancas(cuerpo);
            std::string general = a.value("general", std::string());
            if (general.empty()) {
                int max = 0;
                for (const auto& [c, n] : camaras)
                    if (n > max && eng.sourceExists(c)) {
                        max = n;
                        general = c;
                    }
            }
            if (!general.empty() && !eng.sourceExists(general))
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay ninguna entrada «%1»"), {general})));
            // Lo que se propone: {escena, fuentes: [{nombre, x, y, w, h}], para}.
            json plan = json::array();
            const auto nombres = eng.sceneNames();
            const auto agregar = [&](const std::string& escena, json fuentes, const std::string& para) {
                plan.push_back({{"escena", escena}, {"fuentes", fuentes}, {"para", para}, {"existe", tiene(nombres, escena)}});
            };
            const auto entera = [](const std::string& f) { return json{{"nombre", f}, {"x", 0}, {"y", 0}, {"w", 1}, {"h", 1}}; };
            const auto izq = [](const std::string& f) { return json{{"nombre", f}, {"x", 0}, {"y", 0.25}, {"w", 0.5}, {"h", 0.5}}; };
            const auto der = [](const std::string& f) { return json{{"nombre", f}, {"x", 0.5}, {"y", 0.25}, {"w", 0.5}, {"h", 0.5}}; };
            if (!general.empty()) agregar("Asamblea · Plano general", json::array({entera(general)}), ba::i18n::traducir("mcp", "el recinto entero"));
            // El orador: la fuente «Banca con la palabra» (la cámara y la toma de la banca de
            // quien habla, sola). Con cámaras digitales recorta la toma entera: una sola
            // cámara da el plano general y las bancas a la vez.
            const bool hayTomas = !camaras.empty();
            if (hayTomas) agregar("Asamblea · Orador", json::array({entera(kBanca1)}), ba::i18n::traducir("mcp", "la banca de quien tiene la palabra"));
            const bool r1 = eng.sourceExists(kRemoto1), r2 = eng.sourceExists(kRemoto2);
            if (r1) agregar("Asamblea · Remoto", json::array({entera(kRemoto1)}), ba::i18n::traducir("mcp", "el remoto con la palabra"));
            if (r1 && r2) agregar("Asamblea · Debate remotos", json::array({izq(kRemoto1), der(kRemoto2)}), ba::i18n::traducir("mcp", "debate entre dos remotos"));
            if (r1 && hayTomas)
                agregar("Asamblea · Debate sala y remoto", json::array({izq(kBanca1), der(kRemoto1)}), ba::i18n::traducir("mcp", "debate entre uno de la sala y un remoto"));
            if (hayTomas)
                agregar("Asamblea · Debate sala", json::array({izq(kBanca1), der(kBanca2)}), ba::i18n::traducir("mcp", "debate entre dos de la sala, cada uno en su banca"));
            if (plan.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay cámaras de bancas en el Cuerpo ni entradas de remotos: cargá el preset "
                                              "de cada banca primero (asamblea_revisar)")));
            if (!a.value("crear", false))
                return done(ToolResult()
                                .text(ba::i18n::traducir("mcp", "Esto es lo que crearía (las que ya existen se dejan como están). Mostraselo al "
                                      "operador y, si está de acuerdo, llamá de nuevo con crear: true."))
                                .data({{"propuesta", plan}, {"general", general}}));
            json creadas = json::array(), errores = json::array();
            for (const auto& p : plan) {
                const std::string sc = p["escena"];
                if (p.value("existe", false)) continue;
                if (!rt.addScene(sc)) {
                    errores.push_back(sc + ": " + rt.lastError());
                    continue;
                }
                for (const auto& f : p["fuentes"]) {
                    SourceSpec spec;
                    spec.name = f["nombre"];
                    spec.id = eng.sourceTypeId(spec.name); // ya existe: se agrega la misma (compartida)
                    spec.settingsJson = "{}";
                    // La fuente de banca, la primera vez: ranura 1 (quien habla) o 2 (el otro del debate).
                    if (!eng.sourceExists(spec.name) && (spec.name == kBanca1 || spec.name == kBanca2)) {
                        spec.id = BroadcastEngine::kTipoBanca;
                        spec.settingsJson = json{{"ranura", spec.name == kBanca1 ? 1 : 2}}.dump();
                    }
                    spec.visible = true;
                    spec.rect = CanvasRect{f.value("x", 0.0f), f.value("y", 0.0f), f.value("w", 1.0f), f.value("h", 1.0f)};
                    if (!rt.addSource(sc, spec)) errores.push_back(sc + " / " + spec.name + ": " + rt.lastError());
                }
                creadas.push_back(sc);
            }
            // El debate queda configurado con las suyas (si no tenía otras).
            if (asamblea::Debate* deb = vm.debate()) {
                json esc = json::object();
                const json actual = deb->config().value("escenas", json::object());
                const auto existe = [&](const std::string& n) { return tiene(eng.sceneNames(), n); };
                if (actual.value("remoto_remoto", std::string()).empty() && existe("Asamblea · Debate remotos"))
                    esc["remoto_remoto"] = "Asamblea · Debate remotos";
                if (actual.value("sala_remoto", std::string()).empty() && existe("Asamblea · Debate sala y remoto"))
                    esc["sala_remoto"] = "Asamblea · Debate sala y remoto";
                if (actual.value("sala_sala", std::string()).empty() && existe("Asamblea · Debate sala"))
                    esc["sala_sala"] = "Asamblea · Debate sala";
                if (!esc.empty()) deb->configurar({{"escenas", esc}});
            }
            rt.save();
            done(ToolResult()
                     .text(errores.empty() ? ba::i18n::traducir("mcp", "Listo.") : ba::i18n::traducir("mcp", "Hecho, con errores en algunas fuentes."))
                     .data({{"creadas", creadas}, {"errores", errores}, {"siguiente", "asamblea_revisar"}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
