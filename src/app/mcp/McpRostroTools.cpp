// ============================================================================
// src/app/mcp/McpRostroTools.cpp — reconocer personas, por MCP
//
// El flujo pensado para en vivo, y por que en ese orden:
//
//   1. `rostro_ver {feed}`      ¿quien hay en esta camara?
//   2. `rostro_guardar {...}`   dar de alta a quien el operador diga que es
//   3. `rostro_ver` otra vez    y ya sale con nombre
//
// EL ALTA GUIADA DE UN INVITADO QUE ENTRA POR WEBRTC, que es el caso que
// importa: el invitado esta del otro lado de una camara que no controlamos, con
// la luz de su casa. No alcanza con mirar una vez.
//
//   1. `rostro_calidad {feed: "guest:1"}`  ¿sirve esta cara? y si no, ¿que le
//                                          pido? («acercate», «mira de frente»,
//                                          «hay poca luz»)
//   2. `chat_send`                         se lo decis con esas mismas palabras
//   3. cuando `sirve` es true, `rostro_guardar` (si es nuevo) o `rostro_sumar`
//   4. pedile que gire un poco y repeti 1-3 tres o cuatro veces: `rostro_sumar`
//      avisa con `aporto` si esa pose agrego algo o si ya la tenia
//   5. `rostro_ver` para confirmar que ahora sale con su nombre
//
// Y si lo que se quiere es no perder a nadie: `rostro_autoalta` anota sola a
// toda cara desconocida que pase, sin nombre y con foto; `rostro_pendientes`
// las lista y `rostro_guardar {id, nombre}` las completa. La misma persona
// puede quedar anotada dos veces (otra luz, de perfil): `rostro_fusionar` las
// junta sin perder las caras de ninguna.
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
#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>

#include "AppRuntime.hpp"
#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"
#include "ui/FrameService.hpp"
#include "ui/CalidadCara.hpp"
#include "ui/PersonasViewModel.hpp"
#endif

