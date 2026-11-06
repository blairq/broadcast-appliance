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
#include "diag/Frontera.hpp"
#include "i18n/I18nTexto.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SpoutTransport.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"
#include "platform/Ritmo.hpp"

#include "libyuv/convert.h"
#include "libyuv/convert_argb.h"
#include "libyuv/scale.h"
#include "libyuv/scale_argb.h"

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
/// caja con SIMD (achicar promediando: una cara de 640 en 200 píxeles no titila).
void escalar(const std::uint8_t* src, int sstride, int sw, int sh, std::uint8_t* dst, int dstride, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    libyuv::ARGBScale(src, sstride, sw, sh, dst, dstride, dw, dh, libyuv::kFilterBox);
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

/// El recorte de una imagen (iw x ih) que llena una celda (cw x ch) sin barras: al ancho o
/// al alto, y en vertical cargado hacia arriba, donde están las caras.
void recorte(int iw, int ih, int cw, int ch, int& sx, int& sy, int& sw, int& sh) {
    const float aspL = static_cast<float>(cw) / ch, aspR = static_cast<float>(iw) / ih;
    sx = sy = 0;
    sw = iw;
    sh = ih;
    if (aspR > aspL) {
        sw = std::max(1, static_cast<int>(ih * aspL));
        sx = (iw - sw) / 2;
    } else {
        sh = std::max(1, static_cast<int>(iw / aspL));
        sy = static_cast<int>((ih - sh) * 0.3f);
    }
}

/// Un color BGRA con opacidad extra `a` (0..255) sobre el píxel `p` (BGRA, con alfa).
inline void sobre(std::uint8_t* p, std::uint32_t color, unsigned a) {
    a = a * ((color >> 24) & 0xFF) / 255;
    if (a == 0) return;
    const unsigned na = 255 - a;
    p[0] = static_cast<std::uint8_t>(((color & 0xFF) * a + p[0] * na) / 255);
    p[1] = static_cast<std::uint8_t>((((color >> 8) & 0xFF) * a + p[1] * na) / 255);
    p[2] = static_cast<std::uint8_t>((((color >> 16) & 0xFF) * a + p[2] * na) / 255);
    p[3] = static_cast<std::uint8_t>(a + p[3] * na / 255);
}

/// El borde a medio fundir: `a` (0..255) de opacidad sobre lo pintado.
void bordeMezcla(std::uint8_t* lienzo, int W, const Caja& c, int grosor, std::uint32_t color, unsigned a) {
    const int g = std::min({grosor, c.w / 2, c.h / 2});
    if (g <= 0 || a == 0) return;
    const auto banda = [&](const Caja& b) {
        for (int y = b.y; y < b.y + b.h; ++y) {
            std::uint8_t* p = lienzo + (static_cast<std::size_t>(y) * W + b.x) * 4;
            for (int x = 0; x < b.w; ++x) sobre(p + x * 4, color, a);
        }
    };
    banda({c.x, c.y, c.w, g});
    banda({c.x, c.y + c.h - g, c.w, g});
    banda({c.x, c.y + g, g, c.h - 2 * g}); // los costados sin las esquinas (no se pintan dos veces)
    banda({c.x + c.w - g, c.y + g, g, c.h - 2 * g});
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

void Grillas::start(int cps, std::wstring lienzo) {
    if (hilo_.joinable()) return;
    cps_ = std::clamp(cps, 5, 60);
    lienzo_ = std::move(lienzo);
    periodoPintarNs_ = 1'000'000'000ULL / static_cast<std::uint64_t>(cps_);
    parar_ = false;
    hilo_ = std::thread([this]() {
        ba::platform::nombrarHilo("grillas");
        ba::platform::nucleos::alGrupoGateway(); // F4: el bloque de caché del gateway
        bucle();
    });
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

void Grillas::setAlCambiarCapa(std::function<void(const std::string&)> fn) {
    std::lock_guard<std::mutex> l(fnMu_);
    alCapa_ = std::move(fn);
}

Capa Grillas::capaDe(const std::string& persona) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = capas_.find(persona);
    return it != capas_.end() ? it->second : Capa::Nada;
}

bool Grillas::precargada(const std::string& persona) const {
    std::lock_guard<std::mutex> l(mu_);
    return precarga_.count(persona) > 0;
}

bool Grillas::tieneRotulo(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    return rotulos_.count(id) > 0;
}

std::string Grillas::rotulos(const json& pedido) {
    if (!pedido.is_object() || !pedido.contains("rotulos") || !pedido["rotulos"].is_array())
        return traducir("gateway", "falta «rotulos» (una lista)");
    std::vector<std::pair<std::string, std::shared_ptr<Rotulo>>> nuevos;
    for (const json& r : pedido["rotulos"]) {
        if (!r.is_object()) return traducir("gateway", "cada rótulo es un objeto");
        const std::string id = r.value("id", std::string());
        const int w = r.value("w", 0), h = r.value("h", 0);
        if (id.empty() || id.size() > 100) return traducir("gateway", "rótulo sin «id»");
        if (w < 1 || h < 1 || w > 2048 || h > 512) return llenar(traducir("gateway", "rótulo «%1» de tamaño inválido"), {id});
        const std::string a8 = http::deBase64(r.value("a8", std::string()));
        if (a8.size() != static_cast<std::size_t>(w) * h)
            return llenar(traducir("gateway", "rótulo «%1»: la máscara no mide w*h"), {id});
        auto x = std::make_shared<Rotulo>();
        x->w = w;
        x->h = h;
        x->a8.assign(a8.begin(), a8.end());
        nuevos.emplace_back(id, std::move(x));
    }
    std::lock_guard<std::mutex> l(mu_);
    for (auto& [id, x] : nuevos) rotulos_[id] = std::move(x);
    for (auto& [k, g] : grillas_) g.sucia = true; // los que faltaban ya se pueden pintar
    return {};
}

void Grillas::fundir(const std::string& persona, int ms) {
    if (ms <= 0) return;
    if (const auto r = retrato(persona)) {
        std::lock_guard<std::mutex> l(r->m);
        r->fundidoPendienteMs = ms;
    }
}

void Grillas::cuadroAnterior(const std::string& persona, const std::uint8_t* bgra, int w, int h) {
    if (bgra == nullptr || w <= 0 || h <= 0) return;
    if (const auto r = retrato(persona)) {
        std::lock_guard<std::mutex> l(r->m);
        r->fundido.anterior(bgra, w, h); // la baja: chica, se guarda tal cual
    }
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
std::int64_t Grillas::relojUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float Grillas::suavizar(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

Grillas::Rect Grillas::interpolar(const Rect& desde, const Rect& hasta, std::int64_t t0, int durMs, std::int64_t ahora) {
    if (durMs <= 0) return hasta;
    const float e = suavizar(static_cast<float>(ahora - t0) / static_cast<float>(durMs));
    return {desde.x + (hasta.x - desde.x) * e, desde.y + (hasta.y - desde.y) * e, desde.w + (hasta.w - desde.w) * e,
            desde.h + (hasta.h - desde.h) * e};
}

Grillas::Rect Grillas::animado(const Anim& a, int durMs, std::int64_t ahora) {
    return interpolar(a.desde, a.hasta, a.t0, durMs, ahora);
}

bool Grillas::mismoLugar(const Rect& a, const Rect& b) {
    constexpr float kTol = 0.5f;
    return std::fabs(a.x - b.x) <= kTol && std::fabs(a.y - b.y) <= kTol && std::fabs(a.w - b.w) <= kTol &&
           std::fabs(a.h - b.h) <= kTol;
}

float Grillas::alfaBorde(float desde, bool con, std::int64_t t0, int durMs, std::int64_t ahora) {
    const float hasta = con ? 1.0f : 0.0f;
    if (durMs <= 0) return hasta;
    const float e = suavizar(static_cast<float>(ahora - t0) / static_cast<float>(durMs));
    return std::clamp(desde + (hasta - desde) * e, 0.0f, 1.0f);
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
        EstiloRotulo estilo;
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
        if (g.contains("rotulos") && g["rotulos"].is_object()) {
            const json& e = g["rotulos"];
            n.estilo.color = leerColor(e, "color", n.estilo.color);
            n.estilo.fondo = leerColor(e, "fondo", n.estilo.fondo);
            n.estilo.tamano = std::clamp(e.value("tamano", n.estilo.tamano), 0.03f, 0.5f);
            n.estilo.arriba = e.value("lugar", std::string("abajo")) == "arriba";
        }
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
            const std::string capa = c.value("capa", std::string());
            x.capa = capa == "alta"    ? Capa::Alta
                     : capa == "media" ? Capa::Media // 360p: la alta topada (Capas.hpp)
                     : capa == "baja"  ? Capa::Baja
                                       : Capa::Nada;
            x.rotulo = c.value("rotulo", std::string());
            n.celdas.push_back(std::move(x));
        }
        if (g.contains("mas") && g["mas"].is_object()) {
            const json& m = g["mas"];
            n.mas = std::max(0, m.value("n", 0));
            n.rMas = {m.value("x", 0.0f), m.value("y", 0.0f), m.value("w", 0.0f), m.value("h", 0.0f)};
        }
        nuevas[n.canal] = std::move(n);
    }
    std::set<std::string> precarga;
    if (pedido.contains("precarga") && pedido["precarga"].is_array())
        for (const json& p : pedido["precarga"])
            if (p.is_string() && !p.get<std::string>().empty()) precarga.insert(p.get<std::string>());
    if (pedido.contains("fundido_ms") && pedido["fundido_ms"].is_number())
        fundidoMs_ = std::clamp(pedido["fundido_ms"].get<int>(), 0, 3000);

    std::vector<std::pair<std::string, int>> calidad;
    std::vector<std::string> capaCambio;
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
            g.estiloRotulo = n.estilo;
            std::set<std::string> quedan;
            for (const Celda& c : n.celdas) {
                quedan.insert(c.persona);
                auto a = g.anim.find(c.persona);
                if (a != g.anim.end()) {
                    Anim& x = a->second;
                    // Viaja desde donde está AHORA (aunque estuviera a mitad de otro viaje), pero
                    // solo si su lugar cambió: si ya iba (o estaba) ahí, sigue sin rearrancar.
                    if (x.sale || !mismoLugar(x.hasta, c.r)) {
                        x.desde = animado(x, g.transicionMs, ahora);
                        x.hasta = c.r;
                        x.t0 = ahora;
                    }
                    x.sale = false;
                    // El borde funde desde lo que se ve ahora hacia lo nuevo (si cambió).
                    if (x.resaltar != c.resaltar) {
                        x.bordeDesde = alfaBorde(x.bordeDesde, x.resaltar, x.bordeT0, g.transicionMs, ahora);
                        x.bordeT0 = ahora;
                        x.resaltar = c.resaltar;
                    }
                    x.rotulo = c.rotulo;
                } else {
                    Anim x;
                    x.desde = encoger(c.r, 0.3f); // la que entra crece desde el centro de su lugar
                    x.hasta = c.r;
                    x.t0 = ahora;
                    x.resaltar = c.resaltar;
                    x.bordeDesde = 0; // y su borde, si lo lleva, aparece a la vez
                    x.bordeT0 = ahora;
                    x.rotulo = c.rotulo;
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
                a.bordeDesde = alfaBorde(a.bordeDesde, a.resaltar, a.bordeT0, g.transicionMs, ahora);
                a.bordeT0 = ahora;
                a.resaltar = false;
                a.rotulo.clear();
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
        // La capa que piden (alta si alguna grilla la pide) y los precargados: a quien cambia
        // se le vuelve a decidir la capa.
        std::map<std::string, Capa> capas;
        for (const auto& [k, g] : grillas_)
            for (const Celda& c : g.celdas)
                if (c.capa != Capa::Nada) capas[c.persona] = std::max(capas[c.persona], c.capa);
        std::set<std::string> tocadas;
        for (const auto& [p, c] : capas)
            if (const auto it = capas_.find(p); it == capas_.end() || it->second != c) tocadas.insert(p);
        for (const auto& [p, c] : capas_)
            if (!capas.count(p)) tocadas.insert(p);
        for (const auto& p : precarga)
            if (!precarga_.count(p)) tocadas.insert(p);
        for (const auto& p : precarga_)
            if (!precarga.count(p)) tocadas.insert(p);
        capas_ = std::move(capas);
        precarga_ = std::move(precarga);
        capaCambio.assign(tocadas.begin(), tocadas.end());
        // Los rótulos que ninguna grilla nombra se olvidan (la app los vuelve a mandar si hace falta).
        std::set<std::string> nombrados;
        for (const auto& [k, g] : grillas_)
            for (const Celda& c : g.celdas)
                if (!c.rotulo.empty()) nombrados.insert(c.rotulo);
        for (auto it = rotulos_.begin(); it != rotulos_.end();)
            it = nombrados.count(it->first) ? std::next(it) : rotulos_.erase(it);
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
    std::function<void(const std::string&)> fnCapa;
    {
        std::lock_guard<std::mutex> l(fnMu_);
        fn = alCalidad_;
        fnCapa = alCapa_;
    }
    if (fn)
        for (const auto& [p, a] : calidad) fn(p, a);
    if (fnCapa)
        for (const auto& p : capaCambio) fnCapa(p);
    return {};
}

json Grillas::estado() const {
    json out = json::array();
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [k, g] : grillas_) {
        json celdas = json::array();
        for (const Celda& c : g.celdas) {
            bool imagen = false, fundiendo = false;
            int iw = 0, ih = 0;
            if (auto r = retrato(c.persona)) {
                std::lock_guard<std::mutex> lm(r->m);
                if (const Foto* u = r->ultimo(); u != nullptr && !u->px.empty()) {
                    imagen = true;
                    iw = u->w;
                    ih = u->h;
                }
                fundiendo = !r->fundido.imagen().empty(); // en curso (se suelta al terminar, al pintar)
            }
            // imagen_ancho/alto: el retrato guardado (achicado al lugar): con la HD, el del
            // lugar; con la baja, a lo sumo 320x180. Así se ve desde afuera qué capa se pinta.
            json x{{"persona", c.persona}, {"x", c.r.x},       {"y", c.r.y},         {"w", c.r.w},
                   {"h", c.r.h},           {"resaltar", c.resaltar}, {"imagen", imagen}, {"imagen_ancho", iw},
                   {"imagen_alto", ih},    {"fundiendo", fundiendo}};
            if (c.capa != Capa::Nada) x["capa"] = nombreCapa(c.capa);
            // El ritmo de cada tramo (diag/RitmoCuadros.hpp), con el período de su capa.
            const std::int64_t esperadoUs = c.capa == Capa::Baja ? 1'000'000 / 15 : c.capa == Capa::Nada ? 0 : 1'000'000 / 30;
            const auto ritmoJson = [esperadoUs](const ba::diag::RitmoCuadros& rc) {
                const auto z = rc.resumen(esperadoUs);
                const auto r1 = [](double v) { return std::round(v * 10) / 10; };
                return json{{"cps", r1(z.cps)}, {"p50_ms", r1(z.p50Ms)}, {"p95_ms", r1(z.p95Ms)}, {"max_ms", r1(z.maxMs)},
                            {"huecos", z.huecos}, {"n", z.n}};
            };
            json ritmo = json::object();
            BA_SONDA(if (auto r = retrato(c.persona)) {
                ritmo["red"] = ritmoJson(r->ritmoRed);
                ritmo["deco"] = ritmoJson(r->ritmoDeco);
                ritmo["guardado"] = ritmoJson(r->ritmoGuardado);
                ritmo["tirados"] = r->tirados.load(std::memory_order_relaxed);
            })
            if (const auto pin = g.pintado.find(c.persona); pin != g.pintado.end() && pin->second) {
                ritmo["pintado"] = ritmoJson(pin->second->ritmo);
                ritmo["salteados"] = pin->second->salteados;
                ritmo["repetidos"] = pin->second->repetidos;
            }
            x["ritmo"] = std::move(ritmo);
            if (auto r = retrato(c.persona)) {
                std::lock_guard<std::mutex> lm(r->m);
                if (r->atrasoQpc != 0) x["atraso_ms"] = std::round(r->atrasoNs / 1e5) / 10; // de su instante a la cola
            }
            if (!c.rotulo.empty()) {
                x["rotulo"] = c.rotulo;
                x["rotulo_listo"] = rotulos_.count(c.rotulo) > 0;
            }
            celdas.push_back(std::move(x));
        }
        out.push_back({{"canal", k},
                       {"ancho", g.ancho},
                       {"alto", g.alto},
                       {"celdas", celdas},
                       {"mas", g.mas},
                       {"ms_por_cuadro", std::round(g.msPintar * 100) / 100},
                       // El mosaico muestra el instante tic − espera en todas las celdas (null: por orden de llegada).
                       {"espera_ms", g.esperaNs >= 0 ? json(std::round(g.esperaNs / 1e5) / 10) : json(nullptr)},
                       {"ritmo_publicado", [&] {
                            json j = json::object();
                            BA_SONDA(const auto z = g.ritmoPublicado->resumen(static_cast<std::int64_t>(periodoPintarNs_.load() / 1000));
                                     j = json{{"cps", std::round(z.cps * 10) / 10}, {"p50_ms", z.p50Ms}, {"p95_ms", z.p95Ms},
                                              {"max_ms", z.maxMs}, {"huecos", z.huecos}};)
                            return j;
                        }()},
                       {"cuadros", g.pintados}});
    }
    json hablan = json::array();
    {
        std::lock_guard<std::mutex> lv(vozMu_);
        for (const auto& p : hablan_) hablan.push_back(p);
    }
    json precarga = json::array();
    for (const auto& p : precarga_) precarga.push_back(p);
    return {{"grillas", out},      {"hablan", hablan},
            {"cps", cps_},         {"precarga", precarga},
            {"fundido_ms", fundidoMs_.load()}, {"rotulos", rotulos_.size()}};
}

bool Grillas::admin(long long fd, const std::string& method, const std::string& path, const std::string& body) {
    if (path == "/api/v1/grillas/rotulos") {
        if (method != "POST") {
            http::sendResponse(fd, 405, "application/json", R"({"ok":false,"error":"method"})");
            return true;
        }
        const json req = json::parse(body.empty() ? "{}" : body, nullptr, false);
        const std::string e = rotulos(req);
        const json out = e.empty() ? json{{"ok", true}} : json{{"ok", false}, {"error", e}};
        http::sendResponse(fd, e.empty() ? 200 : 400, "application/json",
                           out.dump(-1, ' ', false, json::error_handler_t::replace));
        return true;
    }
    if (path == "/api/v1/grillas/trazas") {
        // SONDA: los últimos 15 s de cada persona (Retrato::PasoGuardado / PasoElegido), en µs
        // del reloj de los sellos. Sin las sondas: {"sondas": false}.
        json out{{"sondas", ba::diag::kSondasCuadros}};
        BA_SONDA({
            const std::int64_t ahoraUs = static_cast<std::int64_t>(ba::ipc::HealthBlock::nowQpc() / 1000);
            const std::int64_t desde = ahoraUs - 15'000'000;
            std::map<std::string, std::shared_ptr<Retrato>> rs;
            {
                std::lock_guard<std::mutex> l(retratosMu_);
                for (const auto& [k, v] : retratos_) rs[k] = v;
            }
            json personas = json::object();
            for (const auto& [k, r] : rs) {
                json gs = json::array(), es = json::array();
                std::lock_guard<std::mutex> l(r->m);
                for (std::uint64_t i = r->nG > Retrato::kTraza ? r->nG - Retrato::kTraza : 0; i < r->nG; ++i) {
                    const auto& x = r->trazaG[i % Retrato::kTraza];
                    if (x.t >= desde)
                        gs.push_back({x.t, x.red, x.rtp, x.instante, x.desfase, x.contenido, x.seq, std::round(x.brillo)});
                }
                for (std::uint64_t i = r->nE > Retrato::kTraza ? r->nE - Retrato::kTraza : 0; i < r->nE; ++i) {
                    const auto& x = r->trazaE[i % Retrato::kTraza];
                    if (x.tic >= desde) es.push_back({x.tic, x.objetivo, x.instante, x.seq, x.canal});
                }
                personas[k] = {{"g", std::move(gs)}, {"e", std::move(es)}};
            }
            out["ahora_us"] = ahoraUs;
            out["campos_g"] = {"t", "red", "rtp", "instante", "desfase", "contenido", "seq", "brillo"};
            out["campos_e"] = {"tic", "objetivo", "instante", "seq", "canal"};
            out["personas"] = std::move(personas);
        })
        http::sendResponse(fd, 200, "application/json", out.dump(-1, ' ', false, json::error_handler_t::replace));
        return true;
    }
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

void Grillas::guardar(std::shared_ptr<Retrato> r, const std::uint8_t* bgra, int w, int h, std::int64_t ahora, bool vivo) {
    PedidoCelda p = pedirEn(std::move(r), w, h, ahora, vivo);
    if (p.ancho == w && p.alto == h)
        std::memcpy(p.bgra.data(), bgra, static_cast<std::size_t>(w) * h * 4);
    else
        escalar(bgra, w * 4, w, h, p.bgra.data(), p.ancho * 4, p.ancho, p.alto);
    guardarCelda(std::move(p));
}

Grillas::PedidoCelda Grillas::pedirEn(std::shared_ptr<Retrato> r, int w, int h, std::int64_t ahora, bool vivo) {
    PedidoCelda p;
    if (!r || w <= 0 || h <= 0) return p;
    int quiereW = 0, quiereH = 0;
    {
        std::lock_guard<std::mutex> l(r->m);
        p.bgra.swap(r->repuesto);
        quiereW = r->quiereW;
        quiereH = r->quiereH;
    }
    // Achicado al lugar más grande que ocupa, cubriéndolo (el recorte se hace al pintar):
    // nunca se agranda, y lo que se guarda no pesa más de lo que se va a mostrar.
    float f = 1.0f;
    if (quiereW > 0 && quiereH > 0)
        f = std::min(1.0f, std::max(static_cast<float>(quiereW) / w, static_cast<float>(quiereH) / h));
    p.ancho = std::max(1, static_cast<int>(std::lround(w * f)));
    p.alto = std::max(1, static_cast<int>(std::lround(h * f)));
    p.bgra.resize(static_cast<std::size_t>(p.ancho) * p.alto * 4);
    p.retrato_ = std::move(r);
    p.ms_ = ahora;
    p.vivo_ = vivo;
    return p;
}

Grillas::PedidoCelda Grillas::pedirCelda(const std::string& persona, int w, int h, std::int64_t ahora, std::int64_t redUs,
                                          std::int64_t decoUs) {
    if (w <= 0 || h <= 0) return {};
    auto r = retratoParaCuadro(persona, ahora, redUs, decoUs);
    if (!r) return {};
    return pedirEn(std::move(r), w, h, ahora, true);
}

void Grillas::soltarCelda(PedidoCelda&& p) {
    if (!p.retrato_) return;
    std::lock_guard<std::mutex> l(p.retrato_->m);
    if (p.retrato_->repuesto.empty()) p.retrato_->repuesto.swap(p.bgra);
    p.retrato_.reset();
}

void Grillas::guardarCelda(PedidoCelda&& p) {
    if (!p.retrato_ || p.ancho <= 0 || p.alto <= 0 || p.bgra.size() < static_cast<std::size_t>(p.ancho) * p.alto * 4) {
        soltarCelda(std::move(p));
        return;
    }
    Retrato& r = *p.retrato_;
    const std::int64_t ahora = p.ms_;
    std::lock_guard<std::mutex> l(r.m);
    // El primer cuadro de la alta: funde desde lo último que se vio (si es reciente: uno
    // viejo sería un salto).
    if (p.vivo_ && r.fundidoPendienteMs > 0) {
        if (const Foto* u = r.ultimo(); u != nullptr && ahora - u->ms < 2000)
            r.fundido.empezar(u->px.data(), u->w, u->h, r.fundidoPendienteMs, ahora);
        r.fundidoPendienteMs = 0;
    }
    ++r.seq;
    BA_SONDA(r.ritmoGuardado.cuadro(relojUs());)
    Foto& nueva = r.cola[r.seq % kCola]; // pisa el más viejo
    nueva.px.swap(p.bgra);
    r.repuesto.swap(p.bgra); // el que se pisó queda para el próximo
    nueva.w = p.ancho;
    nueva.h = p.alto;
    nueva.ms = ahora;
    nueva.seq = r.seq;
    // El instante que muestra: el de su captura (sin hora RTP de red, la hora de su contenido).
    std::uint64_t instante = r.instanteDe(p.rtp, p.llegadaRedQpc);
    if (instante == 0) instante = p.contenidoQpc;
    nueva.instante = instante;
    if (instante != 0) {
        // Lo que tardó en llegar acá desde su instante: sube 1/8 y baja 1/128 de lo que se
        // aparta (un percentil alto: el pintor espera eso para que esté en la cola a su hora).
        const std::uint64_t llego = ba::ipc::HealthBlock::nowQpc();
        const double d = static_cast<double>(static_cast<std::int64_t>(llego - instante));
        if (r.atrasoQpc == 0 || llego > r.atrasoQpc + 2'000'000'000ULL) r.atrasoNs = d; // el primero (o tras una pausa)
        else r.atrasoNs += (d - r.atrasoNs) / (d > r.atrasoNs ? 8 : 128);
        r.atrasoQpc = llego;
    }
    BA_SONDA({
        // El brillo de la caja del destello de hinchada (--sincronia si: abajo a la derecha,
        // 1/8 del lado; 3×3 puntos de su centro, como motor/AnalisisCeldas::brilloDestello).
        float suma = 0;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                const int py = std::clamp(static_cast<int>((0.905 + 0.03 * j) * nueva.h), 0, nueva.h - 1);
                const int px = std::clamp(static_cast<int>((0.905 + 0.03 * i) * nueva.w), 0, nueva.w - 1);
                const std::uint8_t* q = nueva.px.data() + (static_cast<std::size_t>(py) * nueva.w + px) * 4;
                suma += (static_cast<float>(q[0]) + q[1] + q[2]) / 3.0f;
            }
        Retrato::PasoGuardado& g = r.trazaG[r.nG++ % Retrato::kTraza];
        g = {static_cast<std::int64_t>(ba::ipc::HealthBlock::nowQpc() / 1000),
             static_cast<std::int64_t>(p.llegadaRedQpc / 1000),
             r.conRtp ? r.rtpExt * 1'000'000LL / 90000 : 0,
             static_cast<std::int64_t>(instante / 1000),
             r.desfaseRtpNs / 1000,
             static_cast<std::int64_t>(p.contenidoQpc / 1000),
             nueva.seq,
             suma / 9.0f};
    })
    if (p.vivo_) r.vivoMs = ahora;
}

std::uint64_t Grillas::Retrato::instanteDe(std::uint32_t rtp, std::uint64_t llegadaRedQpc) {
    if (llegadaRedQpc == 0) return 0;
    // La hora RTP, desenvuelta (32 bits a 90 kHz dan la vuelta cada 13 h). Un salto de más de
    // 5 s (otra fuente RTP: el que manda se reconectó o cambió de capa) empieza de nuevo.
    const std::int32_t paso = static_cast<std::int32_t>(rtp - rtpUltimo);
    bool nuevo = !conRtp || paso > 5 * 90000 || paso < -5 * 90000;
    rtpExt = nuevo ? static_cast<std::int64_t>(rtp) : rtpExt + paso;
    rtpUltimo = rtp;
    const std::int64_t rtpNs = rtpExt * 1'000'000'000LL / 90000;
    const std::int64_t d = static_cast<std::int64_t>(llegadaRedQpc) - rtpNs;
    // El desfase: lo más corto que tardó en llegar (el mínimo; sube de a 20 µs por cuadro para
    // seguir la deriva entre los dos relojes). Uno que llega 300 ms más tarde que el mínimo no
    // es la red: el reloj RTP cambió sin saltar (se toma de nuevo).
    if (!nuevo && d > desfaseRtpNs + 300'000'000LL) nuevo = true;
    desfaseRtpNs = nuevo ? d : std::min(desfaseRtpNs + 20'000, d);
    conRtp = true;
    const std::int64_t q = rtpNs + desfaseRtpNs;
    return q > 0 ? static_cast<std::uint64_t>(q) : 0;
}

std::shared_ptr<Grillas::Retrato> Grillas::retratoParaCuadro(const std::string& persona, std::int64_t ahora, std::int64_t redUs,
                                                             std::int64_t decoUs) const {
    auto r = retrato(persona);
    if (!r) return nullptr; // no está en ninguna grilla: ni se toca
    BA_SONDA(if (redUs > 0) r->ritmoRed.cuadro(redUs); r->ritmoDeco.cuadro(decoUs > 0 ? decoUs : relojUs());)
    (void)redUs;
    (void)decoUs;
    {
        // Lo que llega mucho más rápido que el ritmo de las grillas (60 cps contra 30) se
        // achicaría para no pintarse. Un balde: entra a 1,5 veces el ritmo del pintor en
        // promedio, con ráfagas de hasta 5. El decodificador entrega a ráfagas (con 10
        // invitados, de a 3 o 4 después de un hueco): con ráfagas de 2 se tiraba el 3-5 % de
        // una cámara a 30; antes, el que llegaba a menos de medio período del anterior se tiraba.
        std::lock_guard<std::mutex> l(r->m);
        const double periodoMs = static_cast<double>(periodoPintarNs_.load(std::memory_order_relaxed)) / 1e6;
        constexpr double kRafaga = 5; // fijo: no crece con la cola (que guarda más para la espera común)
        if (r->creditoMs == 0 || ahora < r->creditoMs) r->credito = kRafaga;
        else r->credito = std::min(kRafaga, r->credito + 1.5 * static_cast<double>(ahora - r->creditoMs) / periodoMs);
        r->creditoMs = ahora;
        if (r->credito < 1.0) {
            BA_SONDA(r->tirados.fetch_add(1, std::memory_order_relaxed);)
            return nullptr;
        }
        r->credito -= 1.0;
    }
    return r;
}

void Grillas::cuadro(const std::string& persona, const std::uint8_t* bgra, int w, int h, std::int64_t ahora) {
    if (bgra == nullptr || w <= 0 || h <= 0) return;
    if (auto r = retratoParaCuadro(persona, ahora)) guardar(std::move(r), bgra, w, h, ahora, true);
}

void Grillas::achicarYuv(const PlanosYuv& c, std::uint8_t* px, int tw, int th, std::vector<std::uint8_t>& planos) {
    const int cw = (tw + 1) / 2, ch = (th + 1) / 2;
    // De la mitad al tamaño entero, del BGRA si ya está (PlanosYuv.hpp): a la mitad justa
    // el «box» de BGRA también gana (73 us contra 97).
    if (c.bgra != nullptr && (tw * 2 >= c.ancho || th * 2 >= c.alto)) {
        if (tw == c.ancho && th == c.alto)
            std::memcpy(px, c.bgra, static_cast<std::size_t>(tw) * th * 4);
        else
            escalar(c.bgra, c.ancho * 4, c.ancho, c.alto, px, tw * 4, tw, th);
        return;
    }
    if (tw == c.ancho && th == c.alto) {
        libyuv::I420ToARGB(c.y, c.strideY, c.u, c.strideU, c.v, c.strideV, px, tw * 4, tw, th);
        return;
    }
    planos.resize(static_cast<std::size_t>(tw) * th + 2 * static_cast<std::size_t>(cw) * ch);
    std::uint8_t* y = planos.data();
    std::uint8_t* u = y + static_cast<std::size_t>(tw) * th;
    std::uint8_t* v = u + static_cast<std::size_t>(cw) * ch;
    libyuv::I420Scale(c.y, c.strideY, c.u, c.strideU, c.v, c.strideV, c.ancho, c.alto, y, tw, u, cw, v, cw, tw, th,
                      libyuv::kFilterBox);
    libyuv::I420ToARGB(y, tw, u, cw, v, cw, px, tw * 4, tw, th);
}

void Grillas::cuadro(const std::string& persona, const PlanosYuv& c, std::int64_t ahora) {
    if (c.y == nullptr || c.u == nullptr || c.v == nullptr || c.ancho <= 0 || c.alto <= 0) return;
    PedidoCelda p = pedirCelda(persona, c.ancho, c.alto, ahora);
    if (!p.vale()) return;
    // #6 del plan: el cuadro decodificado se achica a su retrato en el hilo que decodificó.
    static ba::diag::Frontera& fCelda = ba::diag::frontera("grilla.cuadro", 6, "decodificador → retrato de la grilla (achicar)");
    ba::diag::MedirPase medir(fCelda, static_cast<std::uint64_t>(c.ancho) * c.alto * 3 / 2);
    thread_local std::vector<std::uint8_t> planos; // el I420 achicado (lo chico), uno por hilo que decodifica
    achicarYuv(c, p.bgra.data(), p.ancho, p.alto, planos);
    guardarCelda(std::move(p));
}

void Grillas::foto(const std::string& persona, const std::string& jpeg, std::int64_t ahora) {
    auto r = retrato(persona);
    if (!r || jpeg.size() < 4) return;
    {
        std::lock_guard<std::mutex> l(r->m);
        if (r->vivoMs != 0 && ahora - r->vivoMs < 3000) return; // tiene video en vivo: gana el video
    }
    const auto* datos = reinterpret_cast<const std::uint8_t*>(jpeg.data());
    int w = 0, h = 0;
    if (libyuv::MJPGSize(datos, jpeg.size(), &w, &h) != 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    if (libyuv::MJPGToARGB(datos, jpeg.size(), px.data(), w * 4, w, h, w, h) != 0) return;
    guardar(std::move(r), px.data(), w, h, ahora, false);
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

void Grillas::pintarRotulo(std::uint8_t* lienzo, int W, int H, const Rect& celda, const Rotulo& r, const EstiloRotulo& e) {
    const Caja c = caja(celda, W, H);
    if (c.w < 8 || c.h < 8 || r.w <= 0 || r.h <= 0 || r.a8.size() < static_cast<std::size_t>(r.w) * r.h) return;
    // La banda: `tamano` del alto de la celda (no menos de 10 px: debajo no se lee nada) y
    // el ancho de la celda; el texto adentro, con un margen, escalado para entrar.
    const int bandaH = std::clamp(static_cast<int>(std::lround(c.h * e.tamano)), std::min(10, c.h), c.h);
    const int margen = std::max(2, bandaH / 6);
    const Caja banda{c.x, e.arriba ? c.y : c.y + c.h - bandaH, c.w, bandaH};
    for (int y = banda.y; y < banda.y + banda.h; ++y) {
        std::uint8_t* p = lienzo + (static_cast<std::size_t>(y) * W + banda.x) * 4;
        for (int x = 0; x < banda.w; ++x) sobre(p + x * 4, e.fondo, 255);
    }
    const float esc = std::min(static_cast<float>(bandaH - 2 * margen) / r.h, static_cast<float>(c.w - 2 * margen) / r.w);
    if (esc <= 0) return;
    const int tw = std::max(1, static_cast<int>(r.w * esc)), th = std::max(1, static_cast<int>(r.h * esc));
    const int x0 = banda.x + margen, y0 = banda.y + (bandaH - th) / 2;
    // Bilineal sobre la máscara (chica: cientos de píxeles por rótulo).
    for (int y = 0; y < th; ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= H) continue;
        const float fy = (y + 0.5f) / esc - 0.5f;
        const int y1 = std::clamp(static_cast<int>(std::floor(fy)), 0, r.h - 1), y2 = std::min(y1 + 1, r.h - 1);
        const float wy = std::clamp(fy - static_cast<float>(y1), 0.0f, 1.0f);
        for (int x = 0; x < tw; ++x) {
            const int px = x0 + x;
            if (px < 0 || px >= W || px >= c.x + c.w) continue;
            const float fx = (x + 0.5f) / esc - 0.5f;
            const int x1 = std::clamp(static_cast<int>(std::floor(fx)), 0, r.w - 1), x2 = std::min(x1 + 1, r.w - 1);
            const float wx = std::clamp(fx - static_cast<float>(x1), 0.0f, 1.0f);
            const auto m = [&](int xx, int yy) { return static_cast<float>(r.a8[static_cast<std::size_t>(yy) * r.w + xx]); };
            const float v = (m(x1, y1) * (1 - wx) + m(x2, y1) * wx) * (1 - wy) + (m(x1, y2) * (1 - wx) + m(x2, y2) * wx) * wy;
            if (v >= 1.0f) sobre(lienzo + (static_cast<std::size_t>(py) * W + px) * 4, e.color, static_cast<unsigned>(v + 0.5f));
        }
    }
}

void Grillas::pintarLocked(Grilla& g, std::int64_t ahora, std::uint64_t ticQpc) {
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
    // Un cuadro guardado que llegó hace más de dos vueltas ya se lo perdió el pintor: si hay
    // uno más nuevo, se saltea (lo que espera no pasa de dos cuadros). Es lo que cuesta
    // ponerse al día después de una ráfaga (un cuadro clave que tarda en llegar y los de atrás
    // que llegan juntos): con la cámara y el pintor al mismo ritmo, el atraso no baja solo.
    // Medido con 10 invitados: con dos vueltas, ~41 ms de espera y 29 cps por cara; con tres,
    // 29,4 cps pero ~74 ms.
    const std::uint64_t periodoNs = periodoPintarNs_.load(std::memory_order_relaxed);
    const auto atrasoMs = static_cast<std::int64_t>(2 * periodoNs / 1'000'000ULL);

    // EL MISMO INSTANTE EN TODAS LAS CELDAS. Elegir en cada celda el último que llegó (o el
    // siguiente al que mostró) deja la sincronía en manos de cuánto tarda cada invitado en
    // llegar y decodificarse: parejo en la Mac, no con las colas de Media Foundation. Con la
    // instante de captura de cada cuadro (Retrato::instanteDe), cada tic muestra en todas el instante tic − espera, con la
    // espera del más lento (su Retrato::atrasoNs, + 3 ms de margen), suavizada (sube rápido, baja
    // despacio: cada cambio corre el instante de todas) y con tope en lo que guarda la cola.
    std::uint64_t objetivo = 0;
    std::int64_t esperaMaxMs = 0;
    if (ticQpc != 0) {
        double peor = -1;
        for (const Celda& c : g.celdas)
            if (const auto r = retrato(c.persona)) {
                std::lock_guard<std::mutex> l(r->m);
                if (r->atrasoQpc != 0 && ticQpc < r->atrasoQpc + 2'000'000'000ULL) peor = std::max(peor, r->atrasoNs);
            }
        if (peor < 0) {
            g.esperaNs = -1;
        } else {
            const double tope = static_cast<double>((kCola - 2) * periodoNs);
            const double quiere = std::clamp(peor + 3e6, 0.0, tope);
            g.esperaNs = g.esperaNs < 0 ? quiere : g.esperaNs + (quiere - g.esperaNs) / (quiere > g.esperaNs ? 4 : 64);
            objetivo = ticQpc - static_cast<std::uint64_t>(g.esperaNs);
            // Un siguiente que espera más que la espera común y dos cuadros: su hora no es creíble.
            esperaMaxMs = static_cast<std::int64_t>((g.esperaNs + 2.0 * static_cast<double>(periodoNs)) / 1e6);
        }
    }

    const auto dibujar = [&](const std::string& persona, const Anim& a) {
        const Rect r = animado(a, g.transicionMs, ahora);
        const Caja c = caja(r, W, H);
        if (c.w < 2 || c.h < 2) return;
        std::uint8_t* destino = g.lienzo.data() + (static_cast<std::size_t>(c.y) * W + c.x) * 4;
        bool pintada = false;
        if (const auto ret = retrato(persona)) {
            std::lock_guard<std::mutex> l(ret->m);
            const auto v = g.visto.find(persona);
            const Foto* f = ret->elegir(v != g.visto.end() ? v->second : 0, ahora, atrasoMs, objetivo, esperaMaxMs);
            BA_SONDA(if (f != nullptr && !a.sale) ret->trazaE[ret->nE++ % Retrato::kTraza] = {
                         static_cast<std::int64_t>(ticQpc / 1000), static_cast<std::int64_t>(objetivo / 1000),
                         static_cast<std::int64_t>(f->instante / 1000), f->seq, g.canal};)
            if (f != nullptr && !f->px.empty()) {
                // El ritmo de lo que se ve: un cuadro suyo nuevo en el mosaico (y los que se saltearon).
                if (!a.sale) {
                    auto& pin = g.pintado[persona];
                    if (!pin) pin = std::make_unique<Grilla::Pintado>();
                    const std::uint64_t antes = v != g.visto.end() ? v->second : 0;
                    if (f->seq != antes) {
                        pin->ritmo.cuadro(relojUs());
                        if (antes != 0 && f->seq > antes + 1) pin->salteados += f->seq - antes - 1;
                    } else {
                        ++pin->repetidos;
                    }
                }
                // Recorte que llena el lugar (sin barras).
                int sx = 0, sy = 0, sw = 0, sh = 0;
                recorte(f->w, f->h, c.w, c.h, sx, sy, sw, sh);
                escalar(f->px.data() + (static_cast<std::size_t>(sy) * f->w + sx) * 4, f->w * 4, sw, sh, destino, W * 4,
                        c.w, c.h);
                // De la baja a la HD: lo que se va, encima, cada vez más transparente.
                if (ret->fundido.activo(ahora) && !ret->fundido.imagen().empty()) {
                    const int aw = ret->fundido.ancho(), ah = ret->fundido.alto();
                    recorte(aw, ah, c.w, c.h, sx, sy, sw, sh);
                    tmpFundido_.resize(static_cast<std::size_t>(c.w) * c.h * 4);
                    escalar(ret->fundido.imagen().data() + (static_cast<std::size_t>(sy) * aw + sx) * 4, aw * 4, sw, sh,
                            tmpFundido_.data(), c.w * 4, c.w, c.h);
                    mezclarBgra(destino, W * 4, tmpFundido_.data(), c.w * 4, c.w, c.h, ret->fundido.alfa(ahora));
                }
                g.visto[persona] = f->seq;
                pintada = true;
            }
        }
        if (!pintada) silueta(g.lienzo.data(), W, c);
        if (!a.rotulo.empty())
            if (const auto rt = rotulos_.find(a.rotulo); rt != rotulos_.end() && rt->second)
                pintarRotulo(g.lienzo.data(), W, H,
                             {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.w), static_cast<float>(c.h)},
                             *rt->second, g.estiloRotulo);
        // El borde, con su fundido (aparece y se va en transicion_ms).
        const float alfa = alfaBorde(a.bordeDesde, a.resaltar, a.bordeT0, g.transicionMs, ahora);
        if (alfa >= 0.999f)
            borde(g.lienzo.data(), W, c, std::max(3, c.h / 30), g.resaltar);
        else if (alfa > 0.004f)
            bordeMezcla(g.lienzo.data(), W, c, std::max(3, c.h / 30), g.resaltar, static_cast<unsigned>(alfa * 255.0f + 0.5f));
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
    for (auto& [k, g] : grillas_) {
        for (const auto& [p, a] : g.anim) usados.insert(p);
        for (auto it = g.pintado.begin(); it != g.pintado.end();)
            it = g.anim.count(it->first) ? std::next(it) : g.pintado.erase(it);
    }
    std::lock_guard<std::mutex> l(retratosMu_);
    for (auto it = retratos_.begin(); it != retratos_.end();)
        it = usados.count(it->first) ? std::next(it) : retratos_.erase(it);
}

std::uint64_t Grillas::proximoPintado(std::uint64_t ahoraNs, std::uint64_t envioNs, std::uint64_t periodoNs,
                                      std::uint64_t propioNs, std::uint64_t anteriorNs) {
    constexpr std::uint64_t kSegundo = 1'000'000'000ULL;
    const bool conLienzo = envioNs != 0 && envioNs <= ahoraNs + kSegundo && ahoraNs < envioNs + kSegundo &&
                           periodoNs >= kSegundo / 61 && periodoNs <= kSegundo / 4;
    if (!conLienzo) return std::max(propioNs, ahoraNs);
    const auto p = static_cast<std::int64_t>(periodoNs);
    std::uint64_t t = 0;
    if (anteriorNs == 0 || ahoraNs > anteriorNs + kSegundo) {
        // Sin uno anterior: directo a la mitad de camino.
        t = envioNs + periodoNs / 2;
    } else {
        // Con uno anterior: un período después, corrido de a poco hacia la mitad de camino
        // (un lazo de fase): seguir cada temblor del lienzo (el programa sale más o menos
        // tarde según cuánto tarde en armarse) era pintar a intervalos desparejos y que el
        // lector de la app se perdiera algunos.
        t = anteriorNs + periodoNs;
        const auto ideal = static_cast<std::int64_t>(envioNs + periodoNs / 2);
        std::int64_t err = (ideal - static_cast<std::int64_t>(t)) % p; // a (-p/2, p/2]
        if (err > p / 2) err -= p;
        if (err <= -p / 2) err += p;
        const std::int64_t paso = std::clamp<std::int64_t>(err / 8, -p / 32, p / 32);
        t = static_cast<std::uint64_t>(static_cast<std::int64_t>(t) + paso);
    }
    if (t <= ahoraNs) t += ((ahoraNs - t) / periodoNs + 1) * periodoNs; // atrasado: sin ráfaga
    return t;
}

void Grillas::bucle() {
    using Reloj = std::chrono::steady_clock;
    const auto ns = [](Reloj::time_point t) {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
    };
    const std::uint64_t propio = 1'000'000'000ULL / static_cast<std::uint64_t>(cps_);
    std::uint64_t proximo = ns(Reloj::now());
    std::int64_t informeMs = ahoraMs();
    // El lienzo de la app: solo su cabecera (cuándo publicó y cada cuánto), sin leerle cuadros.
    ba::ipc::SpoutReceiver lienzo;
    bool conLienzo = false;
    std::uint64_t intentoNs = 0;
    // #7 del plan: los retratos al mosaico, una despertada por cuadro del lienzo.
    static ba::diag::Frontera& fMosaico = ba::diag::frontera("grilla.mosaico", 7, "retratos → mosaico → BA.Grilla (hilo grillas)");
    while (!parar_.load()) {
        const std::int64_t ahora = ahoraMs();
        // El tic para el que se pinta (al que se durmió), en el reloj de los sellos: el instante
        // que muestran las celdas sale de él y es la hora del contenido del mosaico.
        const std::uint64_t ticQpc = proximo + (ba::ipc::HealthBlock::nowQpc() - ns(Reloj::now()));
        fMosaico.despertada();
        {
            std::lock_guard<std::mutex> l(mu_);
            for (auto& [k, g] : grillas_) {
                // Se pinta si algo cambió: otra disposición, un viaje en curso, un cuadro
                // nuevo de alguien que se ve; si no, se repite cada medio segundo (el
                // motor pone «sin señal» a los 3 s sin cuadros).
                bool hace = g.sucia || ahora - g.enviadoMs >= 500;
                for (const auto& [p, a] : g.anim)
                    hace = hace || ahora - a.t0 < g.transicionMs || ahora - a.bordeT0 < g.transicionMs;
                if (!hace)
                    for (const Celda& c : g.celdas)
                        if (const auto r = retrato(c.persona)) {
                            std::lock_guard<std::mutex> lm(r->m);
                            const auto v = g.visto.find(c.persona);
                            if ((r->seq != 0 && (v == g.visto.end() || v->second != r->seq)) || r->fundido.activo(ahora)) {
                                hace = true;
                                break;
                            }
                        }
                if (!hace) continue;
                ba::diag::MedirPase medir(fMosaico, g.lienzo.size());
                pintarLocked(g, ahora, ticQpc);
                if (!g.sender || g.senderW != g.ancho || g.senderH != g.alto) {
                    // El canal tiene tamaño fijo: otro tamaño = canal nuevo (bavideoanillo
                    // lo vuelve a tomar y renegocia).
                    g.sender.reset();
                    auto s = std::make_unique<ba::ipc::SpoutSender>();
                    const std::wstring nombre = L"BA.Grilla." + std::to_wstring(k);
                    // 6 buffers, como los invitados: la entrada del motor los lee todos, en orden
                    // (receiveSiguiente), y ninguno se pisa antes de que pase su lector.
                    ba::ipc::SlotConfig cfg{g.ancho, g.alto};
                    cfg.buffers = kBuffersCanal;
                    if (s->publish(nombre.c_str(), cfg)) {
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
                    ba::ipc::FrameMeta meta{t, t};
                    // La hora de su contenido: la del tic para el que se pintó (parejo, aunque
                    // publicarlo tarde más o menos), no la de ahora (videocomun.cpp).
                    meta.contenidoQpc = ticQpc <= t ? ticQpc : t;
                    g.sender->send(g.lienzo.data(), g.lienzo.size(), meta);
                    BA_SONDA(g.ritmoPublicado->cuadro(relojUs());)
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
        const std::uint64_t ya = ns(Reloj::now());
        // El lienzo se abre (o se reabre: la app se reinició) a lo sumo una vez por segundo.
        if (!lienzo_.empty() && ya - intentoNs >= 1'000'000'000ULL &&
            (!conLienzo || ya > lienzo.ultimoEnvioNs() + 1'000'000'000ULL)) {
            intentoNs = ya;
            lienzo = ba::ipc::SpoutReceiver{};
            conLienzo = lienzo.subscribe(lienzo_.c_str());
        }
        const std::uint64_t periodoLienzo = conLienzo ? lienzo.periodoNs() : 0;
        proximo = proximoPintado(ya, conLienzo ? lienzo.ultimoEnvioNs() : 0, periodoLienzo, proximo + propio, proximo);
        periodoPintarNs_.store(periodoLienzo >= 1'000'000'000ULL / 61 && periodoLienzo <= 1'000'000'000ULL / 4 ? periodoLienzo
                                                                                                               : propio,
                               std::memory_order_relaxed);
        // En Windows sleep_until redondea al tic de 15,6 ms: la grilla salía a ~24 cps disparejos.
        ba::platform::dormirHasta(Reloj::time_point(std::chrono::duration_cast<Reloj::duration>(std::chrono::nanoseconds(proximo))));
    }
}

} // namespace ba::gateway
