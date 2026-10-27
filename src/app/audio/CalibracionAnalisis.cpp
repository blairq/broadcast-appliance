// src/app/audio/CalibracionAnalisis.cpp — ver el .hpp
#include "audio/CalibracionAnalisis.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "i18n/I18n.hpp"

// Las tablas y los valores se marcan para lupdate y se traducen al mostrarlos.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(contexto, texto) texto
#endif

namespace ba::app::audio::calib {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kC = 343.0; // velocidad del sonido (m/s, ~20 °C)
constexpr size_t kN = 240;

double aPot(double db) { return std::pow(10.0, db / 10.0); }
double aDb(double pot) { return pot > 1e-30 ? 10.0 * std::log10(pot) : -300.0; }

// Curvas publicadas de los modelos más comunes, relativas a 1 kHz, leídas de las
// hojas de datos del fabricante (aproximadas: la dispersión entre unidades es de
// ±2-3 dB, y en un cardioide depende del ángulo y la distancia).
struct Modelo {
    const char* id;
    const char* nombre;
    const char* nota;
    std::vector<std::pair<double, double>> curva;
    double detalleHasta, tendenciaHasta, desde;
};
const std::vector<Modelo>& modelos() {
    static const std::vector<Modelo> m = {
        {"plano", QT_TRANSLATE_NOOP("calibracion", "Micrófono de medición (sin archivo)"),
         QT_TRANSLATE_NOOP("calibracion", "Omnidireccional de medición sin su archivo: se lo toma plano (±1 dB típico hasta 10 kHz)."),
         {{20, 0}, {20000, 0}}, 10000, 16000, 20},
        {"sm58", "Shure SM58",
         QT_TRANSLATE_NOOP("calibracion", "Cardioide dinámico. Hoja de Shure: cae bajo 100 Hz, realce de presencia +4/+6 dB en 4-6 kHz. "
                           "Apuntarlo al parlante, a más de 1 m (sin efecto de proximidad)."),
         {{50, -10}, {80, -6}, {100, -4}, {150, -2}, {200, -1}, {500, 0}, {1000, 0}, {2000, 1}, {3000, 3},
          {4000, 5}, {5000, 5}, {6000, 4}, {8000, 1}, {10000, -2}, {12000, -6}, {15000, -10}},
         2000, 10000, 40},
        {"sm57", "Shure SM57",
         QT_TRANSLATE_NOOP("calibracion", "Cardioide dinámico. Hoja de Shure: cae bajo 100 Hz, presencia +5/+6 dB en 5-7 kHz. Apuntarlo al "
                           "parlante, a más de 1 m."),
         {{40, -10}, {80, -5}, {100, -4}, {150, -2}, {200, -1}, {500, 0}, {1000, 0}, {2000, 1}, {4000, 4},
          {6000, 6}, {7000, 5}, {8000, 3}, {10000, 0}, {12000, -4}, {15000, -8}},
         2000, 10000, 40},
        {"beta58", "Shure Beta 58A",
         QT_TRANSLATE_NOOP("calibracion", "Supercardioide dinámico. Presencia +4 dB en 4-6 kHz; cae bajo 100 Hz."),
         {{50, -9}, {100, -3}, {200, -1}, {1000, 0}, {3000, 2}, {5000, 4}, {8000, 2}, {10000, -1},
          {15000, -8}},
         2000, 10000, 40},
        {"e835", "Sennheiser e835",
         QT_TRANSLATE_NOOP("calibracion", "Cardioide dinámico. Presencia +3/+4 dB en 3-6 kHz; cae bajo 100 Hz."),
         {{50, -9}, {100, -3}, {200, -1}, {1000, 0}, {3000, 3}, {5000, 4}, {8000, 2}, {12000, -4},
          {16000, -10}},
         2000, 10000, 40},
        {"desconocido", QT_TRANSLATE_NOOP("calibracion", "Otro (sin curva)"),
         QT_TRANSLATE_NOOP("calibracion", "Sin datos del micrófono: solo se corrigen los graves (modos: lo que importa es el pico contra sus "
                           "vecinas) y tendencias muy anchas."),
         {{20, 0}, {20000, 0}}, 1000, 4000, 40},
    };
    return m;
}
const Modelo* modeloDe(const std::string& id) {
    for (const auto& m : modelos())
        if (id == m.id) return &m;
    return nullptr;
}

const json& lista(const json& j, const char* clave) {
    static const json vacia = json::array();
    const auto it = j.find(clave);
    return it != j.end() && it->is_array() ? *it : vacia;
}

const json* buscar(const json& lista, const std::string& id) {
    if (!lista.is_array()) return nullptr;
    for (const auto& x : lista)
        if (x.value("id", "") == id) return &x;
    return nullptr;
}

Curva curvaDe(const json& med) {
    Curva c(kN, -300.0);
    const json& j = med.contains("curva") ? med["curva"] : json::array();
    for (size_t i = 0; i < kN && i < j.size(); ++i) c[i] = j[i].get<double>();
    return c;
}

json aJson(const Curva& c, int decimales = 2) {
    json a = json::array();
    const double k = std::pow(10.0, decimales);
    for (double v : c) a.push_back(std::round(v * k) / k);
    return a;
}

double promedioEntre(const Curva& c, double f0, double f1) {
    double p = 0;
    int n = 0;
    for (size_t i = 0; i < kN; ++i)
        if (grilla()[i] >= f0 && grilla()[i] <= f1) {
            p += aPot(c[i]);
            ++n;
        }
    return n > 0 ? aDb(p / n) : -300.0;
}

// La medición más nueva de cada (parlante, punto) en una etapa.
std::vector<const json*> ultimas(const json& sesion, const std::string& parlante, const std::string& etapa,
                                 const std::set<std::string>& tiposPunto) {
    std::map<std::string, const json*> porPunto;
    for (const auto& m : lista(sesion, "mediciones")) {
        if (m.value("parlante", "") != parlante || m.value("etapa", "antes") != etapa) continue;
        const json* p = buscar(lista(sesion, "puntos"), m.value("punto", ""));
        if (p == nullptr || !tiposPunto.count(p->value("tipo", "escucha"))) continue;
        porPunto[m.value("punto", "")] = &m; // en orden: la última gana
    }
    std::vector<const json*> r;
    for (auto& [k, v] : porPunto) r.push_back(v);
    return r;
}

std::string fmt(double v, int dec = 0) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(dec);
    o << v;
    return o.str();
}

} // namespace