namespace ba::app::mcp {

#if !BA_HAVE_ROSTROS
void McpService::registerRostroTools() {}
#else
namespace {

/// UNA SOLA LIBRETA VIVA, la de la página de Personas.
///
/// Antes esto tenía su propio `Motor` y su propia `Personas` leídos del mismo
/// JSON. Mientras nadie escribía, las dos copias decían lo mismo y el error no
/// se veía. Con el anotado automático escribiendo, la copia de acá queda vieja
/// y el primer `rostro_guardar` pisa el archivo con una libreta a la que le
/// faltan las fichas recién anotadas —trabajo perdido sin un solo error—.
/// Además los modelos se cargan una vez y no dos.
PersonasViewModel* libretaViva(StudioViewModel& vm) {
    return static_cast<PersonasViewModel*>(vm.personas());
}

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

json fichaJson(const ba::rostros::Persona& p) {
    return json{{"id", p.id},
                {"nombre", p.nombre},
                {"cargo", p.cargo},
                {"nota", p.nota},
                {"foto", p.foto},
                {"caras", p.caras.size()},
                {"vistaMs", p.vistaMs}};
}

/// Guarda el recorte de la cara como foto de la ficha. Devuelve el nombre del
/// archivo, o vacío si no se pudo.
///
/// CON MARGEN Y CUADRADO. El rectángulo que da el detector es justo la cara:
/// recortado ahí, la foto queda claustrofóbica y cuesta reconocer a quién
/// pertenece. Un 40% alrededor entra el pelo y algo de hombro, que es lo que
/// una persona usa para decir «sí, es ella».
///
/// Chica a propósito (256 px): es para mirar en una lista, no para imprimir, y
/// multiplicada por cien fichas la carpeta portable se nota.
QString guardarFoto(const QImage& img, const ba::rostros::Cara& c, const QString& carpeta,
                    const QString& id) {
    const int margen = static_cast<int>(std::max(c.ancho, c.alto) * 0.4);
    const int lado = std::max(c.ancho, c.alto) + margen * 2;
    QRect r(c.x + c.ancho / 2 - lado / 2, c.y + c.alto / 2 - lado / 2, lado, lado);
    r = r.intersected(img.rect());
    if (r.width() < 16 || r.height() < 16) return {};
    QDir().mkpath(carpeta);
    const QString nombre = id + ".jpg";
    const QImage recorte = img.copy(r).scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return recorte.save(carpeta + "/" + nombre, "JPG", 88) ? nombre : QString();
}


json calidadJson(const Calidad& q) {
    return json{{"sirve", q.sirve},
                {"sugerencia", q.sugerencia},
                {"altoPx", q.altoPx},
                {"nitidez", q.nitidez},
                {"luz", q.luz},
                {"giro", q.giro},
                {"inclinacion", q.inclinacion},
                {"cortada", q.cortada},
                {"confianza", q.confianza}};
}

} // namespace

void McpService::registerRostroTools() {
    core_.addCategory("rostros", "Personas y rostros",
                      "Reconocer a alguien que ya estuvo: la libreta de personas (nombre, cargo, "
                      "notas) y quien aparece en cada camara. Tambien el alta guiada de un invitado "
                      "por WebRTC (`rostro_calidad` dice que pedirle) y el anotado automatico de "
                      "toda cara nueva (`rostro_autoalta`). Corre en el aparato, sin red.");

    const auto preparar = [this]() -> std::string {
        PersonasViewModel* pv = libretaViva(vm_);
        if (pv == nullptr) return "No hay libreta de personas en este proceso.";
        if (!pv->disponible()) return pv->aviso().toStdString();
        return {};
    };
    const auto pvDe = [this] { return libretaViva(vm_); };

    {
        Tool t;
        t.name = "rostro_ver";
        t.title = "Quien aparece en una camara";
        t.category = "rostros";
        t.readOnly = true;
        t.description =
            "Solo IDENTIFICA, no pone nada en pantalla. Busca caras en un video "
            "(claves de feeds_list: program, armada, guest:N...) y dice a quien "
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
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            const QString feed = QString::fromStdString(a.value("feed", std::string("program")));
            pv->pedirAnalisis(feed, false,
                              [pv, feed, done](std::vector<Reconocedor::Visto> caras,
                                               const QImage&, const QString& error) {
                                  if (!error.isEmpty())
                                      return done(ToolResult::error(error.toStdString()));
                                  json out = json::array();
                                  for (const auto& visto : caras) {
                                      const ba::rostros::Cara& c = visto.cara;
                                      const auto q = pv->libreta().buscar(visto.vector);
                                      const bool seguro = q.parecido >= ba::rostros::kMismaPersona;
                                      if (seguro) pv->libreta().vistaAhora(q.id, ahoraMs());
                                      out.push_back(json{
                                          {"id", seguro ? q.id : ""},
                                          {"nombre", seguro ? q.nombre : ""},
                                          {"parecido", q.parecido},
                                          {"donde",
                                           json{{"x", c.x}, {"y", c.y}, {"ancho", c.ancho},
                                                {"alto", c.alto}}},
                                          {"confianza", c.confianza}});
                                  }
                                  if (!caras.empty()) pv->persistir();
                                  done(ToolResult().data(
                                      json{{"feed", feed.toStdString()}, {"caras", out}}));
                              });
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
              {"feed", {{"type", "string"}, {"description", "de donde tomar su cara ahora"}}},
              {"nuevaFoto",
               {{"type", "boolean"},
                {"description", "reemplazar la foto de la ficha por la de ahora (por defecto no)"}}}}}};
        t.examples = {R"({"nombre": "Ana Perez", "cargo": "Ministra de Salud", "feed": "guest:1"})"};
        t.run = [pvDe, preparar, this](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            ba::rostros::Persona p;
            p.id = a.value("id", std::string());
            p.nombre = a.value("nombre", std::string());
            p.cargo = a.value("cargo", std::string());
            p.nota = a.value("nota", std::string());
            const std::string id = pv->libreta().guardarFicha(p);

            // Contestar es lo ultimo que pasa, y puede pasar despues de mirar
            // una camara: eso corre en otro hilo (ver Reconocedor.hpp).
            const auto contestar = [pv, id, done](const std::string& extra) {
                if (!pv->persistir())
                    return done(ToolResult::error("No se pudo guardar: " + pv->libreta().error()));
                const auto* f = pv->libreta().porId(id);
                ToolResult r;
                r.text("Guardada «" + (f != nullptr ? f->nombre : std::string()) + "» (id " + id +
                       ")." + extra);
                r.structured = f != nullptr ? fichaJson(*f) : json::object();
                done(r);
            };
            if (!a.contains("feed")) return contestar({});

            const QString feed = QString::fromStdString(a["feed"].get<std::string>());
            const bool nuevaFoto = a.value("nuevaFoto", false);
            pv->pedirAnalisis(
                feed, false,
                [pv, id, feed, nuevaFoto, contestar](std::vector<Reconocedor::Visto> caras,
                                                     const QImage& img, const QString& error) {
                    if (!error.isEmpty())
                        return contestar(" (" + error.toStdString() +
                                         ": la ficha quedo sin cara)");
                    if (caras.empty())
                        return contestar(" (no se vio ninguna cara en '" + feed.toStdString() +
                                         "')");
                    if (caras.size() > 1)
                        return contestar(" (hay " + std::to_string(caras.size()) +
                                         " caras en el cuadro: no adivino cual es. Usa una camara "
                                         "donde este sola)");
                    pv->libreta().sumarCara(id, caras[0].vector);
                    // La foto se saca la PRIMERA vez y después solo si la
                    // pidieron: cambiarla en cada captura haría que la ficha se
                    // vea distinta cada vez sin que nadie lo decidiera, y la
                    // que quedaría es la última, no la mejor.
                    const auto* f0 = pv->libreta().porId(id);
                    if (f0 != nullptr && (f0->foto.empty() || nuevaFoto)) {
                        const QString archivo = guardarFoto(img, caras[0].cara, pv->carpetaFotos(),
                                                            QString::fromStdString(id));
                        if (!archivo.isEmpty()) {
                            ba::rostros::Persona conFoto = *f0;
                            conFoto.foto = archivo.toStdString();
                            pv->libreta().guardarFicha(conFoto);
                        }
                    }
                    const auto* f = pv->libreta().porId(id);
                    contestar(" Con su cara (" +
                              std::to_string(f != nullptr ? f->caras.size() : 0) + " guardada(s))" +
                              (f != nullptr && !f->foto.empty() ? " y su foto." : "."));
                });
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
        t.run = [pvDe, preparar](const json&, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            json out = json::array();
            for (const auto& p : pv->libreta().todas()) out.push_back(fichaJson(p));
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
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            const std::string id = a["id"].get<std::string>();
            const auto* f = pv->libreta().porId(id);
            const std::string nombre = f != nullptr ? f->nombre : std::string();
            const QString foto = f != nullptr ? QString::fromStdString(f->foto) : QString();
            if (!pv->libreta().borrar(id))
                return done(ToolResult::error("No hay nadie con id '" + id + "' (rostro_listar los da)."));
            // LA FOTO SE VA CON LA FICHA. Esto no lo hacia, y dejaba la cara de
            // alguien a quien pidieron borrar tirada en el disco: la ficha
            // desaparecia de la lista y el archivo se quedaba para siempre. Es
            // la misma regla que ya cumplia la pagina; aca faltaba.
            if (!foto.isEmpty()) QFile::remove(pv->carpetaFotos() + "/" + foto);
            pv->persistir();
            done(ToolResult::ok("Borrada «" + nombre + "» y sus caras."));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_calidad";
        t.title = "Si la cara que se ve sirve para reconocer, y que pedir si no";
        t.category = "rostros";
        t.readOnly = true;
        t.description =
            "Mide cada cara que se ve en un video y dice si sirve para guardarla, y si no sirve, QUE "
            "pedirle a la persona (que se acerque, que mire de frente, que prenda una luz).";
        t.details =
            "ESTA ES LA HERRAMIENTA DEL ALTA GUIADA de un invitado que entra por WebRTC. El lazo:\n\n"
            "  1. `rostro_calidad {feed}` -> mira `sugerencia`\n"
            "  2. si `sirve` es false, deci esa frase por `chat_send` al invitado y volve al 1\n"
            "  3. cuando `sirve` es true, `rostro_guardar` (alta) o `rostro_sumar` (una pose mas)\n"
            "  4. repetilo pidiendole que gire un poco la cabeza: con varias poses lo reconoce\n"
            "     despues aunque se mueva, cambie la luz o se saque los anteojos\n\n"
            "`giro` es 0 de frente y se va a +-0.5 de perfil. `nitidez` se mide al tamano que ve el "
            "modelo (112 px), no al que tiene en el cuadro: lo que importa es cuanto detalle le "
            "llega, no que la cara sea grande.\n\n"
            "Tambien dice a quien se parece cada cara, asi que sirve para saber si hay que dar de "
            "alta o solo sumarle una pose a alguien que ya esta.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"feed", {{"type", "string"}, {"description", "clave de feeds_list (por defecto program)"}}}}}};
        t.examples = {R"({"feed": "guest:1"})"};
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            const QString feed = QString::fromStdString(a.value("feed", std::string("program")));
            pv->pedirAnalisis(
                feed, false,
                [pv, feed, done](std::vector<Reconocedor::Visto> caras, const QImage& img,
                                 const QString& error) {
                    if (!error.isEmpty()) return done(ToolResult::error(error.toStdString()));
                    if (caras.empty()) {
                        ToolResult r;
                        r.text("No se ve ninguna cara en '" + feed.toStdString() +
                               "'. Pedile que se ponga de frente a la camara.");
                        r.structured =
                            json{{"feed", feed.toStdString()}, {"caras", json::array()}};
                        return done(r);
                    }
                    json out = json::array();
                    for (const auto& visto : caras) {
                        const ba::rostros::Cara& c = visto.cara;
                        const auto q = pv->libreta().buscar(visto.vector);
                        const bool seguro = q.parecido >= ba::rostros::kMismaPersona;
                        json j = calidadJson(calidadDe(img, c));
                        j["id"] = seguro ? q.id : "";
                        j["nombre"] = seguro ? q.nombre : "";
                        j["parecido"] = q.parecido;
                        j["donde"] = json{
                            {"x", c.x}, {"y", c.y}, {"ancho", c.ancho}, {"alto", c.alto}};
                        out.push_back(j);
                    }
                    ToolResult r;
                    r.text(caras.size() == 1
                               ? out[0]["sugerencia"].get<std::string>()
                               : "Hay " + std::to_string(caras.size()) + " caras en el cuadro.");
                    r.structured = json{{"feed", feed.toStdString()}, {"caras", out}};
                    done(r);
                });
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_sumar";
        t.title = "Sumarle una pose mas a alguien que ya esta";
        t.category = "rostros";
        t.description =
            "Le suma a una ficha la cara que se vea ahora en un video. Dice si la pose APORTO algo o "
            "si ya la tenia, que es lo que hay que saber para pedir otra.";
        t.details =
            "PARA QUE SIRVE SUMAR VARIAS. Una sola cara reconoce a esa persona con esa luz y ese "
            "angulo. Cuatro o cinco —de frente, de tres cuartos a cada lado, con anteojos y sin— "
            "aguantan el dia real. Se guardan hasta 12.\n\n"
            "`aporto` en false quiere decir que la pose era casi igual a una que ya tenia: no es un "
            "error, es «pedile que gire un poco la cabeza y proba de nuevo». Sumar treinta capturas "
            "del mismo segundo no mejora nada.\n\n"
            "Mira `rostro_calidad` ANTES: una cara borrosa o de perfil sumada a la ficha ensucia el "
            "reconocimiento en vez de mejorarlo. Por eso esta herramienta la rechaza sola.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"id", "feed"}},
            {"properties",
             {{"id", {{"type", "string"}, {"description", "de rostro_listar o rostro_ver"}}},
              {"feed", {{"type", "string"}}},
              {"nuevaFoto", {{"type", "boolean"}, {"description", "ademas, cambiarle la foto"}}},
              {"igual",
               {{"type", "boolean"},
                {"description", "sumarla aunque la calidad no de (por defecto no)"}}}}}};
        t.examples = {R"({"id": "p20921624be7f", "feed": "guest:1"})"};
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            const std::string id = a["id"].get<std::string>();
            if (pv->libreta().porId(id) == nullptr)
                return done(ToolResult::error("No hay nadie con id '" + id + "'."));
            const QString feed = QString::fromStdString(a["feed"].get<std::string>());
            const bool nuevaFoto = a.value("nuevaFoto", false);
            const bool igual = a.value("igual", false);
            pv->pedirAnalisis(
                feed, false,
                [pv, id, feed, nuevaFoto, igual, done](std::vector<Reconocedor::Visto> caras,
                                                       const QImage& img, const QString& error) {
                    if (!error.isEmpty()) return done(ToolResult::error(error.toStdString()));
                    if (caras.empty())
                        return done(ToolResult::error("No se ve ninguna cara en '" +
                                                      feed.toStdString() + "'."));
                    if (caras.size() > 1)
                        return done(ToolResult::error(
                            "Hay " + std::to_string(caras.size()) +
                            " caras en el cuadro: no adivino cual es. Usa una camara donde este "
                            "sola."));
                    const Calidad q = calidadDe(img, caras[0].cara);
                    if (!q.sirve && !igual) {
                        ToolResult r;
                        r.text("No la sume: " + q.sugerencia);
                        r.structured = calidadJson(q);
                        return done(r);
                    }
                    const auto alta = pv->libreta().sumarCara(id, caras[0].vector);
                    const auto* f = pv->libreta().porId(id);
                    if (f != nullptr && (f->foto.empty() || nuevaFoto)) {
                        const QString archivo = guardarFoto(img, caras[0].cara, pv->carpetaFotos(),
                                                            QString::fromStdString(id));
                        if (!archivo.isEmpty()) {
                            ba::rostros::Persona conFoto = *f;
                            conFoto.foto = archivo.toStdString();
                            pv->libreta().guardarFicha(conFoto);
                        }
                    }
                    pv->persistir();
                    const bool aporto = alta == ba::rostros::Personas::Alta::Sumada;
                    const auto* fin = pv->libreta().porId(id);
                    ToolResult r;
                    r.text(aporto
                               ? "Sumada. Ahora tiene " +
                                     std::to_string(fin != nullptr ? fin->caras.size() : 0) +
                                     " cara(s)."
                               : "Esa pose ya la tenia. Pedile que gire un poco la cabeza o que "
                                 "cambie de angulo, y proba de nuevo.");
                    json j = calidadJson(q);
                    j["aporto"] = aporto;
                    j["caras"] = fin != nullptr ? fin->caras.size() : 0;
                    r.structured = j;
                    done(r);
                });
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_pendientes";
        t.title = "Las fichas sin nombre, esperando que alguien diga quien es";
        t.category = "rostros";
        t.readOnly = true;
        t.description =
            "Las personas que el sistema anoto solo (interruptor `rostro_autoalta`) y todavia no "
            "tienen nombre. Son las que hay que completar con `rostro_guardar {id, nombre}`.";
        t.details = "Cada una trae su foto en `foto` (archivo al lado de la libreta) para que alguien "
                    "pueda mirarla y decir quien es. Sin nombre no rotulan nada.";
        t.run = [pvDe, preparar](const json&, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            json out = json::array();
            for (const auto& p : pv->libreta().todas())
                if (p.nombre.empty()) out.push_back(fichaJson(p));
            ToolResult r;
            r.text(out.empty() ? "No hay fichas sin nombre."
                               : std::to_string(out.size()) + " ficha(s) sin nombre.");
            r.structured = json{{"personas", out}};
            done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_fusionar";
        t.title = "Juntar dos fichas de la misma persona";
        t.category = "rostros";
        t.destructive = true;
        t.description =
            "Pasa las caras de `otro` a `id` y borra `otro`. Para cuando la misma persona quedo "
            "anotada dos veces.";
        t.details =
            "PASA, Y NO ES UNA FALLA. La misma persona con otra luz, de perfil o con barbijo puede no "
            "parecerse a si misma lo suficiente, y el alta automatica le abre una ficha nueva. "
            "Fusionar aprovecha las caras de las dos: la ficha resultante reconoce MEJOR que "
            "cualquiera de las dos sueltas. Borrar una tira ese trabajo.\n\n"
            "Los datos escritos los gana `id`; de `otro` se toman solo los que `id` tenga vacios. "
            "Mira las dos fotos antes: si no son la misma persona, esto no se deshace.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "otro"}},
                         {"properties",
                          {{"id", {{"type", "string"}, {"description", "la que queda"}}},
                           {"otro", {{"type", "string"}, {"description", "la que se absorbe"}}}}}};
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            const std::string id = a["id"].get<std::string>();
            const std::string otro = a["otro"].get<std::string>();
            const auto* b = pv->libreta().porId(otro);
            const QString fotoOtro = b != nullptr ? QString::fromStdString(b->foto) : QString();
            if (!pv->libreta().fusionar(id, otro))
                return done(ToolResult::error(
                    "No pude fusionar: revisa que los dos ids existan y sean distintos."));
            const auto* f = pv->libreta().porId(id);
            // La foto de la absorbida se borra solo si la fusionada no se quedo
            // con ella (pasa cuando la que queda no tenia foto).
            if (!fotoOtro.isEmpty() &&
                (f == nullptr || QString::fromStdString(f->foto) != fotoOtro))
                QFile::remove(pv->carpetaFotos() + "/" + fotoOtro);
            pv->persistir();
            ToolResult r;
            r.text("Fusionadas. «" + (f != nullptr ? f->nombre : std::string()) + "» quedo con " +
                   std::to_string(f != nullptr ? f->caras.size() : 0) + " cara(s).");
            r.structured = f != nullptr ? fichaJson(*f) : json::object();
            done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "padron_puerta";
        t.title = "La camara de la entrada: acreditar sola a quien llega";
        t.category = "padron";
        t.description =
            "Prende o apaga la camara de la PUERTA: reconoce a quien entra al lugar y lo marca como "
            "llegado en el padron del evento abierto.";
        t.details =
            "Es la acreditacion presencial, y es la contracara del lobby: el mismo reconocimiento, "
            "apuntado al mundo fisico en vez de al remoto. Con las dos prendidas, «quienes estan» "
            "sabe de las dos mitades de la gente, que es de lo que se trata un evento hibrido.\n\n"
            "NO ANOTA A NADIE EN EL PADRON. Si llega alguien que no esta en la lista, lo dice y sigue: "
            "quien entra sin estar invitado es una decision del que organiza, no del reconocimiento. "
            "Para sumarlo, `padron_anotar`.\n\n"
            "Tampoco da de alta fichas de rostro: eso es `rostro_autoalta`, que es otra cosa y se puede "
            "tener prendida al mismo tiempo. Una llena la LIBRETA (quien es esta cara); esta llena la "
            "ASISTENCIA (esta persona ya esta aca).\n\n"
            "Pide la misma seguridad que para abrir la puerta de la sala, y que la cara se vea bien. "
            "Acreditar al que no es deja a dos personas mal anotadas —una que no vino y otra que si— y "
            "no se descubre hasta que hay que contar el quorum.\n\n"
            "Acreditar de nuevo al que ya paso no molesta y no mueve la hora: en una entrada la gente "
            "se queda parada frente a la camara. El aviso es una sola vez por persona.\n\n"
            "Necesita un evento abierto: el padron vive en el evento.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"activo", {{"type", "boolean"}}},
              {"feed",
               {{"type", "string"},
                {"description", "que camara mira la entrada (clave de feeds_list)"}}}}}};
        t.examples = {R"({"activo": true, "feed": "cam:2"})", R"({})"};
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            if (a.contains("feed"))
                pv->setFeedPuerta(QString::fromStdString(a["feed"].get<std::string>()));
            if (a.contains("activo")) {
                if (a["activo"].get<bool>() && pv->feedPuerta().isEmpty())
                    return done(ToolResult::error(
                        "Falta decir que camara mira la entrada: manda `feed` con una clave de "
                        "feeds_list."));
                pv->setPuertaAuto(a["activo"].get<bool>());
            }
            ToolResult r;
            r.text(std::string(pv->puertaAuto() ? "Acreditacion en la puerta PRENDIDA mirando '"
                                                : "Acreditacion en la puerta apagada (miraria '") +
                   pv->feedPuerta().toStdString() + "'.");
            r.structured = json{{"activo", pv->puertaAuto()},
                                {"feed", pv->feedPuerta().toStdString()}};
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "rostro_autoalta";
        t.title = "Anotar sola a cada persona nueva que pase por una camara";
        t.category = "rostros";
        t.description =
            "Prende o apaga el anotado automatico: con el puesto, cada cara que pase por la camara "
            "elegida y no este en la libreta queda anotada SIN NOMBRE y con su foto, y a las que ya "
            "conoce les suma la pose nueva.";
        t.details =
            "QUE NO HACE: no le pone nombre a nadie ni saca nada al aire. Una ficha sin nombre no "
            "rotula. Despues `rostro_pendientes` las lista y `rostro_guardar {id, nombre}` las "
            "completa.\n\n"
            "Anota recien despues de ver la misma cara tres pasadas seguidas: un reflejo o media "
            "cara que entra y sale llenarian la libreta de fichas de nada.\n\n"
            "Llegando a 500 fichas se apaga solo y lo dice. Sin `activo` solo informa como esta.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"activo", {{"type", "boolean"}}},
              {"feed",
               {{"type", "string"},
                {"description",
                 "que camara mirar (clave de feeds_list), o \"lobby\" para todas las posiciones "
                 "de espera por turno"}}}}}};
        t.examples = {R"({"activo": true, "feed": "lobby"})",
                      R"({"activo": true, "feed": "guest:1"})", R"({})"};
        t.run = [pvDe, preparar](const json& a, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            // `feed: "lobby"` no es una camara: es TODAS las posiciones de
            // espera, por turno. Es el modo que se usa en un evento.
            const bool esLobby =
                a.contains("feed") && a["feed"].get<std::string>() == "lobby";
            if (a.contains("feed") && !esLobby)
                pv->setFeedAuto(QString::fromStdString(a["feed"].get<std::string>()));
            if (a.contains("activo")) {
                if (esLobby || pv->lobbyAuto())
                    pv->setLobbyAuto(a["activo"].get<bool>());
                if (!esLobby) pv->setAutoalta(a["activo"].get<bool>());
            }
            ToolResult r;
            const std::string donde =
                pv->lobbyAuto() ? "el lobby (todas las posiciones de espera)"
                                : "'" + pv->feedAuto().toStdString() + "'";
            r.text(std::string(pv->autoalta() || pv->lobbyAuto()
                                   ? "Anotado automatico PRENDIDO mirando "
                                   : "Anotado automatico apagado (miraria ") +
                   donde + ". " + std::to_string(pv->sinNombre()) + " ficha(s) sin nombre.");
            r.structured = json{{"activo", pv->autoalta() || pv->lobbyAuto()},
                                {"lobby", pv->lobbyAuto()},
                                {"feed", pv->lobbyAuto() ? std::string("lobby")
                                                         : pv->feedAuto().toStdString()},
                                {"sinNombre", pv->sinNombre()}};
            done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "rostro_lobby";
        t.title = "Quien es cada uno de los que estan esperando";
        t.category = "rostros";
        t.readOnly = true;
        t.description =
            "Solo IDENTIFICA, no deja entrar a nadie. Para cada posicion del lobby: a quien "
            "reconoce, si su camara sirve para guardarle la cara, y que pedirle si no.";
        t.details =
            "EL LOBBY ES EL MOMENTO. La gente llega antes de entrar y se queda quieta frente a su "
            "camara esperando: es el unico rato en que hay tiempo de saber quien es sin que nada de "
            "esto salga al aire.\n\n"
            "El lazo del alta asistida de alguien que espera:\n\n"
            "  1. `rostro_lobby` -> mira `sugerencia` y `sirve` de su posicion\n"
            "  2. si `sirve` es false, deciselo por `chat_send {a: <su id>}` con esas palabras\n"
            "  3. cuando `sirve` es true y no tiene `id`, `rostro_guardar {nombre, feed: \"lobby:N\"}`\n"
            "     —el nombre lo escribio el al entrar: `room_status` lo trae—\n"
            "  4. si ya tiene `id`, `rostro_sumar {id, feed: \"lobby:N\"}` para que lo reconozca mejor\n"
            "  5. `participant_admit` cuando lo quieras adentro\n\n"
            "`uid` es la persona (el mismo que usan `room_status`, `chat_send` y "
            "`participant_admit`), y `feed` es su camara: sirve tal cual en `rostro_calidad`, "
            "`rostro_sumar`, `rostro_guardar` y `capture_feed`.\n\n"
            "`parecido` por debajo de 0.45 sale sin `id`: es «no se», no «no es nadie». Si viene "
            "cerca del corte (0.40, 0.43) casi seguro ES alguien de la libreta y lo que falta es "
            "una cara mejor: mira `sugerencia`, pediselo por `chat_send`, y cuando `sirve` sea "
            "true volve a preguntar.\n\n"
            "Para que esto conteste algo hace falta que el reconocimiento del lobby este prendido: "
            "`rostro_autoalta {activo: true, feed: \"lobby\"}`, o el interruptor «Reconocer a los que "
            "llegan» en la pestaña Sala. Sin eso nadie esta mirando esas camaras.";
        t.run = [pvDe, preparar](const json&, Done done) {
            if (const std::string e = preparar(); !e.empty()) return done(ToolResult::error(e));
            PersonasViewModel* pv = pvDe();
            json out = json::array();
            const QVariantMap m = pv->enLobby();
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
                const QVariantMap v = it.value().toMap();
                const int lugar = v.value("lugar").toInt();
                out.push_back(json{{"uid", it.key().toStdString()},
                                   {"lugar", lugar},
                                   {"feed", "lobby:" + std::to_string(lugar)},
                                   {"id", v.value("id").toString().toStdString()},
                                   {"nombre", v.value("nombre").toString().toStdString()},
                                   {"cargo", v.value("cargo").toString().toStdString()},
                                   {"parecido", v.value("parecido").toFloat()},
                                   {"caras", v.value("caras").toInt()},
                                   {"sirve", v.value("sirve").toBool()},
                                   {"sugerencia", v.value("sugerencia").toString().toStdString()}});
            }
            ToolResult r;
            if (!pv->lobbyAuto())
                r.text("El reconocimiento del lobby esta APAGADO: nadie esta mirando esas camaras. "
                       "Prendelo con rostro_autoalta {activo: true, feed: \"lobby\"}.");
            else
                r.text(out.empty() ? "No se ve a nadie en las camaras de espera."
                                   : std::to_string(out.size()) + " en el lobby.");
            r.structured = json{{"mirando", pv->lobbyAuto()}, {"lugares", out}};
            done(r);
        };
        core_.addTool(std::move(t));
    }
}
#endif // BA_HAVE_ROSTROS

} // namespace ba::app::mcp
