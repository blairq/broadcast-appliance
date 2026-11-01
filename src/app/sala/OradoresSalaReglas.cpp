// src/app/sala/OradoresSalaReglas.cpp — lo que no habla con la sala (ver OradoresSala.hpp):
// la regla del reloj, el texto de un tiempo, leer un tiempo escrito y la fila en orden. Va
// aparte para probarlo sin la sala (tests/test_oradores_sala.cpp).

#include "sala/OradoresSala.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <regex>

namespace ba::app::sala {

std::string OradoresSala::textoTiempo(double ms) {
    const bool neg = ms < 0;
    const long long t = static_cast<long long>(std::floor(std::abs(ms) / 1000.0));
    char b[32];
    std::snprintf(b, sizeof b, "%s%lld:%02lld", neg ? "+" : "", t / 60, t % 60);
    return b;
}

OradoresSala::Reloj OradoresSala::reloj(int asignadoS, double alAireDesdeMs, double ahoraMs) {
    Reloj r;
    r.restanteMs = asignadoS * 1000.0 - (ahoraMs - alAireDesdeMs);
    r.estado = r.restanteMs < 0 ? "excedido" : r.restanteMs <= 10000 ? "aviso" : "ok";
    // Lo que queda se muestra redondeado para arriba (a «0:01» le falta menos de un segundo).
    r.texto = textoTiempo(r.restanteMs >= 0 ? std::ceil(r.restanteMs / 1000.0) * 1000.0 : r.restanteMs);
    return r;
}

std::string OradoresSala::leerTiempo(const std::string& textoEntero, int actual, int& segundos) {
    std::string t = textoEntero;
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
    int signo = 0;
    if (!t.empty() && (t.front() == '+' || t.front() == '-' || t.front() == '=')) {
        signo = t.front() == '+' ? 1 : t.front() == '-' ? -1 : 0;
        t.erase(0, 1);
        t.erase(0, t.find_first_not_of(' '));
    }
    std::replace(t.begin(), t.end(), ',', '.');
    static const std::regex mss(R"(^(\d{1,5}):(\d{1,2})$)");
    static const std::regex seg(R"(^(\d{1,6})(?:\.(\d{1,3}))?\s*(s|seg|segundos?)$)", std::regex::icase);
    // Un número solo son MINUTOS (lo que escribe el operador: «3» es un tiempo de tres minutos).
    static const std::regex min(R"(^(\d{1,5})(?:\.(\d{1,3}))?\s*(m|min|minutos?)?$)", std::regex::icase);
    // (Sin stod: depende del locale, y con el de la interfaz «1.5» sería 1.)
    const auto numero = [](const std::string& entero, const std::string& decimales) {
        double v = std::stoi(entero), escala = 0.1;
        for (const char c : decimales) {
            v += (c - '0') * escala;
            escala /= 10;
        }
        return v;
    };
    std::smatch m;
    double valor = 0;
    if (std::regex_match(t, m, mss)) valor = std::stoi(m[1]) * 60 + std::stoi(m[2]);
    else if (std::regex_match(t, m, min)) valor = numero(m[1], m[2]) * 60;
    else if (std::regex_match(t, m, seg)) valor = numero(m[1], m[2]);
    else return ba::i18n::traducir("sala", "no se entiende el tiempo: minutos («3»), «m:ss» («0:45») o segundos («45s»); con + o - suma o resta");
    const int s = static_cast<int>(std::lround(valor));
    segundos = signo == 0 ? s : actual + signo * s;
    if (segundos <= 0) return ba::i18n::traducir("sala", "el tiempo tiene que ser de más de cero segundos");
    return {};
}

nlohmann::json OradoresSala::fila(const nlohmann::json& foto) {
    // (Los participantes se copian una vez: `value` devuelve una copia y sus punteros no
    // vivirían más que el bucle.)
    const nlohmann::json participantes = foto.value("participants", nlohmann::json::array());
    std::map<std::string, const nlohmann::json*> porId;
    for (const auto& p : participantes)
        if (p.is_object()) porId[p.value("id", std::string())] = &p;
    nlohmann::json out = nlohmann::json::array();
    int lugar = 0;
    for (const auto& id : foto.value("order", nlohmann::json::array())) {
        if (!id.is_string()) continue;
        const auto it = porId.find(id.get<std::string>());
        if (it == porId.end()) continue;
        nlohmann::json p = *it->second;
        p["position"] = p.value("speaker", std::string()) == "queued" ? ++lugar : 0;
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace ba::app::sala