const std::vector<double>& grilla() {
    static const std::vector<double> g = [] {
        std::vector<double> v(kN);
        for (size_t i = 0; i < kN; ++i) v[i] = 20.0 * std::pow(1000.0, static_cast<double>(i) / (kN - 1));
        return v;
    }();
    return g;
}

size_t indiceDe(double f) {
    if (f <= 20) return 0;
    const double i = std::log(f / 20.0) / std::log(1000.0) * (kN - 1);
    return std::min(kN - 1, static_cast<size_t>(std::lround(i)));
}

double interpolar(const std::vector<std::pair<double, double>>& t, double f) {
    if (t.empty()) return 0;
    if (f <= t.front().first) return t.front().second;
    if (f >= t.back().first) return t.back().second;
    for (size_t i = 1; i < t.size(); ++i)
        if (f <= t[i].first) {
            const double a = std::log(t[i - 1].first), b = std::log(t[i].first);
            const double u = b > a ? (std::log(f) - a) / (b - a) : 0;
            return t[i - 1].second + u * (t[i].second - t[i - 1].second);
        }
    return t.back().second;
}

std::vector<std::pair<double, double>> leerArchivoCalibracion(const std::string& texto) {
    std::vector<std::pair<double, double>> r;
    std::istringstream in(texto);
    std::string linea;
    while (std::getline(in, linea)) {
        for (char& ch : linea)
            if (ch == ',' || ch == ';' || ch == '\t') ch = ' ';
        std::istringstream l(linea);
        double f = 0, db = 0;
        if (!(l >> f >> db)) continue; // encabezados («Sens Factor…», "…", *…)
        if (f <= 0 || f > 100000 || !std::isfinite(db)) continue;
        if (!r.empty() && f <= r.back().first) continue;
        r.emplace_back(f, db);
    }
    if (r.size() < 3) r.clear();
    return r;
}

json modelosMicrofono() {
    json a = json::array();
    for (const auto& m : modelos()) a.push_back({{"id", m.id}, {"nombre", ba::i18n::traducir("calibracion", m.nombre)}, {"nota", ba::i18n::traducir("calibracion", m.nota)}});
    return a;
}

