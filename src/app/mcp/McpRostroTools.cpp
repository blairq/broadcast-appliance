// ============================================================================
// src/app/mcp/McpRostroTools.cpp — reconocer personas, por MCP
//
// El flujo pensado para en vivo, y por que en ese orden:
//
//   1. `rostro_ver {feed}`      ¿quien hay en esta camara?
//   2. `rostro_guardar {...}`   dar de alta a quien el operador diga que es
//   3. `rostro_ver` otra vez    y ya sale con nombre
//
// NO ROTULA SOLO, Y NO ES UNA LIMITACION TECNICA: es la decision. Estas
// herramientas dicen a quien se parece y cuanto; poner ese nombre en un rotulo
// al aire lo pide una persona. Un sistema que rotula solo se equivoca en
// publico, y el error no se puede deshacer —ya salio—.
//
// Si no estan los modelos vendoreados (`make deps-rostros`), estas
// herramientas NO SE REGISTRAN: mas vale que no existan a que existan y
// contesten siempre que no encontraron a nadie.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#if BA_HAVE_ROSTROS
#include <QDateTime>
#include <QImage>

#include "AppRuntime.hpp"
#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"
#include "ui/FrameService.hpp"
#endif

namespace ba::app::mcp {

#if !BA_HAVE_ROSTROS
void McpService::registerRostroTools() {}
#else
namespace {

/// El motor y la libreta viven mientras vive el servicio: cargar los modelos
/// cuesta ~100 ms y no tiene sentido pagarlo en cada llamada.
struct Estado {
    ba::rostros::Motor motor;
    ba::rostros::Personas libreta;
    std::string archivo;
    bool listo = false;
};

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

/// La imagen de un feed, en BGRA contiguo, que es lo que pide el motor.
bool cuadroBgra(StudioViewModel& vm, const QString& feed, QImage& img,
                std::vector<std::uint8_t>& bgra) {
    auto* frames = static_cast<FrameService*>(vm.frames());
    std::uint64_t serial = 0;
    if (!frames->latest(feed, img, serial) || img.isNull()) return false;
    const QImage c = img.convertToFormat(QImage::Format_ARGB32);
    bgra.resize(static_cast<std::size_t>(c.width()) * c.height() * 4);
    for (int y = 0; y < c.height(); ++y)
        std::memcpy(bgra.data() + static_cast<std::size_t>(y) * c.width() * 4, c.constScanLine(y),
                    static_cast<std::size_t>(c.width()) * 4);
    img = c;
    return true;
}

json fichaJson(const ba::rostros::Persona& p) {
    return json{{"id", p.id},
                {"nombre", p.nombre},
                {"cargo", p.cargo},
                {"nota", p.nota},
                {"caras", p.caras.size()},
                {"vistaMs", p.vistaMs}};
}

} // namespace

void McpService::registerRostroTools() {
    core_.addCategory("rostros", "Personas y rostros",
                      "Reconocer a alguien que ya estuvo: la libreta de personas (nombre, cargo, "
                      "notas) y quien aparece en cada camara. Corre en el aparato, sin red.");

    auto est = std::make_shared<Estado>();
    est->archivo = (rt_.dataRoot() / "Config" / "personas.json").string();
    const auto preparar = [est, this]() -> std::string {
        if (est->listo) return {};
        const std::string modelos =
            (rt_.dataRoot() / "data" / "rostros").string();
        if (!est->motor.abrir(modelos)) return est->motor.error();
        est->libreta.leer(est->archivo);
        est->listo = true;
        return {};
    };

    {
        Tool t;
        t.name = "rostro_ver";
        t.title = "Quien aparece en una camara";
        t.category = "rostros";
        t.readOnly = true;
        t.description =
            "Busca caras en un video (claves de feeds_list: program, armada, guest:N...) y dice a quien "
            "se parece cada una, con cuanta confianza.";
        t.details =
            "`parecido` va de 0 a 1. Por encima de 0.45 es la misma persona con bastante margen "
            "—medido: la misma cara da ~0.86 y dos personas distintas ~0.13—. Abajo de eso viene "
            "`nombre` vacio: es «no se», no «no es nadie».\n\n"
            "NO PONE ROTULOS. Dice quien esta; sacarlo al aire lo pide una persona.\n\n"
            "Si alguien no esta en la libreta, `rostro_guardar` lo da de alta con esa misma cara.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"feed", {{"type", "string"}, {"description", "clave de feeds_list (por defecto program)"}}}}}};
        t.examples = {R"({"feed": "guest:1"})", R"({})"};
        t.run = [est, preparar, this](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            const QString feed =
                QString::fromStdString(a.value("feed", std::string("program")));
            QImage img;
            std::vector<std::uint8_t> bgra;
            if (!cuadroBgra(vm_, feed, img, bgra))
                return done(ToolResult::error("No hay imagen en '" + feed.toStdString() +
                                              "' (feeds_list dice cuales tienen)."));
            const auto caras = est->motor.detectar(bgra.data(), img.width(), img.height());
            if (!est->motor.error().empty())
                return done(ToolResult::error(est->motor.error()));
            json out = json::array();
            for (const auto& c : caras) {
                const auto v = est->motor.describir(bgra.data(), img.width(), img.height(), c);
                const auto q = est->libreta.buscar(v);
                const bool seguro = q.parecido >= ba::rostros::kMismaPersona;
                if (seguro) est->libreta.vistaAhora(q.id, ahoraMs());
                out.push_back(json{{"id", seguro ? q.id : ""},
                                   {"nombre", seguro ? q.nombre : ""},
                                   {"parecido", q.parecido},
                                   {"donde", {{"x", c.x}, {"y", c.y}, {"ancho", c.ancho}, {"alto", c.alto}}},
                                   {"confianza", c.confianza}});
            }
            if (!caras.empty()) est->libreta.guardar(est->archivo);
            done(ToolResult().data(json{{"feed", feed.toStdString()}, {"caras", out}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_guardar";
        t.title = "Dar de alta o corregir a una persona";
        t.category = "rostros";
        t.description =
            "Guarda la ficha de una persona (nombre, cargo para el rotulo, nota) y, si se le pasa un "
            "`feed`, le suma la cara que se vea ahi.";
        t.details =
            "Con `id` corrige una que ya existe; sin `id`, crea. Corregir la ficha NO borra las caras "
            "guardadas.\n\n"
            "SUMALE VARIAS CARAS con el tiempo: de frente, de costado, con anteojos y sin. Una sola "
            "reconoce a esa persona con esa luz y ese angulo; varias aguantan el dia real. Se guardan "
            "hasta 12 y las casi iguales se descartan solas.\n\n"
            "Si hay mas de una cara en el cuadro no adivina: pide que se aclare con `feed` de una "
            "camara donde este sola.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"nombre"}},
            {"properties",
             {{"id", {{"type", "string"}, {"description", "para corregir una que ya existe"}}},
              {"nombre", {{"type", "string"}}},
              {"cargo", {{"type", "string"}, {"description", "lo que iria en el rotulo"}}},
              {"nota", {{"type", "string"}, {"description", "para el operador, no sale al aire"}}},
              {"feed", {{"type", "string"}, {"description", "de donde tomar su cara ahora"}}}}}};
        t.examples = {R"({"nombre": "Ana Perez", "cargo": "Ministra de Salud", "feed": "guest:1"})"};
        t.run = [est, preparar, this](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            ba::rostros::Persona p;
            p.id = a.value("id", std::string());
            p.nombre = a.value("nombre", std::string());
            p.cargo = a.value("cargo", std::string());
            p.nota = a.value("nota", std::string());
            const std::string id = est->libreta.guardarFicha(p);

            std::string extra;
            if (a.contains("feed")) {
                const QString feed = QString::fromStdString(a["feed"].get<std::string>());
                QImage img;
                std::vector<std::uint8_t> bgra;
                if (!cuadroBgra(vm_, feed, img, bgra))
                    extra = " (no habia imagen en '" + feed.toStdString() + "': la ficha quedo sin cara)";
                else {
                    const auto caras = est->motor.detectar(bgra.data(), img.width(), img.height());
                    if (caras.empty())
                        extra = " (no se vio ninguna cara en '" + feed.toStdString() + "')";
                    else if (caras.size() > 1)
                        extra = " (hay " + std::to_string(caras.size()) +
                                " caras en el cuadro: no adivino cual es. Usa una camara donde este "
                                "sola)";
                    else {
                        const auto v =
                            est->motor.describir(bgra.data(), img.width(), img.height(), caras[0]);
                        est->libreta.sumarCara(id, v);
                        const auto* f = est->libreta.porId(id);
                        extra = " Con su cara (" + std::to_string(f ? f->caras.size() : 0) +
                                " guardada(s)).";
                    }
                }
            }
            if (!est->libreta.guardar(est->archivo))
                return done(ToolResult::error("No se pudo guardar: " + est->libreta.error()));
            const auto* f = est->libreta.porId(id);
            ToolResult r;
            r.text("Guardada «" + (f ? f->nombre : std::string()) + "» (id " + id + ")." + extra);
            r.structured = f != nullptr ? fichaJson(*f) : json::object();
            done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_listar";
        t.title = "La libreta de personas";
        t.category = "rostros";
        t.readOnly = true;
        t.description = "Todas las personas guardadas: nombre, cargo, nota, cuantas caras tiene "
                        "guardadas y cuando se la vio por ultima vez.";
        t.details = "`caras` en 0 quiere decir que la ficha existe pero nunca se le mostro la cara: "
                    "no se la va a reconocer hasta que se le sume una con `rostro_guardar`.";
        t.run = [est, preparar](const json&, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            json out = json::array();
            for (const auto& p : est->libreta.todas()) out.push_back(fichaJson(p));
            done(ToolResult().data(json{{"personas", out}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_borrar";
        t.title = "Borrar una persona de la libreta";
        t.category = "rostros";
        t.destructive = true;
        t.description = "Borra la ficha y TODAS sus caras. No se puede deshacer: hay que darla de alta "
                        "de nuevo y volver a mostrarle la cara al sistema.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [est, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            const std::string id = a["id"].get<std::string>();
            const auto* f = est->libreta.porId(id);
            const std::string nombre = f != nullptr ? f->nombre : std::string();
            if (!est->libreta.borrar(id))
                return done(ToolResult::error("No hay nadie con id '" + id + "' (rostro_listar los da)."));
            est->libreta.guardar(est->archivo);
            done(ToolResult::ok("Borrada «" + nombre + "» y sus caras."));
        };
        core_.addTool(std::move(t));
    }
}
#endif // BA_HAVE_ROSTROS

} // namespace ba::app::mcp
