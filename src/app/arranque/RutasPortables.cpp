// src/app/arranque/RutasPortables.cpp — ver RutasPortables.hpp

#include "arranque/RutasPortables.hpp"

#include "platform/Paths.hpp"

#include <QUrl>

#include <cctype>
#include <filesystem>
#include <string>

namespace ba::app {

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

/// Carpetas de los datos (de la configuración y de la máquina) que puede nombrar una ruta guardada.
bool empiezaPorCarpetaDeDatos(const std::string& s) {
    static const char* const kMaquina[] = {"Modelos", "Herramientas", "Agentes"};
    const auto barra = s.find('/');
    if (barra == std::string::npos || barra == 0) return false;
    const std::string primera = s.substr(0, barra);
    for (const char* c : ba::platform::kCarpetasDeConfiguracion)
        if (primera == c) return true;
    for (const char* c : kMaquina)
        if (primera == c) return true;
    return false;
}

bool pareceRutaAbsoluta(const std::string& s) {
    if (s.size() < 2) return false;
    if (s[0] == '/') return true;
    return s.size() > 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}

std::string relativa(const std::string& s) {
    if (s.rfind("file://", 0) == 0) {
        const QString local = QUrl(QString::fromStdString(s)).toLocalFile();
        if (local.isEmpty()) return s;
        const std::string r = ba::platform::rutaGuardable(fs::path(local.toStdWString()));
        return pareceRutaAbsoluta(r) ? s : "file:" + r;
    }
    if (!pareceRutaAbsoluta(s)) return s;
    const std::string r = ba::platform::rutaGuardable(fs::path(std::u8string(s.begin(), s.end())));
    return pareceRutaAbsoluta(r) ? s : r;
}

std::string absoluta(const std::string& s) {
    if (s.rfind("file:", 0) == 0 && s.rfind("file://", 0) != 0) {
        const std::string r = s.substr(5);
        if (!empiezaPorCarpetaDeDatos(r)) return s;
        return QUrl::fromLocalFile(QString::fromStdWString(ba::platform::rutaDeDatos(r).wstring())).toString().toStdString();
    }
    if (empiezaPorCarpetaDeDatos(s)) {
        const std::u8string u = ba::platform::rutaDeDatos(s).u8string();
        return std::string(u.begin(), u.end());
    }
    // Una absoluta de antes de mover la carpeta: la de ahora.
    if (pareceRutaAbsoluta(s)) {
        const std::u8string u = ba::platform::rutaDeDatos(s).u8string();
        return std::string(u.begin(), u.end());
    }
    return s;
}

/// Adentro de un pipeline de GStreamer («filesrc location=/…/Media/x.png ! …»): cada
/// valor de location= pasa por `f` (con o sin comillas).
std::string enPipeline(const std::string& s, std::string (*f)(const std::string&)) {
    if (s.find("location=") == std::string::npos || s.find('!') == std::string::npos) return s;
    std::string out;
    std::size_t i = 0;
    while (true) {
        const auto k = s.find("location=", i);
        if (k == std::string::npos) {
            out += s.substr(i);
            break;
        }
        const std::size_t v = k + 9;
        out += s.substr(i, v - i);
        if (v < s.size() && s[v] == '"') {
            const auto fin = s.find('"', v + 1);
            if (fin == std::string::npos) {
                out += s.substr(v);
                break;
            }
            out += '"' + f(s.substr(v + 1, fin - v - 1)) + '"';
            i = fin + 1;
        } else {
            std::size_t fin = v;
            while (fin < s.size() && s[fin] != ' ' && s[fin] != '!') ++fin;
            const std::string r = f(s.substr(v, fin - v));
            out += r.find(' ') != std::string::npos ? '"' + r + '"' : r; // la carpeta de datos tiene espacios
            i = fin;
        }
    }
    return out;
}

json recorrer(const json& j, std::string (*f)(const std::string&)) {
    if (j.is_string()) {
        const std::string s = j.get<std::string>();
        const std::string conPipeline = enPipeline(s, f);
        return conPipeline != s ? conPipeline : f(s);
    }
    if (j.is_array()) {
        json out = json::array();
        for (const auto& x : j) out.push_back(recorrer(x, f));
        return out;
    }
    if (j.is_object()) {
        json out = json::object();
        for (auto it = j.begin(); it != j.end(); ++it) out[it.key()] = recorrer(it.value(), f);
        return out;
    }
    return j;
}
} // namespace

json rutasRelativas(const json& j) { return recorrer(j, &relativa); }
json rutasAbsolutas(const json& j) { return recorrer(j, &absoluta); }

} // namespace ba::app
