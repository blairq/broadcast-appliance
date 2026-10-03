// src/app/video/Diagnostico.cpp — ver Diagnostico.hpp

#include "video/Diagnostico.hpp"

#include <cmath>
#include <string>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
void agregar(json& out, const char* gravedad, const char* tema, const std::string& problema, const std::string& arreglo) {
    out.push_back({{"gravedad", gravedad}, {"tema", tema}, {"problema", problema}, {"arreglo", arreglo}});
}
std::string n0(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }
} // namespace

json diagnosticar(const json& e) {
    json out = json::array();
    if (!e.is_object() || (e.contains("error") && !e.contains("entrada"))) return out;
    const std::string nombre = e.value("entrada", std::string());
    const json lienzo = e.value("lienzo", json::object());
    const double fpsLienzo = lienzo.value("fps", 0.0);
    const json g = e.value("gstreamer", json::object());
    const bool esGst = !g.empty();

    // 1) Que ande.
    const bool iniciando = e.value("error", std::string()) == "GStreamer se está iniciando";
    if (iniciando)
        agregar(out, "info", "señal", "GStreamer se está iniciando (la primera vez después de instalar revisa sus módulos)",
                "esperar unos segundos y volver a mirar con `video_estado`");
    else if (e.contains("error") && !e.value("error", std::string()).empty())
        agregar(out, "error", "señal", "la fuente no anda: " + e.value("error", std::string()),
                "revisar la URI o el pipeline (`video_estado` lo vuelve a medir); si es de red, que el que manda esté "
                "transmitiendo y el puerto abierto");
    if (e.value("ancho", 0) == 0 && e.value("error", std::string()).empty() && !iniciando)
        agregar(out, "aviso", "señal", "no hay imagen todavía (tamaño 0)",
                esGst ? "esperar unos segundos (la conexión de red tarda) y volver a mirar" : "que la cámara esté conectada y encendida");

    // Lo que llega (antes de normalizar) y si GStreamer ya lo normaliza.
    const json o = g.value("origen", json::object());
    const bool normalizado = g.value("normalizado", false);

    // 2) Cuadros por segundo.
    const double fpsOrigen = o.value("fps", 0.0);
    const double fps = g.value("fps_recibidos", 0.0) > 0 && !normalizado ? g.value("fps_recibidos", 0.0) : fpsOrigen;
    if (o.value("fps_variable", false) && !normalizado)
        agregar(out, "aviso", "cuadros", "manda cuadros a ritmo variable: con movimiento puede tironear",
                "normalizarla (`video_entrada_red {\"nombre\": \"" + nombre + "\", \"normalizar\": true}`)");
    else if (fps > 0 && fpsLienzo > 0 && std::fabs(fps - fpsLienzo) > 1.5)
        agregar(out, normalizado ? "info" : "aviso", "cuadros",
                "manda " + n0(fps) + " cuadros por segundo y el estudio va a " + n0(fpsLienzo) +
                    (normalizado ? " (GStreamer los convierte)" : ": libobs repite o salta cuadros (con movimiento se nota un tironeo)"),
                normalizado ? "nada: ya se convierte" : "si se puede, que la fuente mande a " + n0(fpsLienzo) + " fps, o normalizarla");
    else if (o.value("fps_variable", false))
        agregar(out, "info", "cuadros", "manda cuadros a ritmo variable (GStreamer los lleva a los del estudio)", "nada");

    // 3) Entrelazado.
    const bool entrelazado = o.value("entrelazado", g.value("entrelazado", false));
    const std::string des = e.value("desentrelazado", std::string("no"));
    if (entrelazado && normalizado)
        agregar(out, "info", "entrelazado", "llega entrelazada: GStreamer la desentrelaza", "nada");
    else if (entrelazado && des == "no")
        agregar(out, "error", "entrelazado", "la imagen llega ENTRELAZADA y no se está desentrelazando (peines en el movimiento)",
                esGst ? "normalizarla (`video_entrada_red {\"nombre\": \"" + nombre + "\", \"normalizar\": true}`)"
                      : "tarjeta `normalizar` con {\"desentrelazar\": \"yadif\"} en «" + nombre + "» (yadif2x si hace falta fluidez)");

    // 4) Color: lo que declara la fuente.
    const std::string matriz = o.value("matriz", std::string());
    const std::string col = g.value("colorimetria", std::string());
    const int alto = o.value("alto", e.value("alto", 0));
    if ((matriz == "BT.601" || (matriz.empty() && col.find("bt601") != std::string::npos)) && alto >= 720)
        agregar(out, "aviso", "color",
                "declara color BT.601 siendo HD (" + std::to_string(alto) + " líneas): casi siempre es un error del que manda; "
                "si los colores se ven corridos (piel naranja, verdes raros), es eso",
                "comparar con `capture_source`; si está corrido: tarjeta `normalizar` con {\"matriz_declarada\": \"709\", "
                "\"matriz_real\": \"601\"} si ya se normalizó, o al revés si no");
    if (o.value("hdr", false))
        agregar(out, "aviso", "color", "manda HDR y el estudio es SDR: se va a ver lavado",
                "configurar la cámara en SDR (Rec.709)");
    const std::string pixel = o.value("pixel", std::string("cuadrado"));
    if (pixel != "cuadrado")
        agregar(out, normalizado ? "info" : "aviso", "tamaño",
                "píxeles no cuadrados (" + pixel + ")" + (normalizado ? ": GStreamer la escala a píxel cuadrado" : ": se ve apretada o estirada"),
                normalizado ? "nada" : "normalizarla");

    // 5) Tamaño.
    const int ancho = e.value("ancho", 0);
    if (ancho > 0 && lienzo.value("ancho", 0) > 0 &&
        std::fabs(static_cast<double>(ancho) / std::max(1, alto) - static_cast<double>(lienzo.value("ancho", 1)) /
                                                                   std::max(1, lienzo.value("alto", 1))) > 0.05)
        agregar(out, "info", "tamaño",
                "su proporción (" + std::to_string(ancho) + "x" + std::to_string(alto) + ") no es la del lienzo: en la escena "
                "quedan bandas o se estira",
                "en un compositor la capa la encaja sola (ajuste «encajar»); en una escena, ajustar la fuente con `source_update`");

    // 6) Red: reloj y colchón.
    if (esGst) {
        if (!g.value("reloj_sincronizado", true))
            agregar(out, "error", "reloj",
                    "el reloj «" + g.value("reloj", std::string()) + "» todavía no está sincronizado: los cuadros salen a "
                    "destiempo",
                    "esperar unos segundos; si no sincroniza, revisar `video_reloj` (servidor y puerto accesibles)");
        const double recibidos = g.value("recibidos", 0.0), tarde = g.value("llegaron_tarde", 0.0);
        if (recibidos > 30 && tarde / recibidos > 0.05)
            agregar(out, "aviso", "colchón",
                    "el " + n0(100 * tarde / recibidos) + "% de los cuadros llegó tarde: el colchón (" +
                        n0(g.value("latencia_ms", 0.0)) + " ms) es corto para esta red",
                    "subir su colchón: `video_entrada_red {\"nombre\": \"" + nombre +
                        "\", \"latencia_ms\": …}` (sobre la que ya existe, la actualiza), o para todas `video_reloj "
                        "{\"latencia_ms\": …}`");
        if (g.value("descartados", 0.0) > 0)
            agregar(out, "aviso", "colchón",
                    "se descartaron cuadros porque el colchón se llenó: el reloj de la fuente y el del estudio no coinciden",
                    "usar el mismo reloj en el que manda y en el estudio (`video_reloj`), o bajar la latencia");
    }

    // 7) Lo que dicen las tarjetas.
    const json errores = e.value("errores_tarjetas", json::object()), avisos = e.value("avisos_tarjetas", json::object());
    for (const auto& [id, msg] : errores.items())
        agregar(out, "error", "tarjeta", "«" + id + "»: " + msg.get<std::string>(),
                "`video_modulos {\"elemento\": …}` para ver qué espera el módulo, o cambiarlo por otro");
    for (const auto& [id, msg] : avisos.items()) {
        const std::string m = msg.get<std::string>();
        if (m.find("4:2:0") != std::string::npos || m.find("trabaja en I420") != std::string::npos ||
            m.find("a menor resolución") != std::string::npos)
            agregar(out, "aviso", "color", "«" + id + "»: " + m,
                    "si va antes de un croma o de texto fino, buscar un módulo equivalente que trabaje en RGB o AYUV "
                    "(`video_modulos`)");
        else if (m.find("latencia") != std::string::npos)
            agregar(out, "info", "latencia", "«" + id + "»: " + m,
                    "si esta entrada se compara con otra, tenerlo en cuenta (o `esperar` en la tarjeta)");
    }
    return out;
}

} // namespace ba::app::video
