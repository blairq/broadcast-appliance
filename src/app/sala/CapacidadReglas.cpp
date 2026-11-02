// src/app/sala/CapacidadReglas.cpp — ¿cuántos invitados aguanta la máquina? (ver el .hpp)

#include "sala/CapacidadReglas.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ba::app::sala::capacidad {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {
std::string entero(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }

/// El percentil `p` (0..1) de los valores conocidos (≥ 0); -1 si no hay.
double percentil(const std::vector<Muestra>& ms, double Muestra::*campo, double p) {
    std::vector<double> v;
    for (const Muestra& m : ms)
        if (m.*campo >= 0) v.push_back(m.*campo);
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const auto i = static_cast<std::size_t>(std::lround(p * static_cast<double>(v.size() - 1)));
    return v[std::min(i, v.size() - 1)];
}
double minimo(const std::vector<Muestra>& ms, double Muestra::*campo) {
    double r = -1;
    for (const Muestra& m : ms)
        if (m.*campo >= 0) r = r < 0 ? m.*campo : std::min(r, m.*campo);
    return r;
}
double maximo(const std::vector<Muestra>& ms, double Muestra::*campo) {
    double r = -1;
    for (const Muestra& m : ms) r = std::max(r, m.*campo);
    return r;
}
/// Redondea para abajo a un múltiplo de 5 (de 10 para arriba); al menos 1 si había algo.
int redondear(double n) {
    if (n <= 0) return 0;
    if (n < 10) return std::max(1, static_cast<int>(std::floor(n)));
    return static_cast<int>(std::floor(n / 5.0)) * 5;
}
double r1(double v) { return v < 0 ? v : std::round(v * 10) / 10; }
} // namespace

// --- Los escalones ---------------------------------------------------------------------------

std::vector<int> escalones(int tope) {
    std::vector<int> out;
    if (tope < 1) return out;
    tope = std::min(tope, ba::ipc::kMaxInvitados);
    static const int kBase[] = {10, 20, 40, 60, 80, 100, 120, 160, 200, 240, 320, 400, 480};
    for (int n : kBase) {
        if (n >= tope) break;
        out.push_back(n);
    }
    out.push_back(tope);
    return out;
}

int entradaCadaMs(int nuevos, const Tiempos& t) {
    if (nuevos <= 0) return t.entradaMaxMs;
    const int cada = t.entradaS() * 1000 / nuevos;
    return std::clamp(cada, t.entradaMinMs, t.entradaMaxMs);
}

int duracionEscalonS(int nuevos, const Tiempos& t) {
    const int entrar = static_cast<int>(std::ceil(std::max(0, nuevos) * entradaCadaMs(nuevos, t) / 1000.0));
    return std::min(t.maxEscalonS, entrar + t.asentarS + t.regimenS);
}

// --- Las muestras ----------------------------------------------------------------------------

json aJson(const Muestra& m) {
    return json{{"t", m.t},
                {"cpuMaquina", r1(m.cpuMaquina)},
                {"cpuGenerador", r1(m.cpuGenerador)},
                {"cpuEquipo", r1(m.cpuEquipo)},
                {"memLibreMb", std::lround(m.memLibreMb)},
                {"memTotalMb", std::lround(m.memTotalMb)},
                {"cpuApp", r1(m.cpuApp)},
                {"cpuGateway", r1(m.cpuGateway)},
                {"cps", r1(m.cps)},
                {"audioMs", r1(m.audioMs)},
                {"uiMs", std::lround(m.uiMs)},
                {"entraron", m.entraron},
                {"conVideo", m.conVideo},
                {"kbpsSubida", std::lround(m.kbpsSubida)},
                {"kbpsBajada", std::lround(m.kbpsBajada)}};
}

