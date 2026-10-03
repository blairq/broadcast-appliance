// src/app/tests/test_diagnostico_video.cpp — el diagnóstico de una entrada de video

#include "video/Diagnostico.hpp"

#include <cstdio>
#include <string>

using nlohmann::json;
using ba::app::video::diagnosticar;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                     \
    do {                                                                \
        if (c)                                                          \
            std::printf("ok: %s\n", m);                                 \
        else {                                                          \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__); \
            ++fallos;                                                   \
        }                                                               \
    } while (0)
bool hay(const json& d, const std::string& tema, const std::string& gravedad) {
    for (const auto& x : d)
        if (x.value("tema", "") == tema && x.value("gravedad", "") == gravedad) return true;
    return false;
}
} // namespace

int main() {
    const json lienzo = {{"ancho", 1920}, {"alto", 1080}, {"fps", 30.0}};
    // Una cámara local bien: nada que decir.
    json ok{{"entrada", "Cam"}, {"lienzo", lienzo}, {"ancho", 1920}, {"alto", 1080}, {"asincronica", true}, {"desentrelazado", "no"}};
    CHECK(diagnosticar(ok).empty(), "una cámara bien: sin diagnóstico");
    // Una de red entrelazada, a 25 fps, 601 en HD, con cuadros tarde y reloj sin sincronizar.
    json mal = ok;
    mal["entrada"] = "Panasonic";
    mal["gstreamer"] = {{"reloj", "ntp"},          {"reloj_sincronizado", false}, {"fps_recibidos", 25.0}, {"entrelazado", true},
                        {"colorimetria", "bt601 (rango limitado)"}, {"recibidos", 100}, {"llegaron_tarde", 20}, {"latencia_ms", 100}};
    const json d = diagnosticar(mal);
    std::printf("%s\n", d.dump(1).c_str());
    CHECK(hay(d, "entrelazado", "error") && hay(d, "cuadros", "aviso") && hay(d, "color", "aviso") && hay(d, "reloj", "error") &&
              hay(d, "colchón", "aviso"),
          "red con problemas: entrelazado, fps, color, reloj y colchón");
    bool arreglo = false;
    for (const auto& x : d)
        if (x.value("tema", "") == "entrelazado") arreglo = x.value("arreglo", "").find("normalizar") != std::string::npos;
    CHECK(arreglo, "el arreglo del entrelazado dice qué hacer (normalizarla)");
    // Normalizada por GStreamer: lo mismo es solo información.
    json norm = mal;
    norm["gstreamer"]["normalizado"] = true;
    norm["gstreamer"]["origen"] = {{"entrelazado", true}, {"fps", 25.0}, {"pixel", "12/11 (no cuadrado)"}};
    const json dn = diagnosticar(norm);
    CHECK(hay(dn, "entrelazado", "info") && hay(dn, "cuadros", "info") && hay(dn, "tamaño", "info") && !hay(dn, "entrelazado", "error"),
          "normalizada: entrelazado, cuadros y píxel son información, no errores");
    // Ya desentrelazada: no se queja más del entrelazado.
    mal["desentrelazado"] = "yadif";
    CHECK(!hay(diagnosticar(mal), "entrelazado", "error"), "desentrelazada: ya no lo marca");
    // Tarjetas: un error y un aviso de 4:2:0.
    json t = ok;
    t["errores_tarjetas"] = {{"cam.gst", "no existe el módulo «x»"}};
    t["avisos_tarjetas"] = {{"cam.gst-2", "«videobalance» trabaja en I420: el color queda a menor resolución"}};
    const json dt = diagnosticar(t);
    CHECK(hay(dt, "tarjeta", "error") && hay(dt, "color", "aviso"), "tarjetas: el error y el aviso de 4:2:0");
    std::printf(fallos == 0 ? "test_diagnostico_video: OK\n" : "test_diagnostico_video: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
