// ============================================================================
// src/gateway/Grillas.cpp — ver Grillas.hpp
// ============================================================================

#include "Grillas.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

#include "HttpIo.hpp"
#include "Log.hpp"
#include "i18n/I18n.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SpoutTransport.hpp"

#if BA_GATEWAY_HAVE_WEBRTC
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"
#include "third_party/libyuv/include/libyuv/scale_argb.h"
#endif

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Escala un rectángulo BGRA a otro, cada uno con su paso (así se escribe adentro
/// del lienzo y se lee un recorte del retrato sin copias). Con libyuv, filtro de
/// caja con SIMD (achicar promediando: una cara de 640 en 200 píxeles no titila);
/// sin libwebrtc (el backend de las pruebas), el vecino más cercano.
void escalar(const std::uint8_t* src, int sstride, int sw, int sh, std::uint8_t* dst, int dstride, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
#if BA_GATEWAY_HAVE_WEBRTC
    libyuv::ARGBScale(src, sstride, sw, sh, dst, dstride, dw, dh, libyuv::kFilterBox);
#else
    for (int y = 0; y < dh; ++y) {
        const std::uint8_t* fila = src + static_cast<std::size_t>(y * sh / dh) * sstride;
        auto* out = reinterpret_cast<std::uint32_t*>(dst + static_cast<std::size_t>(y) * dstride);
        for (int x = 0; x < dw; ++x) std::memcpy(out + x, fila + static_cast<std::size_t>(x * sw / dw) * 4, 4);
    }
#endif
}

/// Un rectángulo en enteros, recortado al lienzo.
struct Caja {
    int x = 0, y = 0, w = 0, h = 0;
};
Caja caja(const Grillas::Rect& r, int W, int H) {
    const int x0 = std::clamp(static_cast<int>(std::lround(r.x)), 0, W);
    const int y0 = std::clamp(static_cast<int>(std::lround(r.y)), 0, H);
    const int x1 = std::clamp(static_cast<int>(std::lround(r.x + r.w)), 0, W);
    const int y1 = std::clamp(static_cast<int>(std::lround(r.y + r.h)), 0, H);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void llenar(std::uint8_t* lienzo, int W, const Caja& c, std::uint32_t color) {
    for (int y = c.y; y < c.y + c.h; ++y) {
        auto* p = reinterpret_cast<std::uint32_t*>(lienzo + (static_cast<std::size_t>(y) * W + c.x) * 4);
        std::fill(p, p + c.w, color);
    }
}

/// El lugar de alguien sin imagen todavía (recién conectado, sin cámara, la asamblea
/// antes de su primera foto): un fondo y una silueta de cabeza y hombros. Mejor que un
/// negro, que en una grilla parece una falla.
void silueta(std::uint8_t* lienzo, int W, const Caja& c) {
    constexpr std::uint32_t kFondo = 0xFF3A302Bu, kFigura = 0xFF63524Au; // BGRA: gris azulado
    llenar(lienzo, W, c, kFondo);
    const float cx = c.x + c.w * 0.5f;
    const float cabezaY = c.y + c.h * 0.40f, radio = c.h * 0.17f;
    const float hombrosY = c.y + c.h * 1.0f, rx = std::min(c.w * 0.30f, c.h * 0.48f), ry = c.h * 0.33f;
    for (int y = c.y; y < c.y + c.h; ++y) {
        auto* p = reinterpret_cast<std::uint32_t*>(lienzo + static_cast<std::size_t>(y) * W * 4);
        for (int x = c.x; x < c.x + c.w; ++x) {
            const float dx = x - cx, dyc = y - cabezaY, dyh = y - hombrosY;
            const bool cabeza = dx * dx + dyc * dyc <= radio * radio;
            const bool hombros = rx > 0 && ry > 0 && (dx * dx) / (rx * rx) + (dyh * dyh) / (ry * ry) <= 1.0f;
            if (cabeza || hombros) p[x] = kFigura;
        }
    }
}

void borde(std::uint8_t* lienzo, int W, const Caja& c, int grosor, std::uint32_t color) {
    const int g = std::min({grosor, c.w / 2, c.h / 2});
    if (g <= 0) return;
    llenar(lienzo, W, {c.x, c.y, c.w, g}, color);
    llenar(lienzo, W, {c.x, c.y + c.h - g, c.w, g}, color);
    llenar(lienzo, W, {c.x, c.y, g, c.h}, color);
    llenar(lienzo, W, {c.x + c.w - g, c.y, g, c.h}, color);
}

/// Los dígitos y el «+» en 5x7 (un bit por píxel, de izquierda a derecha).
const std::uint8_t* glifo(char ch) {
    static const std::uint8_t kDig[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
    static const std::uint8_t kMas[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    if (ch == '+') return kMas;
    if (ch >= '0' && ch <= '9') return kDig[ch - '0'];
    return nullptr;
}

/// Encoge un rectángulo hacia su centro (`f` = 1 lo deja igual).
Grillas::Rect encoger(const Grillas::Rect& r, float f) {
    return {r.x + r.w * (1 - f) / 2, r.y + r.h * (1 - f) / 2, r.w * f, r.h * f};
}

std::uint32_t leerColor(const json& j, const char* k, std::uint32_t def) {
    std::uint32_t c = def;
    if (j.contains(k) && j[k].is_string() && Grillas::color(j[k].get<std::string>(), c)) return c;
    return def;
}

} // namespace

std::string nivelCalidad(const MediaRoute& ruta, int altoGrilla) {
    if (ruta.kind == MediaRoute::Kind::Program) return "libre";
    // El lugar más grande donde se lo ve manda: la grilla o la vista previa de la fila.
    int alto = altoGrilla > 0 ? altoGrilla : 0;
    if (ruta.kind == MediaRoute::Kind::Backstage) {
        if (ruta.index > 0) alto = std::max(alto, 360);
        if (alto == 0) return "180p";
    }
    if (alto == 0) return "libre"; // el lobby y lo demás: como siempre
    return alto <= 200 ? "180p" : alto <= 400 ? "360p" : "libre";
}

Grillas::Grillas(Log& log) : log_(log) {}

Grillas::~Grillas() { stop(); }

void Grillas::start(int cps) {
    if (hilo_.joinable()) return;
    cps_ = std::clamp(cps, 5, 60);
    parar_ = false;
    hilo_ = std::thread([this]() { bucle(); });
}

void Grillas::stop() {
    parar_ = true;
    if (hilo_.joinable()) hilo_.join();
    std::lock_guard<std::mutex> l(mu_);
    grillas_.clear(); // los canales se cierran (el motor pone «sin señal»)
}

void Grillas::setAlCambiarHablan(std::function<void(const std::vector<std::string>&)> fn) {
    std::lock_guard<std::mutex> l(fnMu_);
    alHablan_ = std::move(fn);
}

void Grillas::setAlCambiarCalidad(std::function<void(const std::string&, int)> fn) {
    std::lock_guard<std::mutex> l(fnMu_);
    alCalidad_ = std::move(fn);
}

int Grillas::altoDe(const std::string& persona) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = altos_.find(persona);
    return it != altos_.end() ? it->second : 0;
}

bool Grillas::color(const std::string& texto, std::uint32_t& bgra) {
    std::string h = !texto.empty() && texto[0] == '#' ? texto.substr(1) : texto;
    if (h.size() != 6 && h.size() != 8) return false;
    if (!std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
        return false;
    if (h.size() == 6) h = "FF" + h;
    // AARRGGBB en el texto; en memoria, B G R A (entero little-endian = AARRGGBB).
    bgra = static_cast<std::uint32_t>(std::stoul(h, nullptr, 16));
    return true;
}

std::int64_t Grillas::reloj() { return ahoraMs(); }

float Grillas::suavizar(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

Grillas::Rect Grillas::animado(const Anim& a, int durMs, std::int64_t ahora) {
    if (durMs <= 0) return a.hasta;
    const float e = suavizar(static_cast<float>(ahora - a.t0) / static_cast<float>(durMs));
    return {a.desde.x + (a.hasta.x - a.desde.x) * e, a.desde.y + (a.hasta.y - a.desde.y) * e,
            a.desde.w + (a.hasta.w - a.desde.w) * e, a.desde.h + (a.hasta.h - a.desde.h) * e};
}

// ---------------------------------------------------------------------------
// Lo que manda la app
// ---------------------------------------------------------------------------

std::string Grillas::aplicar(const json& pedido, std::int64_t ahora) {
    if (!pedido.is_object() || !pedido.contains("grillas") || !pedido["grillas"].is_array())
        return traducir("gateway", "falta «grillas» (una lista)");
    struct Nueva {
        int canal = 0, ancho = 0, alto = 0, transicion = 400;
        std::uint32_t fondo = 0, resaltar = 0xFF07C1FF;
        std::vector<Celda> celdas;
        int mas = 0;
        Rect rMas;
    };
    std::map<int, Nueva> nuevas;
    for (const json& g : pedido["grillas"]) {
        if (!g.is_object()) return traducir("gateway", "cada grilla es un objeto");
        Nueva n;
        n.canal = g.value("canal", 0);
        if (n.canal < 1 || n.canal > kMaxGrillas)
            return llenar(traducir("gateway", "«canal» va de 1 a %1"), {std::to_string(kMaxGrillas)});
        if (nuevas.count(n.canal)) return llenar(traducir("gateway", "el canal %1 está dos veces"), {std::to_string(n.canal)});
        n.ancho = g.value("ancho", 1280);
        n.alto = g.value("alto", 720);
        if (n.ancho < 16 || n.ancho > 3840 || n.alto < 16 || n.alto > 2160)
            return traducir("gateway", "tamaño fuera de 16x16..3840x2160");
        n.transicion = std::clamp(g.value("transicion_ms", 400), 0, 3000);
        n.fondo = leerColor(g, "fondo", 0);
        n.resaltar = leerColor(g, "resaltar_color", 0xFF07C1FF);
        const json celdas = g.value("celdas", json::array());
        if (!celdas.is_array()) return traducir("gateway", "«celdas» es una lista");
        if (celdas.size() > static_cast<std::size_t>(kMaxCeldasGrilla))
            return llenar(traducir("gateway", "más de %1 celdas"), {std::to_string(kMaxCeldasGrilla)});
        std::set<std::string> vistas;
        for (const json& c : celdas) {
            if (!c.is_object()) return traducir("gateway", "cada celda es un objeto");
            Celda x;
            x.persona = c.value("persona", std::string());
            if (x.persona.empty() || x.persona.size() > 200) return traducir("gateway", "celda sin «persona»");
            if (!vistas.insert(x.persona).second)
                return llenar(traducir("gateway", "«%1» está dos veces en la misma grilla"), {x.persona});
            x.r = {c.value("x", 0.0f), c.value("y", 0.0f), c.value("w", 0.0f), c.value("h", 0.0f)};
            if (x.r.w <= 0 || x.r.h <= 0) return llenar(traducir("gateway", "celda de «%1» sin tamaño"), {x.persona});
            x.resaltar = c.value("resaltar", false);
            n.celdas.push_back(std::move(x));
        }
        if (g.contains("mas") && g["mas"].is_object()) {
            const json& m = g["mas"];
            n.mas = std::max(0, m.value("n", 0));
            n.rMas = {m.value("x", 0.0f), m.value("y", 0.0f), m.value("w", 0.0f), m.value("h", 0.0f)};
        }
        nuevas[n.canal] = std::move(n);
    }

    std::vector<std::pair<std::string, int>> calidad;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (auto it = grillas_.begin(); it != grillas_.end();)
            it = nuevas.count(it->first) ? std::next(it) : grillas_.erase(it);
        for (auto& [canal, n] : nuevas) {
            Grilla& g = grillas_[canal];
            g.canal = canal;
            if (g.ancho != n.ancho || g.alto != n.alto) g.anim.clear(); // otro tamaño: sin viaje
            g.ancho = n.ancho;
            g.alto = n.alto;
            g.fondo = n.fondo;
            g.resaltar = n.resaltar;
            g.transicionMs = n.transicion;
            std::set<std::string> quedan;
            for (const Celda& c : n.celdas) {
                quedan.insert(c.persona);
                auto a = g.anim.find(c.persona);
                if (a != g.anim.end()) {
                    // Viaja desde donde está AHORA (aunque estuviera a mitad de otro viaje).
                    a->second.desde = animado(a->second, g.transicionMs, ahora);
                    a->second.hasta = c.r;
                    a->second.t0 = ahora;
                    a->second.sale = false;
                    a->second.resaltar = c.resaltar;
                } else {
                    Anim x;
                    x.desde = encoger(c.r, 0.3f); // la que entra crece desde el centro de su lugar
                    x.hasta = c.r;
                    x.t0 = ahora;
                    x.resaltar = c.resaltar;
                    g.anim[c.persona] = x;
                }
            }
            for (auto& [p, a] : g.anim) {
                if (quedan.count(p) || a.sale) continue;
                const Rect ahoraR = animado(a, g.transicionMs, ahora);
                a.desde = ahoraR;
                a.hasta = encoger(ahoraR, 0.01f); // la que sale se achica hasta desaparecer
                a.t0 = ahora;
                a.sale = true;
                a.resaltar = false;
            }
            if (g.transicionMs == 0)
                for (auto it = g.anim.begin(); it != g.anim.end();) it = it->second.sale ? g.anim.erase(it) : std::next(it);
            g.celdas = std::move(n.celdas);
            g.mas = n.mas;
            g.rMas = n.rMas;
            g.sucia = true;
        }
        // El lugar más grande de cada uno: a ese tamaño se guarda su retrato, y su alto
        // es la calidad que se le pide al navegador.
        std::map<std::string, std::pair<int, int>> tam;
        for (const auto& [k, g] : grillas_)
            for (const Celda& c : g.celdas) {
                auto& t = tam[c.persona];
                t.first = std::max(t.first, static_cast<int>(std::lround(c.r.w)));
                t.second = std::max(t.second, static_cast<int>(std::lround(c.r.h)));
            }
        std::map<std::string, int> altos;
        for (const auto& [p, t] : tam) altos[p] = t.second;
        for (const auto& [p, a] : altos)
            if (altos_.count(p) == 0 || altos_[p] != a) calidad.emplace_back(p, a);
        for (const auto& [p, a] : altos_)
            if (altos.count(p) == 0) calidad.emplace_back(p, 0);
        altos_ = std::move(altos);
        std::lock_guard<std::mutex> lr(retratosMu_);
        for (const auto& [p, t] : tam) {
            auto& r = retratos_[p];
            if (!r) r = std::make_shared<Retrato>();
            std::lock_guard<std::mutex> lm(r->m);
            r->quiereW = t.first;
            r->quiereH = t.second;
        }
    }
    std::function<void(const std::string&, int)> fn;
    {
        std::lock_guard<std::mutex> l(fnMu_);
        fn = alCalidad_;
    }
    if (fn)
        for (const auto& [p, a] : calidad) fn(p, a);
    return {};
}

json Grillas::estado() const {
    json out = json::array();
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [k, g] : grillas_) {
        json celdas = json::array();
        for (const Celda& c : g.celdas) {
            bool imagen = false;
            if (auto r = retrato(c.persona)) {
                std::lock_guard<std::mutex> lm(r->m);
                imagen = !r->px.empty();
            }
            celdas.push_back({{"persona", c.persona}, {"x", c.r.x}, {"y", c.r.y}, {"w", c.r.w}, {"h", c.r.h},
                              {"resaltar", c.resaltar}, {"imagen", imagen}});
        }
        out.push_back({{"canal", k},
                       {"ancho", g.ancho},
                       {"alto", g.alto},
                       {"celdas", celdas},
                       {"mas", g.mas},
                       {"ms_por_cuadro", std::round(g.msPintar * 100) / 100},
                       {"cuadros", g.pintados}});
    }
    json hablan = json::array();
    {
        std::lock_guard<std::mutex> lv(vozMu_);
        for (const auto& p : hablan_) hablan.push_back(p);
    }
    return {{"grillas", out}, {"hablan", hablan}, {"cps", cps_}};
}

bool Grillas::admin(long long fd, const std::string& method, const std::string& path, const std::string& body) {
    if (path != "/api/v1/grillas") return false;
    if (method == "GET") {
        http::sendResponse(fd, 200, "application/json", estado().dump(-1, ' ', false, json::error_handler_t::replace));
        return true;
    }
    if (method != "POST") {
        http::sendResponse(fd, 405, "application/json", R"({"ok":false,"error":"method"})");
        return true;
    }
    const json req = json::parse(body.empty() ? "{}" : body, nullptr, false);
    const std::string e = aplicar(req, ahoraMs());
    const json out = e.empty() ? json{{"ok", true}} : json{{"ok", false}, {"error", e}};
    http::sendResponse(fd, e.empty() ? 200 : 400, "application/json", out.dump(-1, ' ', false, json::error_handler_t::replace));
    return true;
}

// ---------------------------------------------------------------------------
// Lo que llega de cada persona
// ---------------------------------------------------------------------------

std::shared_ptr<Grillas::Retrato> Grillas::retrato(const std::string& persona) const {
    std::lock_guard<std::mutex> l(retratosMu_);
    const auto it = retratos_.find(persona);
    return it != retratos_.end() ? it->second : nullptr;
}

void Grillas::guardar(Retrato& r, const std::uint8_t* bgra, int w, int h, std::int64_t ahora, bool vivo) {
    std::lock_guard<std::mutex> l(r.m);
    // Achicado al lugar más grande que ocupa, cubriéndolo (el recorte se hace al pintar):
    // nunca se agranda, y lo que se guarda no pesa más de lo que se va a mostrar.
    float f = 1.0f;
    if (r.quiereW > 0 && r.quiereH > 0)
        f = std::min(1.0f, std::max(static_cast<float>(r.quiereW) / w, static_cast<float>(r.quiereH) / h));
    const int tw = std::max(1, static_cast<int>(std::lround(w * f)));
    const int th = std::max(1, static_cast<int>(std::lround(h * f)));
    r.px.resize(static_cast<std::size_t>(tw) * th * 4);
    if (tw == w && th == h)
        std::memcpy(r.px.data(), bgra, r.px.size());
    else
        escalar(bgra, w * 4, w, h, r.px.data(), tw * 4, tw, th);
    r.w = tw;
    r.h = th;
    r.ms = ahora;
    if (vivo) r.vivoMs = ahora;
    ++r.seq;
}

void Grillas::cuadro(const std::string& persona, const std::uint8_t* bgra, int w, int h, std::int64_t ahora) {
    if (bgra == nullptr || w <= 0 || h <= 0) return;
    const auto r = retrato(persona);
    if (!r) return; // no está en ninguna grilla: ni se toca
    {
        // Al ritmo de las grillas: un celular a 30 cps no hace achicar 30 veces para
        // pintar 25.
        std::lock_guard<std::mutex> l(r->m);
        if (r->vivoMs != 0 && ahora - r->vivoMs < 1000 / (cps_ + 5)) return;
    }
    guardar(*r, bgra, w, h, ahora, true);
}

void Grillas::foto(const std::string& persona, const std::string& jpeg, std::int64_t ahora) {
    const auto r = retrato(persona);
    if (!r || jpeg.size() < 4) return;
    {
        std::lock_guard<std::mutex> l(r->m);
        if (r->vivoMs != 0 && ahora - r->vivoMs < 3000) return; // tiene video en vivo: gana el video
    }
#if BA_GATEWAY_HAVE_WEBRTC
    const auto* datos = reinterpret_cast<const std::uint8_t*>(jpeg.data());
    int w = 0, h = 0;
    if (libyuv::MJPGSize(datos, jpeg.size(), &w, &h) != 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    if (libyuv::MJPGToARGB(datos, jpeg.size(), px.data(), w * 4, w, h, w, h) != 0) return;
    guardar(*r, px.data(), w, h, ahora, false);
#else
    (void)ahora; // sin libwebrtc no hay con qué abrir el JPEG: queda la silueta
#endif
}

void Grillas::audio(const std::string& persona, const float* s, std::size_t n, int canales, std::int64_t ahora) {
    if (s == nullptr || n == 0) return;
    (void)canales; // la energía se mide sobre todas las muestras intercaladas
    double e = 0;
    for (std::size_t i = 0; i < n; ++i) e += static_cast<double>(s[i]) * s[i];
    const double rms = std::sqrt(e / static_cast<double>(n));
    // -36 dBFS: la voz de alguien que habla a su celular pasa holgada; el ruido de una
    // casa con el micrófono abierto, no. Entra a los 200 ms seguidos (un golpe no es
    // hablar) y sale a los 1,2 s de silencio (las pausas entre frases no lo apagan).
    constexpr double kUmbral = 0.0158;
    bool cambio = false;
    std::vector<std::string> lista;
    {
        std::lock_guard<std::mutex> l(vozMu_);
        Voz& v = voces_[persona];
        if (rms > kUmbral) {
            if (v.sobreDesdeMs == 0) v.sobreDesdeMs = ahora;
            v.vozMs = ahora;
            if (!v.habla && ahora - v.sobreDesdeMs >= 200) v.habla = cambio = true;
        } else {
            if (ahora - v.vozMs > 250) v.sobreDesdeMs = 0;
            if (v.habla && ahora - v.vozMs > 1200) {
                v.habla = false;
                cambio = true;
            }
        }
        if (cambio) {
            if (v.habla)
                hablan_.insert(persona);
            else
                hablan_.erase(persona);
            lista.assign(hablan_.begin(), hablan_.end());
        }
    }
    if (!cambio) return;
    std::function<void(const std::vector<std::string>&)> fn;
    {
        std::lock_guard<std::mutex> l(fnMu_);
        fn = alHablan_;
    }
    if (fn) fn(lista);
}

// ---------------------------------------------------------------------------
// Pintar
// ---------------------------------------------------------------------------

void Grillas::dibujarMas(std::uint8_t* lienzo, int W, int H, const Rect& r, int n, std::uint32_t color) {
    const std::string texto = "+" + std::to_string(std::clamp(n, 0, 9999));
    const int cols = static_cast<int>(texto.size()) * 6 - 1;
    const Caja c = caja(r, W, H);
    const int esc = std::max(1, std::min(static_cast<int>(c.w * 0.7f / cols), static_cast<int>(c.h * 0.45f / 7)));
    const int x0 = c.x + (c.w - cols * esc) / 2;
    const int y0 = c.y + (c.h - 7 * esc) / 2;
    for (std::size_t i = 0; i < texto.size(); ++i) {
        const std::uint8_t* gl = glifo(texto[i]);
        if (gl == nullptr) continue;
        for (int fy = 0; fy < 7; ++fy)
            for (int fx = 0; fx < 5; ++fx)
                if (gl[fy] & (0x10 >> fx)) {
                    const Caja b{x0 + (static_cast<int>(i) * 6 + fx) * esc, y0 + fy * esc, esc, esc};
                    const Caja d = caja({static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.w),
                                         static_cast<float>(b.h)},
                                        W, H);
                    llenar(lienzo, W, d, color);
                }
    }
}

void Grillas::pintarLocked(Grilla& g, std::int64_t ahora) {
    const auto t1 = std::chrono::steady_clock::now();
    const int W = g.ancho, H = g.alto;
    g.lienzo.resize(static_cast<std::size_t>(W) * H * 4);
    {
        auto* p = reinterpret_cast<std::uint32_t*>(g.lienzo.data());
        std::fill(p, p + static_cast<std::size_t>(W) * H, g.fondo);
    }
    // Las que terminaron de salir se van.
    for (auto it = g.anim.begin(); it != g.anim.end();)
        it = it->second.sale && ahora - it->second.t0 >= g.transicionMs ? g.anim.erase(it) : std::next(it);

    const auto dibujar = [&](const std::string& persona, const Anim& a) {
        const Rect r = animado(a, g.transicionMs, ahora);
        const Caja c = caja(r, W, H);
        if (c.w < 2 || c.h < 2) return;
        std::uint8_t* destino = g.lienzo.data() + (static_cast<std::size_t>(c.y) * W + c.x) * 4;
        bool pintada = false;
        if (const auto ret = retrato(persona)) {
            std::lock_guard<std::mutex> l(ret->m);
            if (!ret->px.empty()) {
                // Recorte que llena el lugar (sin barras): al ancho o al alto, y en
                // vertical cargado hacia arriba, donde están las caras.
                const float aspL = static_cast<float>(c.w) / c.h, aspR = static_cast<float>(ret->w) / ret->h;
                int sx = 0, sy = 0, sw = ret->w, sh = ret->h;
                if (aspR > aspL) {
                    sw = std::max(1, static_cast<int>(ret->h * aspL));
                    sx = (ret->w - sw) / 2;
                } else {
                    sh = std::max(1, static_cast<int>(ret->w / aspL));
                    sy = static_cast<int>((ret->h - sh) * 0.3f);
                }
                escalar(ret->px.data() + (static_cast<std::size_t>(sy) * ret->w + sx) * 4, ret->w * 4, sw, sh, destino,
                        W * 4, c.w, c.h);
                g.visto[persona] = ret->seq;
                pintada = true;
            }
        }
        if (!pintada) silueta(g.lienzo.data(), W, c);
        if (a.resaltar) borde(g.lienzo.data(), W, c, std::max(3, c.h / 30), g.resaltar);
    };
    for (const auto& [p, a] : g.anim)
        if (a.sale) dibujar(p, a); // abajo de las que quedan
    for (const Celda& c : g.celdas) {
        const auto a = g.anim.find(c.persona);
        if (a != g.anim.end()) dibujar(c.persona, a->second);
    }
    if (g.mas > 0) {
        const Caja c = caja(g.rMas, W, H);
        llenar(g.lienzo.data(), W, c, 0xE0202020u);
        dibujarMas(g.lienzo.data(), W, H, g.rMas, g.mas, 0xFFFFFFFFu);
    }
    g.sucia = false;
    ++g.pintados;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    g.msPintar = g.pintados == 1 ? ms : g.msPintar * 0.95 + ms * 0.05;
}

bool Grillas::pintar(int canal, std::int64_t ahora, std::vector<std::uint8_t>& lienzo, int& w, int& h) {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = grillas_.find(canal);
    if (it == grillas_.end()) return false;
    pintarLocked(it->second, ahora);
    lienzo = it->second.lienzo;
    w = it->second.ancho;
    h = it->second.alto;
    return true;
}

void Grillas::limpiarRetratosLocked() {
    std::set<std::string> usados;
    for (const auto& [k, g] : grillas_)
        for (const auto& [p, a] : g.anim) usados.insert(p);
    std::lock_guard<std::mutex> l(retratosMu_);
    for (auto it = retratos_.begin(); it != retratos_.end();)
        it = usados.count(it->first) ? std::next(it) : retratos_.erase(it);
}

void Grillas::bucle() {
    const auto periodo = std::chrono::microseconds(1000000 / cps_);
    auto proximo = std::chrono::steady_clock::now();
    std::int64_t informeMs = ahoraMs();
    while (!parar_.load()) {
        const std::int64_t ahora = ahoraMs();
        {
            std::lock_guard<std::mutex> l(mu_);
            for (auto& [k, g] : grillas_) {
                // Se pinta si algo cambió: otra disposición, un viaje en curso, un cuadro
                // nuevo de alguien que se ve; si no, se repite cada medio segundo (el
                // motor pone «sin señal» a los 3 s sin cuadros).
                bool hace = g.sucia || ahora - g.enviadoMs >= 500;
                for (const auto& [p, a] : g.anim) hace = hace || ahora - a.t0 < g.transicionMs;
                if (!hace)
                    for (const Celda& c : g.celdas)
                        if (const auto r = retrato(c.persona)) {
                            std::lock_guard<std::mutex> lm(r->m);
                            const auto v = g.visto.find(c.persona);
                            if (!r->px.empty() && (v == g.visto.end() || v->second != r->seq)) {
                                hace = true;
                                break;
                            }
                        }
                if (!hace) continue;
                pintarLocked(g, ahora);
                if (!g.sender || g.senderW != g.ancho || g.senderH != g.alto) {
                    // El canal tiene tamaño fijo: otro tamaño = canal nuevo (bavideoanillo
                    // lo vuelve a tomar y renegocia).
                    g.sender.reset();
                    auto s = std::make_unique<ba::ipc::SpoutSender>();
                    const std::wstring nombre = L"BA.Grilla." + std::to_wstring(k);
                    if (s->publish(nombre.c_str(), ba::ipc::SlotConfig{g.ancho, g.alto})) {
                        log_.info(llenar(traducir("gateway", "grillas: BA.Grilla.%1 listo (%2x%3)"),
                                         {std::to_string(k), std::to_string(g.ancho), std::to_string(g.alto)}));
                        g.sender = std::move(s);
                        g.senderW = g.ancho;
                        g.senderH = g.alto;
                    } else {
                        log_.warn(llenar(traducir("gateway", "grillas: no se pudo publicar BA.Grilla.%1"), {std::to_string(k)}), -1);
                    }
                }
                if (g.sender) {
                    const std::uint64_t t = ba::ipc::HealthBlock::nowQpc();
                    g.sender->send(g.lienzo.data(), g.lienzo.size(), ba::ipc::FrameMeta{t, t});
                }
                g.enviadoMs = ahora;
            }
            limpiarRetratosLocked();
            if (ahora - informeMs >= 30000 && !grillas_.empty()) {
                informeMs = ahora;
                for (const auto& [k, g] : grillas_) {
                    char ms[32];
                    std::snprintf(ms, sizeof ms, "%.1f", static_cast<double>(g.msPintar));
                    log_.info(llenar(traducir("gateway", "grillas: BA.Grilla.%1 %2x%3, %4 personas (+%5), %6 ms por cuadro"),
                                     {std::to_string(k), std::to_string(g.ancho), std::to_string(g.alto),
                                      std::to_string(g.celdas.size()), std::to_string(g.mas), ms}));
                }
            }
        }
        proximo += periodo;
        const auto ya = std::chrono::steady_clock::now();
        if (proximo < ya) proximo = ya; // atrasado (una pausa larga): sin ráfaga para alcanzar
        std::this_thread::sleep_until(proximo);
    }
}

} // namespace ba::gateway