Muestra muestraDe(const json& j) {
    Muestra m;
    if (!j.is_object()) return m;
    const auto d = [&j](const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_number() ? it->get<double>() : -1.0;
    };
    const auto e = [&j](const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_number() ? it->get<int>() : -1;
    };
    m.t = j.value("t", int64_t{0});
    m.cpuMaquina = d("cpuMaquina");
    m.cpuGenerador = d("cpuGenerador");
    m.cpuEquipo = d("cpuEquipo");
    m.memLibreMb = d("memLibreMb");
    m.memTotalMb = d("memTotalMb");
    m.cpuApp = d("cpuApp");
    m.cpuGateway = d("cpuGateway");
    m.cps = d("cps");
    m.audioMs = d("audioMs");
    m.uiMs = d("uiMs");
    m.entraron = e("entraron");
    m.conVideo = e("conVideo");
    m.kbpsSubida = d("kbpsSubida");
    m.kbpsBajada = d("kbpsBajada");
    return m;
}

// --- La guardia ------------------------------------------------------------------------------

std::string claveGuardia(const std::vector<Muestra>& ultimas, const Criterios& c) {
    if (ultimas.empty()) return {};
    const Muestra& u = ultimas.back();
    if (u.memLibreMb >= 0 && u.memLibreMb < c.memCorteMb) return "memoria";
    if (u.uiMs >= c.uiCorteMs) return "interfaz";
    // La máquina entera al tope, sostenido: la medida o la que genera.
    if (static_cast<int>(ultimas.size()) >= c.cpuAlTopeS) {
        bool medida = true, equipo = true;
        for (std::size_t i = ultimas.size() - static_cast<std::size_t>(c.cpuAlTopeS); i < ultimas.size(); ++i) {
            medida = medida && ultimas[i].cpuMaquina >= c.cpuAlTopePct;
            equipo = equipo && ultimas[i].cpuEquipo >= c.cpuAlTopePct;
        }
        if (medida) return "cpu";
        if (equipo) return "generador";
    }
    return {};
}

std::string guardia(const std::vector<Muestra>& ultimas, const Criterios& c) {
    const std::string k = claveGuardia(ultimas, c);
    if (k.empty()) return {};
    const Muestra& u = ultimas.back();
    if (k == "memoria")
        return llenar(traducir("capacidad", "la memoria disponible bajó a %1 MB (el corte es %2 MB)"),
                      {entero(u.memLibreMb), entero(c.memCorteMb)});
    if (k == "interfaz")
        return llenar(traducir("capacidad", "la interfaz estuvo trabada %1 ms"), {entero(u.uiMs)});
    if (k == "cpu")
        return llenar(traducir("capacidad", "la CPU estuvo al %1 % o más durante %2 s seguidos"),
                      {entero(c.cpuAlTopePct), std::to_string(c.cpuAlTopeS)});
    return llenar(traducir("capacidad", "la máquina que genera los invitados estuvo al %1 % o más durante %2 s seguidos"),
                  {entero(c.cpuAlTopePct), std::to_string(c.cpuAlTopeS)});
}

// --- El juicio de un escalón -----------------------------------------------------------------

json aJson(const Paso& p) {
    return json{{"n", p.n},
                {"desde", p.desde},
                {"hasta", p.hasta},
                {"cpu", r1(p.cpu)},
                {"cpuTotal", r1(p.cpuTotal)},
                {"cpuGenerador", r1(p.cpuGenerador)},
                {"cpuEquipo", r1(p.cpuEquipo)},
                {"memMinMb", std::lround(p.memMinMb)},
                {"memTotalMb", std::lround(p.memTotalMb)},
                {"cps", r1(p.cps)},
                {"audioMs", r1(p.audioMs)},
                {"uiMs", std::lround(p.uiMs)},
                {"uiMaxMs", std::lround(p.uiMaxMs)},
                {"entraron", p.entraron},
                {"conVideo", p.conVideo},
                {"kbpsSubida", std::lround(p.kbpsSubida)},
                {"kbpsBajada", std::lround(p.kbpsBajada)},
                {"degradado", p.degradado},
                {"guardia", p.guardia},
                {"limites", p.limites},
                {"motivos", p.motivos}};
}

