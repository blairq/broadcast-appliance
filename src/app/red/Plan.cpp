// src/app/red/Plan.cpp — ver Plan.hpp

#include "red/Plan.hpp"

#include <cmath>
#include <string>

namespace ba::app::red {

using json = nlohmann::json;

namespace {
/// Qué tan bueno es un perfil para un lienzo de `alto` líneas (más es mejor).
double puntaje(const json& p, int alto) {
    if (p.value("uri", std::string()).empty()) return -1e9;
    const std::string c = p.value("codec", std::string());
    const int h = p.value("alto", 0);
    double s = 0;
    // Cubrir el lienzo es lo principal; pasarse cuesta (decodificar de más).
    if (h >= alto)
        s += 1000 - (h - alto) * 0.5;
    else
        s += 500 - (alto - h) * 1.0; // no llega: el más grande que haya
    // El códec: por hardware antes que por CPU.
    if (c == "H264" || c == "H.264") s += 60;
    else if (c == "H265" || c == "H.265" || c == "HEVC") s += 50;
    else if (c == "JPEG" || c == "MJPEG") s -= 120;
    return s;
}
std::string n0(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }
} // namespace

json planDeSinCuidar(const json& ficha, const json& analisis, const json& lienzo);

int elegirPerfil(const json& perfiles, int altoLienzo) {
    int mejor = -1;
    double pm = -1e18;
    for (size_t i = 0; i < perfiles.size(); ++i) {
        const double s = puntaje(perfiles[i], altoLienzo);
        if (s > pm) {
            pm = s;
            mejor = static_cast<int>(i);
        }
    }
    return pm <= -1e9 ? -1 : mejor;
}

json planDe(const json& ficha, const json& analisis, const json& lienzo) {
    // Nunca tira: un dato con otro tipo (un bool que vino como número) no
    // puede tumbar al que lo llama.
    try {
        return planDeSinCuidar(ficha, analisis, lienzo);
    } catch (const std::exception& e) {
        return {{"error", std::string("no se pudo armar el plan: ") + e.what()}, {"pasos", json::array()}, {"argumentos", json::object()}};
    }
}

json planDeSinCuidar(const json& ficha, const json& analisis, const json& lienzo) {
    json plan{{"pasos", json::array()}, {"argumentos", json::object()}};
    auto& pasos = plan["pasos"];
    const int altoL = lienzo.value("alto", 1080);
    const double fpsL = lienzo.value("fps", 30.0);
    const json perfiles = ficha.value("perfiles", json::array());
    const int i = elegirPerfil(perfiles, altoL);
    json args{{"ip", ficha.value("ip", std::string())}};
    if (i >= 0) {
        const json& p = perfiles[static_cast<size_t>(i)];
        plan["perfil"] = p;
        args["perfil"] = p.value("token", std::string());
        const std::string c = p.value("codec", std::string());
        pasos.push_back("Perfil «" + p.value("nombre", std::string()) + "»: " + n0(p.value("ancho", 0)) + "x" + n0(p.value("alto", 0)) +
                        " " + c + " a " + n0(p.value("fps", 0.0)) + " fps (el más chico que cubre el lienzo de " +
                        n0(altoL) + " líneas" + (c == "JPEG" || c == "MJPEG" ? "; ojo: MJPEG se decodifica por CPU" : ", se decodifica por hardware") +
                        ")");
    } else if (!ficha.value("url", std::string()).empty()) {
        args["url"] = ficha["url"];
        pasos.push_back("Sin perfiles ONVIF: se usa la URL que contestó (" + ficha.value("url", std::string()) + ")");
    } else {
        plan["error"] = "no hay ninguna URL de video: hacen falta usuario y clave (camara_credenciales) o la URL a mano";
        return plan;
    }
    // Normalizar: solo si hace falta (lo que libobs no sabe hacer).
    const json o = analisis.value("video", json::object());
    std::string por;
    if (o.value("entrelazado", false)) por += "viene entrelazada; ";
    if (o.value("pixel", std::string("cuadrado")) != "cuadrado") por += "píxel no cuadrado (" + o.value("pixel", std::string()) + "); ";
    const double fps = o.value("fps", 0.0);
    if (o.value("fps_variable", false) || fps == 0) por += "cuadros por segundo variables; ";
    else if (std::fabs(fps - fpsL) > 1.5) por += "manda " + n0(fps) + " fps y el estudio va a " + n0(fpsL) + "; ";
    const bool normalizar = !por.empty() || analisis.empty();
    args["normalizar"] = normalizar;
    if (analisis.empty())
        pasos.push_back("Normalizar por las dudas (no se pudo analizar el stream antes): GStreamer desentrelaza si hace falta, "
                        "lleva a los cuadros del estudio y a píxel cuadrado");
    else if (normalizar)
        pasos.push_back("Normalizar con GStreamer porque " + por.substr(0, por.size() - 2) +
                        " (desentrelaza, lleva a los cuadros del estudio y a píxel cuadrado)");
    else
        pasos.push_back("Sin normalizar: ya viene progresiva, con píxel cuadrado y a los cuadros del estudio (cero CPU extra)");
    pasos.push_back("El color (" + o.value("matriz", std::string("lo que declare")) + ", rango " + o.value("rango", std::string("el que declare")) +
                    ") lo convierte libobs en la GPU: no hace falta tarjeta");
    if (o.value("hdr", false)) pasos.push_back("OJO: manda HDR y el estudio es SDR: pasar la cámara a SDR (Rec.709)");
    // Latencia: una PTZ se maneja mirando la imagen; cada ms de colchón es un
    // ms más entre mover el joystick y ver que se movió.
    const bool esPtz = ficha.value("ptz", json::object()).value("tiene", false);
    args["latencia_ms"] = esPtz ? 60 : 200;
    pasos.push_back(esPtz ? "Colchón de 60 ms (es PTZ: se maneja mirando la imagen; con Wi-Fi o por internet, 300-600)"
                          : "Colchón de 200 ms (red local cableada; con Wi-Fi o por internet, 400-800)");
    // PTZ.
    const json ptz = ficha.value("ptz", json::object());
    if (ficha.contains("onvif") && !ficha.value("onvif", true)) {
        // ONVIF no contestó (pide clave): NO se sabe si tiene PTZ.
        args["ptz"] = false;
        plan["ptz"] = "desconocido";
        pasos.push_back("PTZ: no se sabe (ONVIF pide usuario y clave). Con la clave: camara_ficha y camara_agregar con "
                        "`solo_ptz`. Mientras, el PTZ digital (recorte y zoom en la GPU)");
    } else if (!ptz.empty() && ptz.value("tiene", false)) {
        args["ptz"] = true;
        std::string perfilPtz = ptz.value("perfil", std::string());
        pasos.push_back("PTZ por ONVIF" + (perfilPtz.empty() ? std::string() : " con el perfil «" + perfilPtz + "»") +
                        (ptz.contains("presets") ? " (" + n0(ptz["presets"].size()) + " presets)" : std::string()) +
                        (ptz.value("home", false) ? ", con posición de inicio" : std::string()));
    } else {
        args["ptz"] = false;
        pasos.push_back("Sin PTZ por ONVIF (queda el PTZ digital: recorte y zoom en la GPU)");
    }
    plan["argumentos"] = args;
    return plan;
}

} // namespace ba::app::red
