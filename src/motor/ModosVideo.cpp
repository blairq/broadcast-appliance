// ============================================================================
// src/motor/ModosVideo.cpp — ver ModosVideo.hpp
// ============================================================================
#include "ModosVideo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace ba::motor {

using json = nlohmann::json;

namespace {

std::string numero(double f) {
    char b[16];
    if (std::fabs(f - std::round(f)) < 0.01) std::snprintf(b, sizeof b, "%d", static_cast<int>(std::lround(f)));
    else std::snprintf(b, sizeof b, "%.2f", f);
    return b;
}

/// Los cps de un framerate (fijo, lista o rango): agrega a `out`; `rango` si es un rango.
void cpsDe(const GValue* v, std::vector<double>& out, bool& rango) {
    if (v == nullptr) return;
    if (GST_VALUE_HOLDS_FRACTION(v)) {
        const int n = gst_value_get_fraction_numerator(v), d = gst_value_get_fraction_denominator(v);
        if (d > 0 && n > 0) out.push_back(static_cast<double>(n) / d);
    } else if (GST_VALUE_HOLDS_FRACTION_RANGE(v)) {
        rango = true;
        cpsDe(gst_value_get_fraction_range_min(v), out, rango);
        cpsDe(gst_value_get_fraction_range_max(v), out, rango);
    } else if (GST_VALUE_HOLDS_LIST(v)) {
        for (guint i = 0; i < gst_value_list_get_size(v); ++i) cpsDe(gst_value_list_get_value(v, i), out, rango);
    }
}

std::string formatoDe(const GstStructure* st) {
    const char* n = gst_structure_get_name(st);
    if (g_str_equal(n, "video/x-raw")) return "crudo";
    if (g_str_equal(n, "image/jpeg")) return "mjpeg";
    if (g_str_equal(n, "video/x-h264")) return "h264";
    if (g_str_equal(n, "video/x-h265")) return "h265";
    return {};
}

std::string capsComprimidas(const std::string& formato) {
    if (formato == "mjpeg") return "image/jpeg";
    if (formato == "h264") return "video/x-h264";
    if (formato == "h265") return "video/x-h265";
    return {};
}

/// Lo que cuesta, para desempatar entre modos que sirven igual.
double costo(const ModoVideo& m) {
    if (m.formato == "crudo") return m.convertido ? 2.5 : 0;
    if (m.formato == "mjpeg") return m.gpu ? 1 : 2;
    return (m.gpu ? 3 : 6) + (m.formato == "h265" ? 0.5 : 0); // H.264/H.265: la latencia del GOP
}

/// Los cps que se le piden a ese modo para `quiero`: los mismos si los da; si no, los
/// menores que pasen (videorate baja después sin repetir cuadros); si no, los más altos.
double cpsPara(const ModoVideo& m, double quiero) {
    if (m.cps.empty()) return 0;
    if (m.rango) return std::clamp(quiero, m.cps.front(), m.cps.back());
    for (double c : m.cps)
        if (std::fabs(c - quiero) < 0.05) return c;
    for (double c : m.cps)
        if (c > quiero) return c;
    return m.cps.back();
}

} // namespace

std::string ModoVideo::valor() const {
    std::string v = std::to_string(ancho) + "x" + std::to_string(alto);
    if (fps > 0) v += "@" + numero(fps);
    if (formato != "crudo") v += ":" + formato;
    return v;
}

std::string ModoVideo::etiqueta() const {
    std::string e = std::to_string(ancho) + "×" + std::to_string(alto);
    if (fps > 0) e += " · " + numero(fps) + " cps";
    if (formato == "crudo") {
        e += " · crudo" + (pixel.empty() ? std::string() : " " + pixel);
        if (convertido) e += " (lo convierte el sistema en la CPU: la cámara lo manda comprimido)";
    } else {
        std::string f = formato == "mjpeg" ? "MJPEG" : formato == "h264" ? "H.264" : "H.265";
        e += " · " + f;
        e += decodificador.empty() ? " (no hay decodificador)"
                                   : std::string(" (decodifica ") + (gpu ? "la GPU" : "la CPU") + ": " + decodificador + ")";
    }
    return e;
}

json ModoVideo::json() const {
    nlohmann::json j{{"valor", valor()},     {"etiqueta", etiqueta()}, {"ancho", ancho},  {"alto", alto},
                     {"cps", fps},           {"formato", formato},     {"usable", usable()}};
    if (!pixel.empty()) j["pixel"] = pixel;
    if (!decodificador.empty()) j["decodificador"] = decodificador, j["gpu"] = gpu;
    if (convertido) j["convertido_por_el_sistema"] = true;
    if (!cps.empty()) j["cps_posibles"] = rango ? nlohmann::json{{"desde", cps.front()}, {"hasta", cps.back()}} : nlohmann::json(cps);
    return j;
}