Paso juzgar(int n, const std::vector<Muestra>& regimen, bool restarGenerador, const Criterios& c,
            const std::string& guardiaClave, const std::string& guardiaMotivo) {
    Paso p;
    p.n = n;
    if (!regimen.empty()) {
        p.desde = regimen.front().t;
        p.hasta = regimen.back().t;
    }
    // La CPU sin el generador (si corre acá): muestra por muestra, para la mediana.
    std::vector<Muestra> sinGen = regimen;
    for (Muestra& m : sinGen)
        if (m.cpuMaquina >= 0) m.cpuMaquina = restarGenerador && m.cpuGenerador >= 0 ? std::max(0.0, m.cpuMaquina - m.cpuGenerador) : m.cpuMaquina;
    p.cpu = percentil(sinGen, &Muestra::cpuMaquina, 0.5);
    p.cpuTotal = percentil(regimen, &Muestra::cpuMaquina, 0.5);
    p.cpuGenerador = restarGenerador ? percentil(regimen, &Muestra::cpuGenerador, 0.5) : -1;
    p.cpuEquipo = percentil(regimen, &Muestra::cpuEquipo, 0.5);
    p.memMinMb = minimo(regimen, &Muestra::memLibreMb);
    p.memTotalMb = maximo(regimen, &Muestra::memTotalMb);
    p.cps = minimo(regimen, &Muestra::cps);
    p.audioMs = percentil(regimen, &Muestra::audioMs, 0.5);
    p.uiMs = percentil(regimen, &Muestra::uiMs, 0.9);
    p.uiMaxMs = maximo(regimen, &Muestra::uiMs);
    p.kbpsSubida = percentil(regimen, &Muestra::kbpsSubida, 0.5);
    p.kbpsBajada = percentil(regimen, &Muestra::kbpsBajada, 0.5);
    if (!regimen.empty()) {
        p.entraron = regimen.back().entraron;
        p.conVideo = regimen.back().conVideo;
    }
    const auto limite = [&p](const char* clave, std::string motivo) {
        if (std::find(p.limites.begin(), p.limites.end(), clave) == p.limites.end()) p.limites.push_back(clave);
        p.motivos.push_back(std::move(motivo));
    };
    if (!guardiaClave.empty()) {
        p.guardia = true;
        limite(guardiaClave.c_str(), guardiaMotivo.empty() ? traducir("capacidad", "la cortó la guardia") : guardiaMotivo);
    }
    if (p.cpu >= 0 && p.cpu > c.cpuMaxPct)
        limite("cpu", llenar(traducir("capacidad", "CPU sostenida al %1 % (el tope es %2 %)"), {entero(p.cpu), entero(c.cpuMaxPct)}));
    if (p.memMinMb >= 0 && p.memMinMb < c.memAvisoMb)
        limite("memoria", llenar(traducir("capacidad", "quedaron %1 MB de memoria disponible (hacen falta %2 MB)"),
                                 {entero(p.memMinMb), entero(c.memAvisoMb)}));
    if (p.cps >= 0 && p.cps < c.cpsMin)
        limite("grilla", llenar(traducir("capacidad", "la grilla bajó a %1 cuadros por segundo (el mínimo es %2)"),
                                {entero(p.cps), entero(c.cpsMin)}));
    if (p.audioMs >= 0 && p.audioMs > c.audioMaxMs)
        limite("audio", llenar(traducir("capacidad", "el audio de los invitados llega con %1 ms (el tope es %2 ms)"),
                               {entero(p.audioMs), entero(c.audioMaxMs)}));
    // Los que entraron con video: sin la foto de la sala no se juzga.
    if (p.conVideo >= 0 && n > 0 && p.conVideo < static_cast<int>(std::ceil(c.entraronMin * n)))
        limite("entrada", llenar(traducir("capacidad", "entraron con video %1 de %2"), {std::to_string(p.conVideo), std::to_string(n)}));
    if ((p.uiMs >= 0 && p.uiMs > c.uiMaxMs) || (p.uiMaxMs >= c.uiTrabadaMs))
        limite("interfaz", llenar(traducir("capacidad", "la interfaz se trabó (hasta %1 ms)"), {entero(p.uiMaxMs)}));
    if (p.cpuEquipo >= 0 && p.cpuEquipo > c.cpuEquipoMaxPct)
        limite("generador", llenar(traducir("capacidad", "la máquina que genera los invitados estuvo al %1 %: el resultado es un mínimo"),
                                   {entero(p.cpuEquipo)}));
    p.degradado = !p.limites.empty();
    return p;
}

// --- El resultado ----------------------------------------------------------------------------