Curva curvaMicrofono(const json& mic) {
    const json cal = mic.value("calibracion", json::object());
    const std::string tipo = cal.value("tipo", "modelo");
    std::vector<std::pair<double, double>> t;
    if (tipo == "archivo" || tipo == "sustitucion") {
        for (const auto& p : cal.value("puntos", json::array()))
            if (p.is_array() && p.size() >= 2) t.emplace_back(p[0].get<double>(), p[1].get<double>());
    } else if (const Modelo* m = modeloDe(tipo == "plano" ? "plano" : cal.value("modelo", "desconocido"))) {
        t = m->curva;
    }
    Curva c(kN, 0.0);
    if (t.empty()) return c;
    // Relativa a 1 kHz: lo que importa es la forma, no la sensibilidad.
    const double ref = interpolar(t, 1000.0);
    for (size_t i = 0; i < kN; ++i) c[i] = interpolar(t, grilla()[i]) - ref;
    return c;
}

json confianzaMicrofono(const json& mic) {
    const json cal = mic.value("calibracion", json::object());
    const std::string tipo = cal.value("tipo", "modelo");
    if (tipo == "archivo") return {{"desde", 20}, {"detalle_hasta", 16000}, {"tendencia_hasta", 20000}};
    // Por sustitución: tan bueno como la referencia, pero solo en la orientación medida.
    if (tipo == "sustitucion") return {{"desde", 40}, {"detalle_hasta", 8000}, {"tendencia_hasta", 14000}};
    const Modelo* m = modeloDe(tipo == "plano" ? "plano" : cal.value("modelo", "desconocido"));
    if (m == nullptr) m = modeloDe("desconocido");
    return {{"desde", m->desde}, {"detalle_hasta", m->detalleHasta}, {"tendencia_hasta", m->tendenciaHasta}};
}

Curva suavizar(const Curva& c, double n) {
    if (n <= 0) return c;
    // La grilla es de 1/24: 1/n de octava = ±12/n puntos.
    const int medio = std::max(0, static_cast<int>(std::lround(12.0 / n)));
    Curva r(c.size());
    for (int i = 0; i < static_cast<int>(c.size()); ++i) {
        double p = 0;
        int k = 0;
        for (int j = std::max(0, i - medio); j <= std::min(static_cast<int>(c.size()) - 1, i + medio); ++j) {
            p += aPot(c[j]);
            ++k;
        }
        r[i] = aDb(p / k);
    }
    return r;
}

Curva promedioPotencia(const std::vector<Curva>& cs) {
    Curva r(kN, -300.0);
    if (cs.empty()) return r;
    for (size_t i = 0; i < kN; ++i) {
        double p = 0;
        for (const auto& c : cs) p += aPot(c[i]);
        r[i] = aDb(p / static_cast<double>(cs.size()));
    }
    return r;
}

double frecuenciaSchroeder(double rt60, double volumen) {
    if (rt60 <= 0 || volumen <= 0) return 0;
    return 2000.0 * std::sqrt(rt60 / volumen);
}

json modosTeoricos(double L, double W, double H, double fmax) {
    json a = json::array();
    if (L <= 0 || W <= 0 || H <= 0) return a;
    const std::string eje[3] = {ba::i18n::traducir("calibracion", "largo"), ba::i18n::traducir("calibracion", "ancho"), ba::i18n::traducir("calibracion", "alto")};
    const double dim[3] = {L, W, H};
    for (int x = 0; x < 12; ++x)
        for (int y = 0; y < 12; ++y)
            for (int z = 0; z < 12; ++z) {
                if (x + y + z == 0) continue;
                const double f = kC / 2.0 * std::sqrt(std::pow(x / L, 2) + std::pow(y / W, 2) + std::pow(z / H, 2));
                if (f > fmax) continue;
                const int n[3] = {x, y, z};
                const int ceros = (x == 0) + (y == 0) + (z == 0);
                // El tipo es un valor (se compara); se traduce al mostrarlo (el motivo de un filtro).
                std::string tipo = ceros == 2   ? QT_TRANSLATE_NOOP("calibracion", "axial")
                                   : ceros == 1 ? QT_TRANSLATE_NOOP("calibracion", "tangencial")
                                                : QT_TRANSLATE_NOOP("calibracion", "oblicuo");
                std::string de;
                for (int k = 0; k < 3; ++k)
                    if (n[k] > 0) de += (de.empty() ? "" : " + ") + eje[k] + " (" + fmt(dim[k], 2) + " m)";
                a.push_back({{"f", std::round(f * 10) / 10}, {"n", {x, y, z}}, {"tipo", tipo}, {"de", de}});
            }
    std::sort(a.begin(), a.end(), [](const json& p, const json& q) { return p["f"].get<double>() < q["f"].get<double>(); });
    return a;
}