std::pair<std::string, bool> decodificadorPara(const std::string& capsTexto) {
    static std::mutex mu;
    static std::map<std::string, std::pair<std::string, bool>> cache;
    std::lock_guard<std::mutex> l(mu);
    if (const auto it = cache.find(capsTexto); it != cache.end()) return it->second;
    std::pair<std::string, bool> r{"", false};
    GstCaps* caps = gst_caps_from_string(capsTexto.c_str());
    if (caps != nullptr) {
        // Todos los decodificadores que acepten esas caps, por rango (el registro ya sabe
        // cuál conviene en esta máquina: D3D12/VA/VideoToolbox arriba de los de CPU).
        GList* todos = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL);
        GList* sirven = gst_element_factory_list_filter(todos, caps, GST_PAD_SINK, FALSE);
        sirven = g_list_sort(sirven, gst_plugin_feature_rank_compare_func);
        for (GList* x = sirven; x != nullptr; x = x->next) {
            auto* f = GST_ELEMENT_FACTORY(x->data);
            const std::string nombre = GST_OBJECT_NAME(f);
            const gchar* k = gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_KLASS);
            const std::string clase = k != nullptr ? k : "";
            if (clase.find("Video") == std::string::npos && clase.find("Image") == std::string::npos) continue;
            r = {nombre, clase.find("Hardware") != std::string::npos || nombre.rfind("vtdec", 0) == 0};
            break;
        }
        gst_plugin_feature_list_free(sirven);
        gst_plugin_feature_list_free(todos);
        gst_caps_unref(caps);
    }
    cache[capsTexto] = r;
    return r;
}

std::vector<ModoVideo> modosDe(GstCaps* caps) {
    std::vector<ModoVideo> out;
    if (caps == nullptr) return out;
    // Juntar por tamaño y formato (y, los crudos, por formato de píxel: después se ve cuáles convierte el sistema).
    struct Junta {
        std::set<double> cps;
        bool rango = false;
    };
    std::map<std::tuple<int, int, std::string, std::string>, Junta> juntas;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        GstCapsFeatures* ft = gst_caps_get_features(caps, i);
        if (ft != nullptr && !gst_caps_features_is_any(ft) && !gst_caps_features_is_equal(ft, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))
            continue;
        const GstStructure* st = gst_caps_get_structure(caps, i);
        const std::string formato = formatoDe(st);
        int w = 0, h = 0;
        if (formato.empty() || !gst_structure_get_int(st, "width", &w) || !gst_structure_get_int(st, "height", &h) || w <= 0 || h <= 0)
            continue;
        std::string pixel;
        if (formato == "crudo")
            if (const char* f = gst_structure_get_string(st, "format")) pixel = f;
        Junta& j = juntas[{w, h, formato, pixel}];
        std::vector<double> c;
        cpsDe(gst_structure_get_value(st, "framerate"), c, j.rango);
        j.cps.insert(c.begin(), c.end());
    }
    // Por tamaño: los cps máximos de los comprimidos y de cada crudo.
    std::map<std::pair<int, int>, double> maxComprimido, minCrudo;
    for (const auto& [k, j] : juntas) {
        const auto& [w, h, formato, pixel] = k;
        const double mx = j.cps.empty() ? 0 : *j.cps.rbegin();
        if (formato == "crudo") {
            auto it = minCrudo.find({w, h});
            minCrudo[{w, h}] = it == minCrudo.end() ? mx : std::min(it->second, mx);
        } else {
            maxComprimido[{w, h}] = std::max(maxComprimido[{w, h}], mx);
        }
    }
    // Los modos: los comprimidos uno por formato; los crudos, juntos los de verdad y aparte
    // los que fabrica el sistema (llegan a más cps que los otros crudos de ese tamaño, tantos
    // como el comprimido, o más de lo que entra crudo por un USB 2 habiendo comprimido).
    std::map<std::tuple<int, int, std::string, bool>, ModoVideo> modos;
    for (const auto& [k, j] : juntas) {
        const auto& [w, h, formato, pixel] = k;
        if (j.cps.empty()) continue;
        const double mx = *j.cps.rbegin();
        bool convertido = false;
        if (formato == "crudo") {
            const auto mc = maxComprimido.find({w, h});
            if (mc != maxComprimido.end() && mx >= mc->second - 0.05) {
                const bool masQueOtros = mx > minCrudo[{w, h}] + 0.05;
                const double bytes = static_cast<double>(w) * h * 2 * mx; // como YUY2
                convertido = masQueOtros || bytes > 40e6;
            }
        }
        ModoVideo& m = modos[{w, h, formato, convertido}];
        m.ancho = w;
        m.alto = h;
        m.formato = formato;
        m.convertido = convertido;
        if (!pixel.empty()) m.pixel += (m.pixel.empty() ? "" : "/") + pixel;
        std::set<double> todos(m.cps.begin(), m.cps.end());
        todos.insert(j.cps.begin(), j.cps.end());
        m.cps.assign(todos.begin(), todos.end());
        m.rango = m.rango || j.rango;
        m.fps = m.cps.back();
        if (formato != "crudo" && m.decodificador.empty()) {
            const auto d = decodificadorPara(capsComprimidas(formato));
            m.decodificador = d.first;
            m.gpu = d.second;
        }
    }
    for (auto& [k, m] : modos) out.push_back(std::move(m));
    std::sort(out.begin(), out.end(), [](const ModoVideo& a, const ModoVideo& b) {
        if (a.ancho * a.alto != b.ancho * b.alto) return a.ancho * a.alto > b.ancho * b.alto;
        if (std::fabs(a.fps - b.fps) > 0.05) return a.fps > b.fps;
        return costo(a) < costo(b);
    });
    return out;
}