Resultado resumir(const std::vector<Paso>& pasos, int topePedido, bool cancelada, const Criterios& c) {
    Resultado r;
    r.cortada = cancelada;
    const Paso* ultimoSano = nullptr;
    for (const Paso& p : pasos) {
        if (p.degradado) {
            r.degradoEn = p.n;
            r.limita = p.limites.empty() ? std::string() : p.limites.front();
            break;
        }
        ultimoSano = &p;
    }
    r.maximo = ultimoSano ? ultimoSano->n : 0;
    r.alMenos = r.degradoEn == 0 && !cancelada && ultimoSano != nullptr && ultimoSano->n >= topePedido;
    if (ultimoSano) r.cpuGenerador = ultimoSano->cpuGenerador;
    // Con margen: lo probado si anduvo holgado; si no, una parte del máximo.
    if (r.maximo > 0) {
        const bool holgado = r.degradoEn == 0 && ultimoSano->cpu >= 0 && ultimoSano->cpu <= c.cpuConMargenPct &&
                             (ultimoSano->memMinMb < 0 || ultimoSano->memMinMb >= c.memAvisoMb);
        r.conMargen = holgado ? r.maximo : std::min(r.maximo, redondear(r.maximo * c.margen));
    }
    // La tendencia de la CPU (sin el generador) en los escalones sanos: una recta por
    // cuadrados mínimos y dónde cruza el tope. Hace falta al menos dos con la CPU medida.
    std::vector<std::pair<double, double>> xy;
    for (const Paso& p : pasos) {
        if (p.degradado) break;
        if (p.cpu >= 0) xy.emplace_back(p.n, p.cpu);
    }
    if (xy.size() >= 2) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto& [x, y] : xy) {
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        const double k = static_cast<double>(xy.size());
        const double den = k * sxx - sx * sx;
        if (den > 0) {
            const double b = (k * sxy - sx * sy) / den;
            const double a = (sy - b * sx) / k;
            if (b > 0.01) {
                const double n = (c.cpuMaxPct - a) / b;
                if (n > r.maximo) r.estimadoCpu = std::min(ba::ipc::kMaxInvitados, redondear(n));
            }
        }
    }
    return r;
}

json aJson(const Resultado& r) {
    return json{{"maximo", r.maximo},
                {"conMargen", r.conMargen},
                {"alMenos", r.alMenos},
                {"degradoEn", r.degradoEn},
                {"limita", r.limita},
                {"limitaTexto", r.limita.empty() ? std::string() : nombreLimite(r.limita)},
                {"estimadoCpu", r.estimadoCpu},
                {"cpuGenerador", r1(r.cpuGenerador)},
                {"cortada", r.cortada},
                {"frase", frase(r)}};
}

std::string nombreLimite(const std::string& k) {
    if (k == "cpu") return traducir("capacidad", "la CPU");
    if (k == "memoria") return traducir("capacidad", "la memoria");
    if (k == "grilla") return traducir("capacidad", "la grilla (cuadros por segundo)");
    if (k == "audio") return traducir("capacidad", "la demora del audio");
    if (k == "entrada") return traducir("capacidad", "la entrada de los invitados");
    if (k == "interfaz") return traducir("capacidad", "la interfaz (se traba)");
    if (k == "generador") return traducir("capacidad", "la máquina que genera los invitados");
    return k;
}

std::string frase(const Resultado& r) {
    std::string s;
    if (r.maximo <= 0) {
        s = r.degradoEn > 0 ? llenar(traducir("capacidad", "Ya con %1 invitados se degrada."), {std::to_string(r.degradoEn)})
                            : traducir("capacidad", "La prueba no llegó a completar ningún escalón.");
    } else if (r.alMenos) {
        s = llenar(traducir("capacidad", "Aguanta al menos %1 invitados con margen (la prueba llegó hasta %2 sin degradarse)."),
                   {std::to_string(r.conMargen), std::to_string(r.maximo)});
    } else if (r.degradoEn > 0) {
        s = llenar(traducir("capacidad", "Aguanta %1 invitados con margen (%2 como máximo antes de degradarse; con %3 se degradó)."),
                   {std::to_string(r.conMargen), std::to_string(r.maximo), std::to_string(r.degradoEn)});
    } else {
        s = llenar(traducir("capacidad", "Aguanta al menos %1 invitados con margen (%2 probados; la prueba se cortó antes)."),
                   {std::to_string(r.conMargen), std::to_string(r.maximo)});
    }
    if (!r.limita.empty()) s += " " + llenar(traducir("capacidad", "Lo que limita es %1."), {nombreLimite(r.limita)});
    if (r.estimadoCpu > 0 && r.degradoEn == 0)
        s += " " + llenar(traducir("capacidad", "Por la tendencia de la CPU llegaría a unos %1."), {std::to_string(r.estimadoCpu)});
    if (r.cpuGenerador > 0)
        s += " " + llenar(traducir("capacidad", "El generador de prueba corrió en esta máquina y usó ~%1 % de la CPU: ya está descontado."),
                          {entero(r.cpuGenerador)});
    return s;
}

