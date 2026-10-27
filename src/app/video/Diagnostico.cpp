// src/app/video/Diagnostico.cpp — ver Diagnostico.hpp

#include "video/Diagnostico.hpp"

#include "i18n/I18n.hpp"

#include <cmath>
#include <string>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
void agregar(json& out, const char* gravedad, const char* tema, const std::string& problema,
             const std::string& arreglo) {
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

    // 0) Que se esté mostrando: sin eso OBS no la dibuja, y su grafo de tarjetas
    //    (superresolución, filtros) no trabaja; el tamaño que informa es el de
    //    la cámara, no el de la salida del grafo.
    if (e.contains("mostrandose") && !e.value("mostrandose", true))
        agregar(out, "info", "grafo",
                ba::i18n::traducir("diagnostico", "no se está mostrando (no está en programa ni en la vista previa): su "
                                                  "grafo de tarjetas no trabaja y el tamaño que informa es el de la "
                                                  "entrada, sin procesar"),
                ba::i18n::traducir("diagnostico", "ponela en una escena visible (scene_preview o scene_switch) para ver "
                                                  "el resultado de sus tarjetas"));

    // 1) Que ande.
    const bool iniciando = e.value("error", std::string()) == "GStreamer se está iniciando";
    if (iniciando)
        agregar(out, "info", "señal",
                ba::i18n::traducir("diagnostico",
                                   "GStreamer se está iniciando (la primera vez después de instalar revisa sus módulos)"),
                ba::i18n::traducir("diagnostico", "esperar unos segundos y volver a mirar con `video_estado`"));
    else if (e.contains("error") && !e.value("error", std::string()).empty())
        agregar(out, "error", "señal",
                ba::i18n::llenar(ba::i18n::traducir("diagnostico", "la fuente no anda: %1"),
                                 {e.value("error", std::string())}),
                ba::i18n::traducir("diagnostico", "revisar la URI o el pipeline (`video_estado` lo vuelve a medir); si "
                                                  "es de red, que el que manda esté transmitiendo y el puerto abierto"));
    if (e.value("ancho", 0) == 0 && e.value("error", std::string()).empty() && !iniciando)
        agregar(out, "aviso", "señal", ba::i18n::traducir("diagnostico", "no hay imagen todavía (tamaño 0)"),
                esGst ? ba::i18n::traducir("diagnostico", "esperar unos segundos (la conexión de red tarda) y volver a mirar")
                      : ba::i18n::traducir("diagnostico", "que la cámara esté conectada y encendida"));

    // Lo que llega (antes de normalizar) y si GStreamer ya lo normaliza.
    const json o = g.value("origen", json::object());
    const bool normalizado = g.value("normalizado", false);

    // 2) Cuadros por segundo.
    const double fpsOrigen = o.value("fps", 0.0);
    const double fps = fpsOrigen; // cómo llega (antes de normalizar), del motor
    if (o.value("fps_variable", false) && !normalizado)
        agregar(out, "aviso", "cuadros",
                ba::i18n::traducir("diagnostico", "manda cuadros a ritmo variable: con movimiento puede tironear"),
                ba::i18n::llenar(ba::i18n::traducir("diagnostico", "normalizarla (`video_entrada_red {\"nombre\": "
                                                                   "\"%1\", \"normalizar\": true}`)"),
                                 {nombre}));
    else if (fps > 0 && fpsLienzo > 0 && std::fabs(fps - fpsLienzo) > 1.5)
        agregar(out, normalizado ? "info" : "aviso", "cuadros",
                normalizado
                    ? ba::i18n::llenar(ba::i18n::traducir("diagnostico", "manda %1 cuadros por segundo y el estudio va a "
                                                                         "%2 (GStreamer los convierte)"),
                                       {n0(fps), n0(fpsLienzo)})
                    : ba::i18n::llenar(ba::i18n::traducir("diagnostico", "manda %1 cuadros por segundo y el estudio va a "
                                                                         "%2: libobs repite o salta cuadros (con "
                                                                         "movimiento se nota un tironeo)"),
                                       {n0(fps), n0(fpsLienzo)}),
                normalizado ? ba::i18n::traducir("diagnostico", "nada: ya se convierte")
                            : ba::i18n::llenar(ba::i18n::traducir("diagnostico", "si se puede, que la fuente mande a %1 "
                                                                                 "fps, o normalizarla"),
                                               {n0(fpsLienzo)}));
    else if (o.value("fps_variable", false))
        agregar(out, "info", "cuadros",
                ba::i18n::traducir("diagnostico", "manda cuadros a ritmo variable (GStreamer los lleva a los del estudio)"),
                ba::i18n::traducir("diagnostico", "nada"));

    // 3) Entrelazado.
    const bool entrelazado = o.value("entrelazado", g.value("entrelazado", false));
    const std::string des = e.value("desentrelazado", std::string("no"));
    if (entrelazado && normalizado)
        agregar(out, "info", "entrelazado",
                ba::i18n::traducir("diagnostico", "llega entrelazada: GStreamer la desentrelaza"),
                ba::i18n::traducir("diagnostico", "nada"));
    else if (entrelazado && des == "no")
        agregar(out, "error", "entrelazado",
                ba::i18n::traducir("diagnostico",
                                   "la imagen llega ENTRELAZADA y no se está desentrelazando (peines en el movimiento)"),
                esGst ? ba::i18n::llenar(ba::i18n::traducir("diagnostico", "normalizarla (`video_entrada_red {\"nombre\": "
                                                                           "\"%1\", \"normalizar\": true}`)"),
                                         {nombre})
                      : ba::i18n::llenar(ba::i18n::traducir("diagnostico", "tarjeta `normalizar` con {\"desentrelazar\": "
                                                                           "\"yadif\"} en «%1» (yadif2x si hace falta "
                                                                           "fluidez)"),
                                         {nombre}));

    // 4) Color: lo que declara la fuente.
    const std::string matriz = o.value("matriz", std::string());
    const std::string col = g.value("colorimetria", std::string());
    const int alto = o.value("alto", e.value("alto", 0));
    if ((matriz == "BT.601" || (matriz.empty() && col.find("bt601") != std::string::npos)) && alto >= 720)
        agregar(out, "aviso", "color",
                ba::i18n::llenar(ba::i18n::traducir("diagnostico", "declara color BT.601 siendo HD (%1 líneas): casi "
                                                                   "siempre es un error del que manda; si los colores "
                                                                   "se ven corridos (piel naranja, verdes raros), es eso"),
                                 {std::to_string(alto)}),
                ba::i18n::traducir("diagnostico", "comparar con `foto_fuente`; si está corrido: tarjeta `normalizar` con "
                                                  "{\"matriz_declarada\": \"709\", \"matriz_real\": \"601\"} si ya se "
                                                  "normalizó, o al revés si no"));
    if (o.value("hdr", false))
        agregar(out, "aviso", "color",
                ba::i18n::traducir("diagnostico", "manda HDR y el estudio es SDR: se va a ver lavado"),
                ba::i18n::traducir("diagnostico", "configurar la cámara en SDR (Rec.709)"));
    const std::string pixel = o.value("pixel", std::string("cuadrado"));
    if (pixel != "cuadrado")
        agregar(out, normalizado ? "info" : "aviso", "tamaño",
                ba::i18n::llenar(normalizado ? ba::i18n::traducir("diagnostico", "píxeles no cuadrados (%1): GStreamer "
                                                                                 "la escala a píxel cuadrado")
                                             : ba::i18n::traducir("diagnostico", "píxeles no cuadrados (%1): se ve "
                                                                                 "apretada o estirada"),
                                 {pixel}),
                normalizado ? ba::i18n::traducir("diagnostico", "nada") : ba::i18n::traducir("diagnostico", "normalizarla"));

    // 5) Tamaño.
    // Lo que sale (después de los filtros): ancho y alto del mismo lado.
    const int ancho = e.value("ancho", 0), altoSale = e.value("alto", 0);
    if (ancho > 0 && altoSale > 0 && lienzo.value("ancho", 0) > 0 &&
        std::fabs(static_cast<double>(ancho) / altoSale -
                  static_cast<double>(lienzo.value("ancho", 1)) / std::max(1, lienzo.value("alto", 1))) >
            0.05)
        agregar(out, "info", "tamaño",
                ba::i18n::llenar(ba::i18n::traducir("diagnostico", "su proporción (%1x%2) no es la del lienzo: en la "
                                                                   "escena quedan bandas o se estira"),
                                 {std::to_string(ancho), std::to_string(altoSale)}),
                ba::i18n::traducir("diagnostico", "en un compositor la capa la encaja sola (ajuste «encajar»); en una "
                                                  "escena, ajustar la fuente con `source_update`"));

    // 6) Red: reloj y colchón (con lo que mide el motor: Entrada::medirLatencia).
    if (esGst) {
        if (g.value("ntp_apagado_solo", false))
            agregar(out, "info", "reloj",
                    ba::i18n::llenar(ba::i18n::traducir("diagnostico", "la hora que mandaba «%1» estaba muy corrida "
                                                                       "(la entrada salía con segundos de atraso): se "
                                                                       "dejó de usar sola"),
                                     {nombre}),
                    ba::i18n::llenar(ba::i18n::traducir("diagnostico", "poner en hora la cámara (NTP); para volver a "
                                                                       "usar su hora: `video_entrada_red {\"nombre\": "
                                                                       "\"%1\", \"ntp_sync\": true}`"),
                                     {nombre}));
        // La hora de captura que manda, corrida: sus cuadros «llegan antes de capturarse»
        // (edad negativa) y la entrada sale atrasada eso.
        const double edad = g.value("edad_p50_ms", 0.0);
        if (g.value("ntp_sync", false) && g.value("cuadros", 0.0) > 30 && edad < -250)
            agregar(out, "aviso", "reloj",
                    ba::i18n::llenar(ba::i18n::traducir("diagnostico", "los cuadros llegan ~%1 ms antes de su hora de "
                                                                       "captura: la hora que manda «%2» (RTCP/NTP) no "
                                                                       "coincide con la del estudio, y la entrada sale "
                                                                       "ATRASADA esos ms"),
                                     {n0(-edad), nombre}),
                    ba::i18n::llenar(ba::i18n::traducir("diagnostico", "poner en hora la cámara (NTP), o no usar su "
                                                                       "hora: `video_entrada_red {\"nombre\": \"%1\", "
                                                                       "\"ntp_sync\": false}` (después, si hace falta, "
                                                                       "alinear con video_sincronia)"),
                                     {nombre}));
        // Necesita más colchón que el tope del estudio: sus cuadros llegan tarde a la mezcla.
        if (g.value("llega_tarde", false))
            agregar(out, "aviso", "colchón",
                    ba::i18n::llenar(ba::i18n::traducir("diagnostico", "necesita %1 ms de colchón (tarda hasta %2 ms "
                                                                       "en llegar) y el estudio tiene un tope menor: "
                                                                       "sus cuadros llegan tarde a la mezcla"),
                                     {n0(g.value("latencia_ms", 0.0)), n0(g.value("edad_p95_ms", 0.0))}),
                    ba::i18n::traducir("diagnostico", "subir el tope del estudio (`estudio_latencia`), o mejorar la red "
                                                      "de la cámara"));
    }

    // 7) Lo que dicen las tarjetas. Los avisos se reconocen por su texto (los del
    //    motor, que todavía no se traducen).
    const json errores = e.value("errores_tarjetas", json::object()),
               avisos = e.value("avisos_tarjetas", json::object());
    for (const auto& [id, msg] : errores.items())
        agregar(out, "error", "tarjeta",
                ba::i18n::llenar(ba::i18n::traducir("diagnostico", "«%1»: %2"), {id, msg.get<std::string>()}),
                ba::i18n::traducir("diagnostico",
                                   "`video_modulos {\"elemento\": …}` para ver qué espera el módulo, o cambiarlo por otro"));
    for (const auto& [id, msg] : avisos.items()) {
        const std::string m = msg.get<std::string>();
        if (m.find("4:2:0") != std::string::npos || m.find("trabaja en I420") != std::string::npos ||
            m.find("a menor resolución") != std::string::npos)
            agregar(out, "aviso", "color", ba::i18n::llenar(ba::i18n::traducir("diagnostico", "«%1»: %2"), {id, m}),
                    ba::i18n::traducir("diagnostico", "si va antes de un croma o de texto fino, buscar un módulo "
                                                      "equivalente que trabaje en RGB o AYUV (`video_modulos`)"));
        else if (m.find("latencia") != std::string::npos)
            agregar(out, "info", "latencia", ba::i18n::llenar(ba::i18n::traducir("diagnostico", "«%1»: %2"), {id, m}),
                    ba::i18n::traducir("diagnostico",
                                       "si esta entrada se compara con otra, tenerlo en cuenta (o `esperar` en la tarjeta)"));
    }
    return out;
}

} // namespace ba::app::video