ModoVideo elegirModo(const std::vector<ModoVideo>& modos, const PedidoVideo& p, std::string* porque) {
    const ModoVideo* mejor = nullptr;
    const double prop = p.alto > 0 ? static_cast<double>(p.ancho) / p.alto : 0;
    // La clave, de lo más importante a lo menos: cps, proporción, que cubra, sobrante, costo.
    const auto clave = [&](const ModoVideo& m) {
        const bool cpsBien = cpsPara(m, p.fps) >= p.fps - 0.5;
        const bool propBien = prop <= 0 || std::fabs(static_cast<double>(m.ancho) / m.alto - prop) < 0.02;
        const bool cubre = p.ancho <= 0 || (m.ancho >= p.ancho && m.alto >= p.alto);
        const double area = static_cast<double>(m.ancho) * m.alto;
        return std::make_tuple(!cpsBien, !propBien, !cubre, cubre ? area : -area, costo(m));
    };
    for (const auto& m : modos) {
        if (!m.usable()) continue;
        if (mejor == nullptr || clave(m) < clave(*mejor)) mejor = &m;
    }
    if (mejor == nullptr) {
        if (porque != nullptr) *porque = "ningún modo que se pueda usar";
        return {};
    }
    ModoVideo r = *mejor;
    r.fps = cpsPara(r, p.fps);
    if (porque != nullptr) {
        const auto [noCps, noProp, noCubre, area, c] = clave(r);
        std::string t = p.ancho > 0 ? "para " + std::to_string(p.ancho) + "×" + std::to_string(p.alto) + " a " + numero(p.fps) + " cps: "
                                    : "";
        t += noCubre ? "el más grande que hay (ninguno llega al tamaño pedido)" : "el más chico que cubre el tamaño";
        if (noCps) t += "; ningún modo de ese tamaño da los cps pedidos";
        if (noProp) t += "; con otra proporción (se recorta o se ajusta en la escena)";
        t += r.formato == "crudo" ? (r.convertido ? "; crudo, pero lo convierte el sistema" : "; crudo (sin decodificar)")
                                  : "; " + r.formato + (r.gpu ? " decodificado en la GPU" : " decodificado en la CPU") + " (" +
                                        r.decodificador + ")";
        *porque = t;
    }
    return r;
}

ModoVideo modoGuardado(const std::vector<ModoVideo>& modos, const std::string& valor) {
    int w = 0, h = 0;
    double f = 0;
    char fmt[16] = {0};
    const int n = std::sscanf(valor.c_str(), "%dx%d@%lf:%15s", &w, &h, &f, fmt);
    if (n < 2 || w <= 0 || h <= 0) return {};
    const std::string formato = fmt[0] != 0 ? fmt : "";
    const ModoVideo* mejor = nullptr;
    for (const auto& m : modos) {
        if (!m.usable() || m.ancho != w || m.alto != h) continue;
        if (!formato.empty() && m.formato != formato) continue;
        // Sin formato (lo guardado antes de esto, o «1920x1080@30» a mano): el que dé esos cps, el más barato.
        const bool da = f <= 0 || cpsPara(m, f) >= f - 0.5;
        if (mejor == nullptr) {
            mejor = &m;
            continue;
        }
        const bool daMejor = f <= 0 || cpsPara(*mejor, f) >= f - 0.5;
        if (std::make_tuple(!da, costo(m)) < std::make_tuple(!daMejor, costo(*mejor))) mejor = &m;
    }
    if (mejor == nullptr) return {};
    ModoVideo r = *mejor;
    r.fps = f > 0 ? cpsPara(r, f) : r.fps;
    return r;
}

std::string cadenaDeModo(const ModoVideo& m) {
    if (m.ancho <= 0 || m.alto <= 0) return {};
    std::string c = " ! ";
    if (m.formato == "crudo") c += "video/x-raw";
    else c += capsComprimidas(m.formato);
    c += ",width=" + std::to_string(m.ancho) + ",height=" + std::to_string(m.alto);
    if (m.fps > 0) {
        // Con un poco de margen: «30» tiene que aceptar 30000/1001 (29,97).
        const int a = static_cast<int>(std::floor(m.fps * 100 - 50)), b = static_cast<int>(std::ceil(m.fps * 100 + 50));
        c += ",framerate=[" + std::to_string(std::max(1, a)) + "/100," + std::to_string(b) + "/100]";
    }
    if (m.formato == "mjpeg") c += " ! jpegparse ! " + m.decodificador;
    else if (m.formato == "h264") c += " ! h264parse ! " + m.decodificador;
    else if (m.formato == "h265") c += " ! h265parse ! " + m.decodificador;
    return c;
}

} // namespace ba::motor