// --- Internet --------------------------------------------------------------------------------

sala::enlace::Medicion idaYVuelta(double kbps, double rttReposoMs, double rttCargaMs, double perdida) {
    sala::enlace::Medicion m;
    m.subidaKbps = kbps;
    m.bajadaKbps = kbps;
    m.perdidaSubida = m.perdidaBajada = std::max(0.0, perdida);
    m.rttReposoMs = rttReposoMs;
    m.rttCargaMs = rttCargaMs;
    m.porTunel = true; // por pinggy: sin los encabezados de WireGuard
    return m;
}

// --- El código del que recibe ----------------------------------------------------------------

namespace {
constexpr const char* kPrefijoCodigo = "BACAP1.";
constexpr const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
    }
    return out;
}

bool desdeBase64url(const std::string& in, std::string& out) {
    out.clear();
    unsigned acum = 0;
    int bits = 0;
    for (char ch : in) {
        const char* p = std::strchr(kB64, ch);
        if (ch == '=') break;
        if (p == nullptr || ch == '\0') return false;
        acum = (acum << 6) | static_cast<unsigned>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acum >> bits) & 0xff);
        }
    }
    return true;
}
} // namespace

std::string armarCodigo(const Codigo& c) {
    const json j{{"u", c.url}, {"c", c.credencial}, {"h", c.huella}, {"m", c.maquina}};
    return std::string(kPrefijoCodigo) + base64url(j.dump(-1, ' ', false, json::error_handler_t::replace));
}

bool leerCodigo(const std::string& texto, Codigo& c) {
    // Lo que se pegue puede venir con espacios o saltos de línea.
    std::string t;
    for (char ch : texto)
        if (!std::isspace(static_cast<unsigned char>(ch))) t.push_back(ch);
    const std::string pre = kPrefijoCodigo;
    if (t.size() <= pre.size() || t.compare(0, pre.size(), pre) != 0) return false;
    std::string crudo;
    if (!desdeBase64url(t.substr(pre.size()), crudo)) return false;
    const json j = json::parse(crudo, nullptr, false);
    if (!j.is_object()) return false;
    Codigo x;
    x.url = j.value("u", std::string());
    x.credencial = j.value("c", std::string());
    x.huella = j.value("h", std::string());
    x.maquina = j.value("m", std::string());
    if (x.credencial.size() < 16) return false;
    if (x.url.rfind("https://", 0) != 0 && x.url.rfind("http://", 0) != 0) return false;
    c = std::move(x);
    return true;
}

// --- El historial ----------------------------------------------------------------------------

std::string limpiarNombre(const std::string& s) {
    std::string out;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') out.push_back(static_cast<char>(ch));
        else if (ch >= 0x80) out.push_back(static_cast<char>(ch)); // UTF-8: se deja
        else if (!out.empty() && out.back() != '-') out.push_back('-');
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "maquina";
    if (out.size() > 60) out.resize(60);
    return out;
}

std::string nombreArchivo(int64_t inicioMs, const std::string& maquina, const std::string& tipo) {
    const std::time_t t = static_cast<std::time_t>(inicioMs / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char fecha[32];
    std::strftime(fecha, sizeof fecha, "%Y%m%d-%H%M%S", &tm);
    return std::string(fecha) + "-" + limpiarNombre(maquina) + "-" + limpiarNombre(tipo) + ".json";
}

} // namespace ba::app::sala::capacidad
