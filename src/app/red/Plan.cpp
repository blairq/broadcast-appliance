// src/app/red/Plan.cpp — ver Plan.hpp

#include "red/Plan.hpp"
#include "i18n/I18n.hpp"

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
    if (c == "H264" || c == "H.264")
        s += 60;
    else if (c == "H265" || c == "H.265" || c == "HEVC")
        s += 50;
    else if (c == "JPEG" || c == "MJPEG")
        s -= 120;
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
        return {{"error", ba::i18n::llenar(ba::i18n::traducir("red", "no se pudo armar el plan: %1"), {e.what()})},
                {"pasos", json::array()},
                {"argumentos", json::object()}};
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
        pasos.push_back(ba::i18n::llenar(
            c == "JPEG" || c == "MJPEG"
                ? ba::i18n::traducir("red", "Perfil «%1»: %2x%3 %4 a %5 fps (el más chico que cubre el lienzo de %6 líneas; "
                                            "ojo: MJPEG se decodifica por CPU)")
                : ba::i18n::traducir("red", "Perfil «%1»: %2x%3 %4 a %5 fps (el más chico que cubre el lienzo de %6 líneas, "
                                            "se decodifica por hardware)"),
            {p.value("nombre", std::string()), n0(p.value("ancho", 0)), n0(p.value("alto", 0)), c,
             n0(p.value("fps", 0.0)), n0(altoL)}));
    } else if (!ficha.value("url", std::string()).empty()) {
        args["url"] = ficha["url"];
        pasos.push_back(ba::i18n::llenar(ba::i18n::traducir("red", "Sin perfiles ONVIF: se usa la URL que contestó (%1)"),
                                         {ficha.value("url", std::string())}));
    } else {
        plan["error"] = ba::i18n::traducir(
            "asistente", "no hay ninguna URL de video: hacen falta usuario y clave (camara_credenciales) o la URL a mano");
        return plan;
    }
    // Normalizar: solo si hace falta (lo que libobs no sabe hacer).
    const json o = analisis.value("video", json::object());
    std::string por;
    if (o.value("entrelazado", false)) por += ba::i18n::traducir("red", "viene entrelazada") + "; ";
    if (o.value("pixel", std::string("cuadrado")) != "cuadrado")
        por += ba::i18n::llenar(ba::i18n::traducir("red", "píxel no cuadrado (%1)"), {o.value("pixel", std::string())}) + "; ";
    const double fps = o.value("fps", 0.0);
    if (o.value("fps_variable", false) || fps == 0)
        por += ba::i18n::traducir("red", "cuadros por segundo variables") + "; ";
    else if (std::fabs(fps - fpsL) > 1.5)
        por += ba::i18n::llenar(ba::i18n::traducir("red", "manda %1 fps y el estudio va a %2"), {n0(fps), n0(fpsL)}) + "; ";
    const bool normalizar = !por.empty() || analisis.empty();
    args["normalizar"] = normalizar;
    if (analisis.empty())
        pasos.push_back(ba::i18n::traducir("red", "Normalizar por las dudas (no se pudo analizar el stream antes): GStreamer "
                                                  "desentrelaza si hace falta, lleva a los cuadros del estudio y a píxel "
                                                  "cuadrado"));
    else if (normalizar)
        pasos.push_back(ba::i18n::llenar(ba::i18n::traducir("red", "Normalizar con GStreamer porque %1 (desentrelaza, lleva "
                                                                   "a los cuadros del estudio y a píxel cuadrado)"),
                                         {por.substr(0, por.size() - 2)}));
    else
        pasos.push_back(ba::i18n::traducir("red", "Sin normalizar: ya viene progresiva, con píxel cuadrado y a los cuadros "
                                                  "del estudio (cero CPU extra)"));
    pasos.push_back(ba::i18n::llenar(ba::i18n::traducir("red", "El color (%1, rango %2) lo convierte libobs en la GPU: no "
                                                               "hace falta tarjeta"),
                                     {o.value("matriz", ba::i18n::traducir("red", "lo que declare")),
                                      o.value("rango", ba::i18n::traducir("red", "el que declare"))}));
    if (o.value("hdr", false))
        pasos.push_back(ba::i18n::traducir("red", "OJO: manda HDR y el estudio es SDR: pasar la cámara a SDR (Rec.709)"));
    // Latencia: una PTZ se maneja mirando la imagen; cada ms de colchón es un
    // ms más entre mover el joystick y ver que se movió.
    const bool esPtz = ficha.value("ptz", json::object()).value("tiene", false);
    args["latencia_ms"] = esPtz ? 60 : 200;
    pasos.push_back(
        esPtz ? ba::i18n::traducir("red", "Colchón de 60 ms (es PTZ: se maneja mirando la imagen; con Wi-Fi o por internet, "
                                          "300-600)")
              : ba::i18n::traducir("red", "Colchón de 200 ms (red local cableada; con Wi-Fi o por internet, 400-800)"));
    // PTZ.
    const json ptz = ficha.value("ptz", json::object());
    if (ficha.contains("onvif") && !ficha.value("onvif", true)) {
        // ONVIF no contestó (pide clave): NO se sabe si tiene PTZ.
        args["ptz"] = false;
        plan["ptz"] = "desconocido";
        pasos.push_back(ba::i18n::traducir(
            "asistente", "PTZ: no se sabe (ONVIF pide usuario y clave). Con la clave: camara_ficha y camara_agregar con "
                         "`solo_ptz`. Mientras, el PTZ digital (recorte y zoom en la GPU)"));
    } else if (!ptz.empty() && ptz.value("tiene", false)) {
        args["ptz"] = true;
        std::string perfilPtz = ptz.value("perfil", std::string());
        // Las partes que se suman son detalles entre paréntesis o tras coma (cada una completa).
        std::string t = perfilPtz.empty()
                            ? ba::i18n::traducir("red", "PTZ por ONVIF")
                            : ba::i18n::llenar(ba::i18n::traducir("red", "PTZ por ONVIF con el perfil «%1»"), {perfilPtz});
        if (ptz.contains("presets"))
            t += " " + ba::i18n::llenar(ba::i18n::traducir("red", "(%1 presets)"), {n0(ptz["presets"].size())});
        if (ptz.value("home", false)) t += ba::i18n::traducir("red", ", con posición de inicio");
        pasos.push_back(t);
    } else {
        args["ptz"] = false;
        pasos.push_back(ba::i18n::traducir("red", "Sin PTZ por ONVIF (queda el PTZ digital: recorte y zoom en la GPU)"));
    }
    plan["argumentos"] = args;
    return plan;
}

} // namespace ba::app::red