Curva respuestaFiltro(const json& f, double sr) {
    // RBJ (Audio EQ Cookbook): pico y estantes.
    const std::string tipo = f.value("tipo", "pico");
    const double f0 = f.value("f", 1000.0), g = f.value("ganancia_db", 0.0), q = std::max(0.1, f.value("q", 1.0));
    const double A = std::pow(10.0, g / 40.0), w0 = 2 * kPi * f0 / sr, cw = std::cos(w0), al = std::sin(w0) / (2 * q);
    double b0, b1, b2, a0, a1, a2;
    if (tipo == "estante_graves" || tipo == "estante_agudos") {
        const double s = 2 * std::sqrt(A) * al;
        const double sg = tipo == "estante_graves" ? -1.0 : 1.0;
        b0 = A * ((A + 1) + sg * (A - 1) * cw + s);
        b1 = -2 * sg * A * ((A - 1) + sg * (A + 1) * cw);
        b2 = A * ((A + 1) + sg * (A - 1) * cw - s);
        a0 = (A + 1) - sg * (A - 1) * cw + s;
        a1 = 2 * sg * ((A - 1) - sg * (A + 1) * cw);
        a2 = (A + 1) - sg * (A - 1) * cw - s;
    } else {
        b0 = 1 + al * A;
        b1 = -2 * cw;
        b2 = 1 - al * A;
        a0 = 1 + al / A;
        a1 = -2 * cw;
        a2 = 1 - al / A;
    }
    Curva r(kN);
    for (size_t i = 0; i < kN; ++i) {
        const double w = 2 * kPi * grilla()[i] / sr;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        r[i] = 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return r;
}

Curva curvaObjetivo(double ref, double pendiente) {
    Curva r(kN);
    for (size_t i = 0; i < kN; ++i) r[i] = ref + pendiente * std::log2(grilla()[i] / 1000.0);
    return r;
}

json analizar(const json& sesion) {
    json res;
    const json sala = sesion.value("sala", json::object());
    const double L = sala.value("largo", 0.0), W = sala.value("ancho", 0.0), H = sala.value("alto", 0.0);
    const double V = L * W * H;
    res["volumen_m3"] = std::round(V * 10) / 10;

    // RT por banda: el promedio de las mediciones en la zona de escucha (cualquier parlante).
    const std::vector<double> centros = {63, 125, 250, 500, 1000, 2000, 4000, 8000};
    std::vector<double> sum(centros.size(), 0), cuenta(centros.size(), 0);
    std::vector<double> sumEdt(centros.size(), 0), cuentaEdt(centros.size(), 0);
    for (const auto& m : lista(sesion, "mediciones")) {
        if (m.value("etapa", "antes") != "antes") continue;
        const json* p = buscar(lista(sesion, "puntos"), m.value("punto", ""));
        if (p == nullptr || p->value("tipo", "escucha") != "escucha") continue;
        const json rb = m.value("rt_bandas", json::object());
        const json cs = rb.value("centros", json::array()), t = rb.value("t20", json::array()),
                   e = rb.value("edt", json::array());
        for (size_t k = 0; k < cs.size() && k < t.size(); ++k)
            for (size_t b = 0; b < centros.size(); ++b)
                if (std::abs(cs[k].get<double>() - centros[b]) < 1) {
                    if (t[k].get<double>() > 0) {
                        sum[b] += t[k].get<double>();
                        cuenta[b] += 1;
                    }
                    if (k < e.size() && e[k].get<double>() > 0) {
                        sumEdt[b] += e[k].get<double>();
                        cuentaEdt[b] += 1;
                    }
                }
    }
    json rt = json::array();
    double rtMedio = 0;
    int nMedio = 0;
    for (size_t b = 0; b < centros.size(); ++b) {
        const double t20 = cuenta[b] > 0 ? sum[b] / cuenta[b] : -1;
        const double edt = cuentaEdt[b] > 0 ? sumEdt[b] / cuentaEdt[b] : -1;
        rt.push_back({{"hz", centros[b]}, {"t20_s", std::round(t20 * 1000) / 1000}, {"edt_s", std::round(edt * 1000) / 1000}});
        if ((centros[b] == 500 || centros[b] == 1000) && t20 > 0) {
            rtMedio += t20;
            ++nMedio;
        }
    }
    res["rt_bandas"] = rt;
    rtMedio = nMedio > 0 ? rtMedio / nMedio : 0;
    res["rt60_medio_s"] = std::round(rtMedio * 1000) / 1000;
    // Sin RT medido todavía, 0,5 s (una sala tratada a medias) solo para tener un fs.
    const double fs = frecuenciaSchroeder(rtMedio > 0 ? rtMedio : 0.5, V);
    res["schroeder_hz"] = std::round(fs);
    res["schroeder_nota"] = rtMedio > 0 ? ba::i18n::traducir("calibracion", "con el RT60 medido (promedio de 500 Hz y 1 kHz)")
                                        : ba::i18n::traducir("calibracion", "estimada con RT60 = 0,5 s (falta medir en la zona de escucha)");
    const double limiteModos = fs > 0 ? std::clamp(fs, 120.0, 400.0) : 250.0;
    const json teoricos = modosTeoricos(L, W, H, limiteModos * 1.1);
    res["modos_teoricos"] = teoricos;

    json porParlante = json::object();
    for (const auto& pa : lista(sesion, "parlantes")) {
        const std::string pid = pa.value("id", "");
        json r;
        const auto meds = ultimas(sesion, pid, "antes", {"escucha"});
        r["puntos"] = static_cast<int>(meds.size());
        if (meds.empty()) {
            r["falta"] = ba::i18n::traducir("calibracion", "medir este parlante en la zona de escucha (puntos de tipo escucha)");
            porParlante[pid] = r;
            continue;
        }
        // Cada punto corregido por su micrófono; la confianza es la del peor.
        std::vector<Curva> curvas;
        double detalleHasta = 20000, tendenciaHasta = 20000, desde = 20;
        double llegada = 0, nivel = 0, snrMin = 1e9;
        int polaridadNeg = 0;
        json porPunto = json::array();
        for (const json* m : meds) {
            Curva c = curvaDe(*m);
            const json* mic = buscar(lista(sesion, "microfonos"), m->value("microfono", ""));
            if (mic != nullptr) {
                const Curva cm = curvaMicrofono(*mic);
                for (size_t i = 0; i < kN; ++i) c[i] -= cm[i];
                const json cf = confianzaMicrofono(*mic);
                detalleHasta = std::min(detalleHasta, cf["detalle_hasta"].get<double>());
                tendenciaHasta = std::min(tendenciaHasta, cf["tendencia_hasta"].get<double>());
                desde = std::max(desde, cf["desde"].get<double>());
            }
            curvas.push_back(suavizar(c, 24));
            llegada += m->value("directo_ms", 0.0);
            nivel += promedioEntre(c, 500, 4000);
            snrMin = std::min(snrMin, m->value("snr_db", 0.0));
            if (m->value("polaridad", 1) < 0) ++polaridadNeg;
            porPunto.push_back({{"punto", m->value("punto", "")}, {"medicion", m->value("id", "")}});
        }
        const double n = static_cast<double>(meds.size());
        r["llegada_ms"] = std::round(llegada / n * 100) / 100;
        r["nivel_db"] = std::round(nivel / n * 10) / 10;
        r["snr_min_db"] = std::round(snrMin);
        r["polaridad"] = polaridadNeg * 2 > static_cast<int>(meds.size()) ? -1 : 1;
        r["confianza"] = {{"desde", desde}, {"detalle_hasta", detalleHasta}, {"tendencia_hasta", tendenciaHasta}};
        r["mediciones"] = porPunto;

        // Promedio espacial (de potencia) y su versión de lectura: 1/24 bajo fs, 1/6 arriba.
        const Curva prom = promedioPotencia(curvas);
        const Curva p6 = suavizar(prom, 6);
        Curva lectura(kN);
        for (size_t i = 0; i < kN; ++i) lectura[i] = grilla()[i] < fs ? prom[i] : p6[i];
        r["promedio"] = aJson(lectura);

        // Modos: picos de 3 dB o más sobre la tendencia (1 octava) bajo el límite, que
        // aparecen en la mayoría de los puntos (no en uno solo: eso es el lugar).
        const Curva base = suavizar(prom, 1);
        std::vector<std::vector<double>> picosPorPunto;
        for (const auto& c : curvas) {
            const Curva b = suavizar(c, 1);
            std::vector<double> ps;
            for (size_t i = 1; i + 1 < kN && grilla()[i] < limiteModos; ++i) {
                if (grilla()[i] < desde) continue;
                const double e = c[i] - b[i];
                if (e >= 3 && c[i] >= c[i - 1] && c[i] >= c[i + 1]) ps.push_back(grilla()[i]);
            }
            picosPorPunto.push_back(ps);
        }
        json modos = json::array(), pozos = json::array();
        const int necesarios = meds.size() <= 2 ? 1 : static_cast<int>(std::ceil(0.6 * n));
        for (size_t i = 1; i + 1 < kN && grilla()[i] < limiteModos; ++i) {
            if (grilla()[i] < desde) continue;
            const double e = prom[i] - base[i];
            const bool max = prom[i] >= prom[i - 1] && prom[i] >= prom[i + 1];
            const bool min = prom[i] <= prom[i - 1] && prom[i] <= prom[i + 1];
            const double f = grilla()[i];
            if (max && e >= 3) {
                int en = 0;
                for (const auto& ps : picosPorPunto)
                    for (double fp : ps)
                        if (std::abs(std::log2(fp / f)) <= 1.0 / 12) {
                            ++en;
                            break;
                        }
                // El ancho: donde el exceso baja a la mitad (en dB) a cada lado.
                size_t a = i, b = i;
                while (a > 0 && prom[a] - base[a] > e / 2) --a;
                while (b + 1 < kN && prom[b] - base[b] > e / 2) ++b;
                const double bw = grilla()[b] - grilla()[a];
                const double q = std::clamp(bw > 0 ? f / bw : 10.0, 1.0, 12.0);
                json cerca = json::array();
                for (const auto& t : teoricos)
                    if (std::abs(std::log2(t["f"].get<double>() / f)) <= 1.0 / 12) cerca.push_back(t);
                modos.push_back({{"f", std::round(f * 10) / 10},
                                 {"exceso_db", std::round(e * 10) / 10},
                                 {"q", std::round(q * 10) / 10},
                                 {"en_puntos", en},
                                 {"de_puntos", static_cast<int>(meds.size())},
                                 {"confirmado", en >= necesarios},
                                 {"teoricos", cerca}});
            } else if (min && e <= -6) {
                pozos.push_back({{"f", std::round(f * 10) / 10}, {"profundidad_db", std::round(e * 10) / 10}});
            }
        }
        r["modos"] = modos;
        r["pozos"] = pozos;
        porParlante[pid] = r;
    }
    res["parlantes"] = porParlante;
    res["grilla_puntos"] = static_cast<int>(kN);
    return res;
}

json proponer(const json& sesion, const json& analisis) {
    json res;
    const double fs = analisis.value("schroeder_hz", 200.0);
    const double pendiente = sesion.value("objetivo", json::object()).value("pendiente_db_oct", -0.8);
    json porParlante = json::object();
    double llegadaMax = -1e9, nivelMin = 1e9;
    const json parlantes = analisis.value("parlantes", json::object());
    for (auto& [pid, a] : parlantes.items()) {
        if (!a.contains("promedio")) continue;
        llegadaMax = std::max(llegadaMax, a.value("llegada_ms", 0.0));
        nivelMin = std::min(nivelMin, a.value("nivel_db", 0.0));
    }
    for (auto& [pid, a] : parlantes.items()) {
        json r;
        if (!a.contains("promedio")) {
            r["falta"] = a.value("falta", ba::i18n::traducir("calibracion", "sin mediciones"));
            porParlante[pid] = r;
            continue;
        }
        Curva prom(kN);
        for (size_t i = 0; i < kN; ++i) prom[i] = a["promedio"][i].get<double>();
        const json cf = a["confianza"];
        const double detalle = cf.value("detalle_hasta", 2000.0), tendencia = cf.value("tendencia_hasta", 8000.0),
                     desde = cf.value("desde", 60.0);
        // El objetivo, al nivel del promedio entre 300 Hz y 3 kHz.
        const Curva p3 = suavizar(prom, 3);
        double s = 0;
        int k = 0;
        for (size_t i = 0; i < kN; ++i)
            if (grilla()[i] >= 300 && grilla()[i] <= 3000) {
                s += p3[i] - pendiente * std::log2(grilla()[i] / 1000.0);
                ++k;
            }
        const double ref = k > 0 ? s / k : 0;
        const Curva obj = curvaObjetivo(ref, pendiente);
        r["objetivo"] = aJson(obj);
        r["objetivo_nota"] = ba::i18n::llenar(ba::i18n::traducir("calibracion", "plana con %1 dB/oct (Toole: régimen permanente en sala), al nivel del promedio entre 300 Hz y 3 kHz"),
                                              {fmt(pendiente, 1)});

        json filtros = json::array(), descartados = json::array();
        // Bajo fs: un corte por modo confirmado (lo que sobra), de mayor a menor exceso.
        std::vector<json> modos;
        for (const auto& m : a.value("modos", json::array())) modos.push_back(m);
        std::sort(modos.begin(), modos.end(),
                  [](const json& p, const json& q) { return p["exceso_db"].get<double>() > q["exceso_db"].get<double>(); });
        for (const auto& m : modos) {
            const double f = m["f"].get<double>();
            std::string de;
            if (!m["teoricos"].empty())
                de = ba::i18n::llenar(ba::i18n::traducir("calibracion", "%1 del %2"), {ba::i18n::traducir("calibracion", m["teoricos"][0]["tipo"].get<std::string>().c_str()),
                                                                    m["teoricos"][0]["de"].get<std::string>()});
            const std::string donde = ba::i18n::llenar(ba::i18n::traducir("calibracion", "%1 Hz, +%2 dB en %3 de %4 puntos"),
                                                       {fmt(f, 0), fmt(m["exceso_db"].get<double>(), 1),
                                                        std::to_string(m["en_puntos"].get<int>()),
                                                        std::to_string(m["de_puntos"].get<int>())});
            if (!m.value("confirmado", false)) {
                descartados.push_back({{"f", f}, {"motivo", ba::i18n::llenar(ba::i18n::traducir("calibracion", "pico en %1: no aparece en la mayoría de los puntos (es del lugar, no de la sala)"), {donde})}});
                continue;
            }
            if (filtros.size() >= 5) {
                descartados.push_back({{"f", f}, {"motivo", ba::i18n::llenar(ba::i18n::traducir("calibracion", "modo en %1: ya hay 5 cortes (los más grandes)"), {donde})}});
                continue;
            }
            const double g = -std::min(12.0, m["exceso_db"].get<double>());
            filtros.push_back({{"tipo", "pico"},
                               {"f", f},
                               {"ganancia_db", std::round(g * 10) / 10},
                               {"q", m["q"]},
                               {"motivo", de.empty() ? ba::i18n::llenar(ba::i18n::traducir("calibracion", "modo de la sala: %1"), {donde})
                                                     : ba::i18n::llenar(ba::i18n::traducir("calibracion", "modo de la sala, %1: %2"), {de, donde})}});
        }
        // Arriba de fs: hasta 3 filtros anchos hacia el objetivo, donde el micrófono
        // da para tendencias; +3/-6 dB como mucho.
        const Curva p1 = suavizar(prom, 1);
        Curva resto(kN);
        for (size_t i = 0; i < kN; ++i) resto[i] = p1[i] - obj[i];
        for (const auto& f : filtros) {
            const Curva rf = respuestaFiltro(f);
            for (size_t i = 0; i < kN; ++i) resto[i] += rf[i];
        }
        const double techo = std::min(tendencia, 8000.0);
        for (int vez = 0; vez < 3; ++vez) {
            double peor = 0, fPeor = 0;
            for (double fc : {250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
                if (fc < std::max(fs * 1.4, desde) || fc > techo) continue;
                const double d = resto[indiceDe(fc)];
                if (std::abs(d) > std::abs(peor)) {
                    peor = d;
                    fPeor = fc;
                }
            }
            if (std::abs(peor) < 2.0 || fPeor == 0) break;
            const double g = std::clamp(-peor, -6.0, 3.0);
            json f = {{"tipo", "pico"},
                      {"f", fPeor},
                      {"ganancia_db", std::round(g * 10) / 10},
                      {"q", 0.9},
                      {"motivo", ba::i18n::llenar(peor > 0 ? ba::i18n::traducir("calibracion", "sobra %1 dB contra el objetivo alrededor de %2 Hz (promedio de la zona, 1 octava)")
                                                   : ba::i18n::traducir("calibracion", "falta %1 dB contra el objetivo alrededor de %2 Hz (promedio de la zona, 1 octava)"),
                                         {fmt(std::abs(peor), 1), fmt(fPeor, 0)}) +
                                     (fPeor > detalle ? ba::i18n::traducir("calibracion", "; el micrófono solo da para tendencias acá: filtro ancho") : "") +
                                     (g != -peor ? ba::i18n::traducir("calibracion", "; limitado a +3/-6 dB") : "")}};
            const Curva rf = respuestaFiltro(f);
            for (size_t i = 0; i < kN; ++i) resto[i] += rf[i];
            filtros.push_back(f);
        }
        for (const auto& pz : a.value("pozos", json::array()))
            descartados.push_back({{"f", pz["f"]},
                                   {"motivo", ba::i18n::llenar(ba::i18n::traducir("calibracion", "pozo de %1 dB en %2 Hz: es una cancelación; no se levanta con EQ (gasta potencia y empeora otros lugares). Probar moviendo el parlante o el punto de escucha"),
                                                              {fmt(pz["profundidad_db"].get<double>(), 1), fmt(pz["f"].get<double>(), 0)})}});
        r["filtros"] = filtros;
        r["no_se_corrige"] = descartados;
        // Lo que se espera después: el promedio con los filtros.
        Curva previsto = prom;
        for (const auto& f : filtros) {
            const Curva rf = respuestaFiltro(f);
            for (size_t i = 0; i < kN; ++i) previsto[i] += rf[i];
        }
        r["previsto"] = aJson(previsto);
        // Retardo y nivel para llegar juntos y parejos (con más de un parlante).
        const double ret = llegadaMax - a.value("llegada_ms", 0.0);
        const double gan = nivelMin - a.value("nivel_db", 0.0);
        r["retardo_ms"] = std::round(ret * 100) / 100;
        r["ganancia_db"] = std::round(gan * 10) / 10;
        if (a.value("polaridad", 1) < 0)
            r["polaridad"] = ba::i18n::traducir("calibracion", "invertida en la mayoría de los puntos: revisar el cableado o invertirla");
        porParlante[pid] = r;
    }
    res["parlantes"] = porParlante;
    return res;
}

json verificar(const json& sesion, const json& analisis) {
    json res = json::object();
    const json prop = proponer(sesion, analisis);
    for (auto& [pid, p] : prop["parlantes"].items()) {
        if (!p.contains("objetivo")) continue;
        Curva obj(kN);
        for (size_t i = 0; i < kN; ++i) obj[i] = p["objetivo"][i].get<double>();
        const json cf = analisis.value("parlantes", json::object()).value(pid, json::object()).value("confianza", json::object());
        const double hasta = std::min(cf.value("tendencia_hasta", 8000.0), 8000.0);
        const double desde = std::max(40.0, cf.value("desde", 40.0));
        const auto desvio = [&](const std::string& etapa, Curva* prom) -> double {
            const auto meds = ultimas(sesion, pid, etapa, {"escucha"});
            if (meds.empty()) return -1;
            std::vector<Curva> cs;
            for (const json* m : meds) {
                Curva c = curvaDe(*m);
                if (const json* mic = buscar(lista(sesion, "microfonos"), m->value("microfono", ""))) {
                    const Curva cm = curvaMicrofono(*mic);
                    for (size_t i = 0; i < kN; ++i) c[i] -= cm[i];
                }
                cs.push_back(c);
            }
            *prom = suavizar(promedioPotencia(cs), 6);
            // El nivel no cuenta (puede haber cambiado el volumen): se centra en 300 Hz-3 kHz.
            double off = 0;
            int k = 0;
            for (size_t i = 0; i < kN; ++i)
                if (grilla()[i] >= 300 && grilla()[i] <= 3000) {
                    off += (*prom)[i] - obj[i];
                    ++k;
                }
            off = k > 0 ? off / k : 0;
            double s = 0;
            int n = 0;
            for (size_t i = 0; i < kN; ++i)
                if (grilla()[i] >= desde && grilla()[i] <= hasta) {
                    const double d = (*prom)[i] - off - obj[i];
                    s += d * d;
                    ++n;
                }
            return n > 0 ? std::sqrt(s / n) : -1;
        };
        Curva antes, despues;
        const double da = desvio("antes", &antes), dd = desvio("verificacion", &despues);
        json r = {{"desvio_antes_db", std::round(da * 10) / 10}, {"desvio_despues_db", std::round(dd * 10) / 10}};
        if (dd >= 0) {
            r["despues"] = aJson(despues);
            // El veredicto es un valor (lo comparan la interfaz y el test): se marca aquí y se
            // traduce al mostrarlo (qsTranslate("calibracion", v.veredicto)).
            r["veredicto"] = da < 0                ? QT_TRANSLATE_NOOP("calibracion", "sin medición de antes")
                             : dd < da - 0.5       ? QT_TRANSLATE_NOOP("calibracion", "mejoró")
                             : dd > da + 0.5       ? QT_TRANSLATE_NOOP("calibracion", "empeoró: revisar")
                                                   : QT_TRANSLATE_NOOP("calibracion", "igual");
        } else {
            r["veredicto"] = QT_TRANSLATE_NOOP("calibracion", "falta medir la verificación (los mismos puntos, etapa verificacion)");
        }
        res[pid] = r;
    }
    return res;
}

} // namespace ba::app::audio::calib
