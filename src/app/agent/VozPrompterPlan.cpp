// src/app/agent/VozPrompterPlan.cpp — ver VozPrompterPlan.hpp

#include "agent/VozPrompterPlan.hpp"

#include <algorithm>

namespace ba::app::agent::prompter_voz {

namespace {

bool esEspacio(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// Termina una frase: punto, signos, puntos suspensivos (también «…» en UTF-8).
bool cierraFrase(const std::string& w) {
    if (w.empty()) return false;
    const char c = w.back();
    if (c == '.' || c == '!' || c == '?' || c == ':' || c == ';') return true;
    if (w.size() >= 3 && w.compare(w.size() - 3, 3, "\xE2\x80\xA6") == 0) return true; // …
    if ((c == '"' || c == ')' || static_cast<unsigned char>(c) == 0xBB) && w.size() >= 2) {
        const char d = w[w.size() - 2];
        return d == '.' || d == '!' || d == '?';
    }
    return false;
}

} // namespace

std::vector<Palabra> palabras(const std::string& texto) {
    std::vector<Palabra> out;
    bool dentro = false;
    for (std::size_t i = 0; i < texto.size(); ++i) {
        if (esEspacio(static_cast<unsigned char>(texto[i]))) {
            if (dentro) out.back().fin = i;
            dentro = false;
        } else if (!dentro) {
            dentro = true;
            out.push_back({i, texto.size()});
        }
    }
    return out;
}

std::vector<Frase> frases(const std::string& texto, const std::vector<Palabra>& ps, int maxPalabras) {
    std::vector<Frase> out;
    int desde = 0;
    const int n = static_cast<int>(ps.size());
    for (int i = 0; i < n; ++i) {
        const std::string w = texto.substr(ps[i].ini, ps[i].fin - ps[i].ini);
        // Un párrafo nuevo (línea en blanco) también corta.
        bool parrafo = false;
        if (i + 1 < n) {
            const std::string entre = texto.substr(ps[i].fin, ps[i + 1].ini - ps[i].fin);
            parrafo = std::count(entre.begin(), entre.end(), '\n') >= 2;
        }
        const int largo = i + 1 - desde;
        const bool coma = !w.empty() && w.back() == ',' && largo >= maxPalabras / 2;
        if (cierraFrase(w) || parrafo || coma || largo >= maxPalabras || i + 1 == n) {
            out.push_back({desde, i + 1});
            desde = i + 1;
        }
    }
    return out;
}

std::string textoDe(const std::string& texto, const std::vector<Palabra>& ps, int desde, int hasta) {
    desde = std::clamp(desde, 0, static_cast<int>(ps.size()));
    hasta = std::clamp(hasta, desde, static_cast<int>(ps.size()));
    if (desde == hasta) return {};
    return texto.substr(ps[static_cast<std::size_t>(desde)].ini, ps[static_cast<std::size_t>(hasta - 1)].fin -
                                                                     ps[static_cast<std::size_t>(desde)].ini);
}

double posicionEn(const Estado& e, double ms) {
    if (!e.corriendo || e.ppm <= 0) return e.posicion;
    return e.posicion + (ms - e.desdeMs) / 60000.0 * e.ppm;
}

double llegaEn(const Estado& e, double w) {
    if (e.ppm <= 0) return e.desdeMs;
    return e.desdeMs + (w - e.posicion) * 60000.0 / e.ppm;
}

double velocidadPara(int nPalabras, int ppm, double naturalPps) {
    if (nPalabras <= 0 || ppm <= 0 || naturalPps <= 0) return 1.0;
    const double pedida = (ppm / 60.0) / naturalPps;
    return std::clamp(pedida, 0.6, 2.2);
}

} // namespace ba::app::agent::prompter_voz
