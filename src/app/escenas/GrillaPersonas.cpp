// ============================================================================
// src/app/escenas/GrillaPersonas.cpp — ver GrillaPersonas.hpp
// ============================================================================

#include "escenas/GrillaPersonas.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

namespace ba::app::grilla {

namespace {

/// Un número de los ajustes (entero o con coma, como lo deje la ventana o el MCP).
int entero(const json& a, const char* k, int def) {
    const auto it = a.find(k);
    if (it == a.end()) return def;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number()) return static_cast<int>(std::lround(it->get<double>()));
    return def;
}

std::string texto(const json& a, const char* k, const std::string& def) {
    const auto it = a.find(k);
    return it != a.end() && it->is_string() ? it->get<std::string>() : def;
}

bool siNo(const json& a, const char* k, bool def) {
    const auto it = a.find(k);
    return it != a.end() && it->is_boolean() ? it->get<bool>() : def;
}

/// Colores: el entero de OBS que guarda la ventana Propiedades o un "#AARRGGBB".
std::string color(const json& a, const char* k, const std::string& def) {
    const auto it = a.find(k);
    if (it == a.end()) return def;
    if (it->is_number()) return colorDeObs(static_cast<std::uint32_t>(it->get<double>()));
    if (it->is_string()) return it->get<std::string>();
    return def;
}

double aspectoDe(const std::string& p) {
    if (p == "llenar") return 0;
    const auto dos = p.find(':');
    if (dos == std::string::npos) return 16.0 / 9.0;
    const double a = std::atof(p.substr(0, dos).c_str()), b = std::atof(p.substr(dos + 1).c_str());
    return a > 0 && b > 0 ? a / b : 16.0 / 9.0;
}

const char* kProporciones[] = {"16:9", "4:3", "1:1", "3:4", "9:16", "llenar"};

} // namespace

std::string colorDeObs(std::uint32_t c) {
    const unsigned a = (c >> 24) & 0xFF, b = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, r = c & 0xFF;
    char out[16];
    std::snprintf(out, sizeof out, "#%02X%02X%02X%02X", a, r, g, b);
    return out;
}

// ---------------------------------------------------------------------------
// Ajustes
// ---------------------------------------------------------------------------

json Config::defectos() {
    return json{{"canal", 0},
                {"origen", "ambas"},
                {"grupo", ""},
                {"orden_en_grupo", 1},
                {"reparto", "en_orden"},
                {"tope", 16},
                {"orden", "llegada"},
                {"solo_con_camara", false},
                {"sin_al_aire", true},
                {"resaltar", true},
                {"ancho", 1280},
                {"alto", 720},
                {"separacion", 8},
                {"margen", 0},
                {"proporcion", "16:9"},
                {"transicion_ms", 400},
                {"fondo", 0},                   // transparente (entero de OBS 0xAABBGGRR)
                {"color_resaltar", 0xFF07C1FFu}, // ámbar #FFC107
                // Cuando alguien habla (ver el .hpp).
                {"al_hablar", "resaltar"},
                {"disparo", "voz"},
                {"voz_ms", 1500},
                {"minimo_ms", 4000},
                {"precarga", true},
                {"destacar_lugar", "centro"},
                {"destacar_tamano", 70},
                {"franja", "abajo"},
                {"superponer_tamano", 60},
                {"superponer_completa", false},
                {"fundido_ms", 300},
                {"alta_hasta", kAltaHastaDefecto}, // más personas: todas las miniaturas en baja (0 = sin límite)
                {"alta_max", kAltaMaxDefecto},     // nunca más que esto en alta a la vez (por prioridad)
                {"agrandar", "no"},             // resaltar y escena: no | encima | achicando
                {"pasar_a_escena", "a_mano"},   // escena: a_mano | automatico | asistente
                {"salida_orador", ""},          // escena: la salida ("" = la principal)
                {"escena_orador", ""},          // escena: el nombre de la escena del orador
                // Nombres en cada celda.
                {"nombres", "no"},
                {"nombre_tamano", 14},
                {"nombre_color", 0xFFFFFFFFu}, // blanco
                {"nombre_fondo", 0xA0000000u}, // negro al 63 %
                {"nombre_lugar", "abajo"},
                // Qué muestra: todos, o solo el orador de otra grilla (la escena del orador).
                {"muestra", "todos"},
                {"sigue", ""}};
}

Config Config::desdeAjustes(const std::string& fuente, const json& ajustesIn) {
    const json a = ajustesIn.is_object() ? ajustesIn : json::object();
    Config c;
    c.fuente = fuente;
    c.canal = std::clamp(entero(a, "canal", 0), 0, kMaxGrillas);
    const std::string o = texto(a, "origen", "ambas");
    c.origen = o == "sala" ? Origen::Sala : o == "asamblea" ? Origen::Asamblea : Origen::Ambas;
    c.grupo = texto(a, "grupo", "");
    c.ordenEnGrupo = entero(a, "orden_en_grupo", 1);
    c.reparto = texto(a, "reparto", "en_orden") == "parejo" ? Reparto::Parejo : Reparto::EnOrden;
    c.tope = std::clamp(entero(a, "tope", 16), 0, kMaxCeldas);
    const std::string ord = texto(a, "orden", "llegada");
    c.orden = ord == "habla" ? Orden::Habla : ord == "prioridad" ? Orden::Prioridad : Orden::Llegada;
    c.soloConCamara = siNo(a, "solo_con_camara", false);
    c.sinAlAire = siNo(a, "sin_al_aire", true);
    c.resaltar = siNo(a, "resaltar", true);
    c.ancho = std::clamp(entero(a, "ancho", 1280), 64, 3840);
    c.alto = std::clamp(entero(a, "alto", 720), 64, 2160);
    c.separacion = std::clamp(entero(a, "separacion", 8), 0, 200);
    c.margen = std::clamp(entero(a, "margen", 0), 0, 500);
    c.aspecto = aspectoDe(texto(a, "proporcion", "16:9"));
    c.transicionMs = std::clamp(entero(a, "transicion_ms", 400), 0, 3000);
    c.fondo = color(a, "fondo", "#00000000");
    c.colorResaltar = color(a, "color_resaltar", "#FFFFC107");
    const std::string ah = texto(a, "al_hablar", "resaltar");
    c.alHablar = ah == "nada"          ? AlHablar::Nada
                 : ah == "sin_cambios" ? AlHablar::SinCambios
                 : ah == "destacar"    ? AlHablar::Destacar
                 : ah == "superponer" ? AlHablar::Superponer
                 : ah == "escena"     ? AlHablar::Escena
                                      : AlHablar::Resaltar;
    const std::string di = texto(a, "disparo", "voz");
    c.disparo = di == "palabra" ? Disparo::Palabra : di == "orador" ? Disparo::Orador : di == "manual" ? Disparo::Manual : Disparo::Voz;
    c.vozMs = std::clamp(entero(a, "voz_ms", 1500), 0, 10000);
    c.minimoMs = std::clamp(entero(a, "minimo_ms", 4000), 0, 60000);
    c.precarga = siNo(a, "precarga", true);
    c.destacarIzquierda = texto(a, "destacar_lugar", "centro") == "izquierda";
    c.destacarTamano = std::clamp(entero(a, "destacar_tamano", 70), 40, 90);
    c.franjaCostado = texto(a, "franja", "abajo") == "costado";
    c.superponerTamano = std::clamp(entero(a, "superponer_tamano", 60), 30, 90);
    c.superponerCompleta = siNo(a, "superponer_completa", false);
    c.fundidoMs = std::clamp(entero(a, "fundido_ms", 300), 0, 2000);
    c.altaHasta = std::clamp(entero(a, "alta_hasta", kAltaHastaDefecto), 0, kMaxCeldas);
    c.altaMax = std::clamp(entero(a, "alta_max", kAltaMaxDefecto), 1, kMaxCeldas);
    const std::string ag = texto(a, "agrandar", "no");
    c.agrandar = ag == "encima" ? Agrandar::Encima : ag == "achicando" ? Agrandar::Achicando : Agrandar::No;
    const std::string pe = texto(a, "pasar_a_escena", "a_mano");
    c.pasarEscena = pe == "automatico" ? PasarEscena::Automatico : pe == "asistente" ? PasarEscena::Asistente : PasarEscena::AMano;
    c.salidaOrador = texto(a, "salida_orador", "");
    c.escenaOrador = texto(a, "escena_orador", "");
    const std::string no = texto(a, "nombres", "no");
    c.nombres = no == "nombre" ? Nombres::Nombre : no == "nombre_cargo" ? Nombres::NombreCargo : Nombres::No;
    c.nombreTamano = std::clamp(entero(a, "nombre_tamano", 14), 5, 40);
    c.nombreColor = color(a, "nombre_color", "#FFFFFFFF");
    c.nombreFondo = color(a, "nombre_fondo", "#A0000000");
    c.nombreArriba = texto(a, "nombre_lugar", "abajo") == "arriba";
    c.soloOrador = texto(a, "muestra", "todos") == "orador";
    c.sigue = texto(a, "sigue", "");
    return c;
}

std::string Config::validar(const json& cambios) {
    if (!cambios.is_object()) return ba::i18n::traducir("escenas", "los ajustes van en un objeto");
    const auto enumera = [&](const char* k, std::initializer_list<const char*> ok) -> std::string {
        if (!cambios.contains(k)) return {};
        const json& v = cambios[k];
        if (v.is_string())
            for (const char* x : ok)
                if (v.get<std::string>() == x) return {};
        std::string lista;
        for (const char* x : ok) lista += std::string(lista.empty() ? "" : ", ") + x;
        return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» es uno de: %2"), {k, lista});
    };
    const auto numero = [&](const char* k, int mn, int mx) -> std::string {
        if (!cambios.contains(k)) return {};
        const json& v = cambios[k];
        if (!v.is_number() || v.get<double>() < mn || v.get<double>() > mx)
            return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» va de %2 a %3"), {k, std::to_string(mn), std::to_string(mx)});
        return {};
    };
    const auto booleano = [&](const char* k) -> std::string {
        if (cambios.contains(k) && !cambios[k].is_boolean())
            return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» es true o false"), {k});
        return {};
    };
    static const std::set<std::string> kConocidos = {
        "canal",      "origen", "grupo",      "orden_en_grupo", "reparto",       "tope",  "orden",
        "solo_con_camara", "sin_al_aire", "resaltar", "ancho", "alto", "separacion", "margen",
        "proporcion", "transicion_ms", "fondo", "color_resaltar",
        "al_hablar", "disparo", "voz_ms", "minimo_ms", "precarga", "destacar_lugar", "destacar_tamano", "franja",
        "superponer_tamano", "superponer_completa", "fundido_ms", "nombres", "nombre_tamano", "nombre_color",
        "nombre_fondo", "nombre_lugar", "muestra", "sigue", "agrandar", "pasar_a_escena", "salida_orador", "escena_orador",
        "alta_hasta", "alta_max"};
    for (const auto& [k, v] : cambios.items())
        if (!kConocidos.count(k)) return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» no es un ajuste de la grilla de personas"), {k});
    for (const std::string& e :
         {enumera("origen", {"sala", "asamblea", "ambas"}), enumera("reparto", {"en_orden", "parejo"}),
          enumera("orden", {"llegada", "habla", "prioridad"}),
          enumera("proporcion", {kProporciones[0], kProporciones[1], kProporciones[2], kProporciones[3], kProporciones[4],
                                 kProporciones[5]}),
          numero("canal", 0, kMaxGrillas), numero("orden_en_grupo", -1000, 1000), numero("tope", 0, kMaxCeldas),
          numero("ancho", 64, 3840), numero("alto", 64, 2160), numero("separacion", 0, 200), numero("margen", 0, 500),
          numero("transicion_ms", 0, 3000), booleano("solo_con_camara"), booleano("sin_al_aire"), booleano("resaltar"),
          enumera("al_hablar", {"nada", "sin_cambios", "resaltar", "destacar", "superponer", "escena"}),
          enumera("agrandar", {"no", "encima", "achicando"}), enumera("pasar_a_escena", {"a_mano", "automatico", "asistente"}),
          enumera("disparo", {"voz", "palabra", "orador", "manual"}), numero("voz_ms", 0, 10000),
          numero("minimo_ms", 0, 60000), booleano("precarga"), enumera("destacar_lugar", {"centro", "izquierda"}),
          numero("destacar_tamano", 40, 90), enumera("franja", {"abajo", "costado"}), numero("superponer_tamano", 30, 90),
          booleano("superponer_completa"), numero("fundido_ms", 0, 2000), numero("alta_hasta", 0, kMaxCeldas),
          numero("alta_max", 1, kMaxCeldas), enumera("nombres", {"no", "nombre", "nombre_cargo"}),
          numero("nombre_tamano", 5, 40), enumera("nombre_lugar", {"abajo", "arriba"}), enumera("muestra", {"todos", "orador"})})
        if (!e.empty()) return e;
    if (cambios.contains("grupo") && !cambios["grupo"].is_string())
        return ba::i18n::traducir("escenas", "«grupo» es un texto (vacío = sola)");
    for (const char* k : {"salida_orador", "escena_orador"})
        if (cambios.contains(k) && !cambios[k].is_string())
            return ba::i18n::llenar(ba::i18n::traducir("escenas", "«%1» es un texto"), {k});
    if (cambios.contains("sigue") && !cambios["sigue"].is_string())
        return ba::i18n::traducir("escenas", "«sigue» es el nombre de otra grilla (vacío = la primera con orador)");
    return {};
}

// ---------------------------------------------------------------------------
// Filtros y orden
// ---------------------------------------------------------------------------

bool acepta(const Config& c, const Persona& p) {
    if (c.origen == Origen::Sala && !p.deSala) return false;
    if (c.origen == Origen::Asamblea && p.deSala) return false;
    if (c.soloConCamara && !p.conCamara) return false;
    if (c.sinAlAire && p.alAire) return false;
    return true;
}

bool antes(Orden o, const Persona& a, const Persona& b) {
    if (o == Orden::Habla) {
        // El que habla ahora, después el que habló hace menos; los que nunca hablaron, por llegada.
        if (a.hablando != b.hablando) return a.hablando;
        if (a.vozMs != b.vozMs) return a.vozMs > b.vozMs;
    } else if (o == Orden::Prioridad) {
        if (a.prioridad != b.prioridad) return a.prioridad > b.prioridad;
    }
    if (a.llegadaMs != b.llegadaMs) return a.llegadaMs < b.llegadaMs;
    return a.clave < b.clave;
}

// ---------------------------------------------------------------------------
// Disposición
// ---------------------------------------------------------------------------

std::vector<Rect> disponer(int n, int ancho, int alto, int sep, int margen, double aspecto, int* colsOut) {
    std::vector<Rect> out;
    if (colsOut) *colsOut = 0;
    if (n <= 0) return out;
    const int aw = ancho - 2 * margen, ah = alto - 2 * margen;
    if (aw <= 0 || ah <= 0) return out;
    int mejorCols = 1;
    double mejorW = 0, mejorH = 0, mejorArea = -1;
    for (int cols = 1; cols <= n; ++cols) {
        const int rows = (n + cols - 1) / cols;
        const double cw = static_cast<double>(aw - (cols - 1) * sep) / cols;
        const double ch = static_cast<double>(ah - (rows - 1) * sep) / rows;
        if (cw < 1 || ch < 1) continue;
        double w = cw, h = ch;
        if (aspecto > 0) {
            w = std::min(cw, ch * aspecto);
            h = w / aspecto;
        }
        // A igualdad de tamaño gana la de menos columnas (la primera que lo logra).
        const double area = w * h;
        if (area > mejorArea + 0.5) {
            mejorArea = area;
            mejorCols = cols;
            mejorW = w;
            mejorH = h;
        }
    }
    if (mejorArea < 0) return out;
    const int cols = mejorCols, rows = (n + cols - 1) / cols;
    const int w = static_cast<int>(std::floor(mejorW)), h = static_cast<int>(std::floor(mejorH));
    const int bloqueH = rows * h + (rows - 1) * sep;
    const int y0 = margen + (ah - bloqueH) / 2;
    for (int i = 0; i < n; ++i) {
        const int fila = i / cols, col = i % cols;
        const int enFila = fila == rows - 1 ? n - fila * cols : cols;
        const int filaW = enFila * w + (enFila - 1) * sep;
        const int x0 = margen + (aw - filaW) / 2;
        out.push_back({x0 + col * (w + sep), y0 + fila * (h + sep), w, h});
    }
    if (colsOut) *colsOut = cols;
    return out;
}

// ---------------------------------------------------------------------------
// A mano y lo que se ve
// ---------------------------------------------------------------------------

bool Manual::fijo(const std::string& fuente, const std::string& clave) const {
    const auto it = fijos.find(clave);
    return it != fijos.end() && it->second == fuente;
}

void Manual::fijar(const std::string& fuente, const std::string& clave) {
    fijos[clave] = fuente;
    fuera.erase({fuente, clave});
}

void Manual::sacar(const std::string& fuente, const std::string& clave) {
    if (fijo(fuente, clave)) fijos.erase(clave);
    fuera.insert({fuente, clave});
}

void Manual::devolver(const std::string& fuente, const std::string& clave) {
    if (fijo(fuente, clave)) fijos.erase(clave);
    fuera.erase({fuente, clave});
}

void Manual::olvidarFuentesSalvo(const std::set<std::string>& existen) {
    for (auto it = fijos.begin(); it != fijos.end();)
        it = existen.count(it->second) ? std::next(it) : fijos.erase(it);
    for (auto it = fuera.begin(); it != fuera.end();)
        it = existen.count(it->first) ? std::next(it) : fuera.erase(it);
    for (auto it = destacados.begin(); it != destacados.end();)
        it = existen.count(it->first) ? std::next(it) : destacados.erase(it);
}

std::string Manual::destacado(const std::string& fuente) const {
    const auto it = destacados.find(fuente);
    return it != destacados.end() ? it->second : std::string();
}

// ---------------------------------------------------------------------------
// El orador
// ---------------------------------------------------------------------------

const std::string& MaquinaOrador::paso(const Config& c, const std::vector<const Persona*>& gente, const std::string& manual,
                                       std::int64_t ahora) {
    const auto esta = [&](const std::string& k) {
        return !k.empty() && std::any_of(gente.begin(), gente.end(), [&](const Persona* p) { return p->clave == k; });
    };
    const auto poner = [&](const std::string& k) {
        if (k == actual_) return;
        actual_ = k;
        desde_ = ahora;
    };
    // A mano gana siempre, y en el acto: el operador sabe lo que hace.
    if (esta(manual)) {
        poner(manual);
        return actual_;
    }
    // Quién quiere el disparo. Si el actual sigue cumpliendo, se queda (nadie le gana por
    // llegar después).
    std::string quiere;
    const auto cumple = [&](const Persona& p) {
        switch (c.disparo) {
            case Disparo::Palabra: return p.palabra;
            case Disparo::Orador: return p.orador;
            case Disparo::Voz: return p.hablando && p.hablaDesdeMs > 0 && ahora - p.hablaDesdeMs >= c.vozMs;
            case Disparo::Manual: return false;
        }
        return false;
    };
    for (const Persona* p : gente)
        if (p->clave == actual_ && cumple(*p)) quiere = actual_;
    if (quiere.empty()) {
        const Persona* mejor = nullptr;
        for (const Persona* p : gente) {
            if (!cumple(*p)) continue;
            // Con voz, el que lleva más tiempo hablando; si no, el primero (el orden de la gente).
            if (mejor == nullptr || (c.disparo == Disparo::Voz && p->hablaDesdeMs < mejor->hablaDesdeMs)) mejor = p;
        }
        if (mejor != nullptr) quiere = mejor->clave;
    }
    // Con voz, cuando nadie habla se queda el último que habló (no se vuelve a la grilla
    // con cada silencio); con la palabra o el orador, se va (ya no le toca).
    if (quiere.empty() && c.disparo == Disparo::Voz && esta(actual_)) quiere = actual_;
    if (quiere == actual_) return actual_;
    // Histéresis: el que está no se va antes de su tiempo mínimo, salvo que ya no esté.
    if (actual_.empty() || !esta(actual_) || ahora - desde_ >= c.minimoMs) poner(quiere);
    return actual_;
}

std::string MaquinaOrador::siguiente(const Config& c, const std::vector<const Persona*>& gente, const std::string& actual,
                                     std::int64_t ahora) {
    if (!c.precarga) return {};
    for (const Persona* p : gente) {
        if (p->clave == actual) continue;
        switch (c.disparo) {
            case Disparo::Palabra:
            case Disparo::Orador:
                if (p->siguiente) return p->clave;
                break;
            case Disparo::Voz:
                // Empezó a hablar y todavía no llegó: que su alta vaya llegando.
                if (p->hablando && p->hablaDesdeMs > 0 && ahora - p->hablaDesdeMs < c.vozMs) return p->clave;
                break;
            case Disparo::Manual: break;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Disposiciones con un lugar grande
// ---------------------------------------------------------------------------

Rect achicar(const Rect& r, double f) {
    const int w = static_cast<int>(std::lround(r.w * f)), h = static_cast<int>(std::lround(r.h * f));
    return {r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
}

namespace {

/// El rectángulo más grande con la proporción `aspecto` (0 = el que hay) adentro de `caja`,
/// centrado (o pegado a la izquierda).
Rect encajar(const Rect& caja, double aspecto, bool izquierda = false) {
    if (caja.w <= 0 || caja.h <= 0) return {};
    int w = caja.w, h = caja.h;
    if (aspecto > 0) {
        w = std::min(caja.w, static_cast<int>(std::floor(caja.h * aspecto)));
        h = static_cast<int>(std::floor(w / aspecto));
    }
    return {izquierda ? caja.x : caja.x + (caja.w - w) / 2, caja.y + (caja.h - h) / 2, w, h};
}

/// `n` celdas en `caja` con disponer; true si cada una tiene al menos kMinCelda de alto.
bool enCaja(int n, const Rect& caja, const Config& c, std::vector<Rect>& out) {
    out = disponer(n, caja.w, caja.h, c.separacion, 0, c.aspecto);
    for (Rect& r : out) {
        r.x += caja.x;
        r.y += caja.y;
    }
    return static_cast<int>(out.size()) == n && (n == 0 || out.front().h >= std::min(kMinCelda, caja.h));
}

/// Cuántos de `n` entran en las cajas (repartidos en orden: la primera se llena antes),
/// con «+N» en la última si no entran todos.
Disposicion enFranjas(int n, const std::vector<Rect>& cajas, const Config& c, Rect grande) {
    Disposicion d;
    d.grande = grande;
    if (n <= 0 || cajas.empty()) return d;
    const int k = static_cast<int>(cajas.size());
    // Reparte `m` celdas entre las cajas: la primera lleva el redondeo hacia arriba.
    const auto repartir = [&](int m, std::vector<Rect>& out) {
        out.clear();
        int resto = m;
        for (int i = 0; i < k; ++i) {
            const int aca = (resto + (k - i) - 1) / (k - i);
            std::vector<Rect> r;
            if (!enCaja(aca, cajas[static_cast<std::size_t>(i)], c, r)) return false;
            out.insert(out.end(), r.begin(), r.end());
            resto -= aca;
        }
        return true;
    };
    std::vector<Rect> todo;
    if (repartir(n, todo)) {
        d.resto = std::move(todo);
        return d;
    }
    // No entran: la mayor cantidad m (con su «+N») que sí.
    for (int m = n; m >= 1; --m)
        if (repartir(m, todo)) {
            d.hayMas = true;
            d.rMas = todo.back();
            todo.pop_back();
            d.resto = std::move(todo);
            return d;
        }
    return d; // ni uno: solo el grande
}

} // namespace

Disposicion disponerDestacar(int n, const Config& c) {
    const Rect lienzo{c.margen, c.margen, c.ancho - 2 * c.margen, c.alto - 2 * c.margen};
    if (n <= 0) return {encajar(lienzo, c.aspecto, c.destacarIzquierda), {}, false, {}};
    const double t = c.destacarTamano / 100.0;
    const int sep = c.separacion;
    if (!c.franjaCostado) {
        // El grande arriba, la franja abajo (a lo ancho).
        const int altoGrande = static_cast<int>(lienzo.h * t);
        const Rect arriba{lienzo.x, lienzo.y, lienzo.w, altoGrande};
        const Rect franja{lienzo.x, lienzo.y + altoGrande + sep, lienzo.w, lienzo.h - altoGrande - sep};
        return enFranjas(n, {franja}, c, encajar(arriba, c.aspecto, c.destacarIzquierda));
    }
    const int anchoGrande = static_cast<int>(lienzo.w * t);
    if (c.destacarIzquierda) {
        // El grande a la izquierda, la franja a la derecha.
        const Rect izq{lienzo.x, lienzo.y, anchoGrande, lienzo.h};
        const Rect franja{lienzo.x + anchoGrande + sep, lienzo.y, lienzo.w - anchoGrande - sep, lienzo.h};
        return enFranjas(n, {franja}, c, encajar(izq, c.aspecto, true));
    }
    // El grande al centro y una franja a cada lado: la de la izquierda se llena primero.
    const int lado = (lienzo.w - anchoGrande - 2 * sep) / 2;
    const Rect centro{lienzo.x + lado + sep, lienzo.y, anchoGrande, lienzo.h};
    const Rect a{lienzo.x, lienzo.y, lado, lienzo.h}, b{lienzo.x + lado + 2 * sep + anchoGrande, lienzo.y, lado, lienzo.h};
    return enFranjas(n, {a, b}, c, encajar(centro, c.aspecto));
}

Disposicion disponerSuperponer(int n, const Config& c) {
    const Rect lienzo{c.margen, c.margen, c.ancho - 2 * c.margen, c.alto - 2 * c.margen};
    Disposicion d;
    if (n <= 0 || c.superponerCompleta) {
        // A pantalla completa (o sin nadie más): el orador ocupa todo; los demás no se ven.
        d.grande = c.superponerCompleta ? lienzo : encajar(lienzo, c.aspecto);
        return d;
    }
    // Una grilla de cols x filas con un bloque de celdas libre en el medio (el hueco): los
    // demás van en las celdas de alrededor, en orden de lectura, y nadie queda tapado. Se
    // elige la que deja las celdas más grandes.
    const double t = c.superponerTamano / 100.0;
    const int sep = c.separacion;
    double mejorArea = -1;
    int mc = 0, mf = 0, mhc = 0, mhf = 0, mw = 0, mh = 0;
    for (int cols = 3; cols <= 16; ++cols)
        for (int filas = 3; filas <= 16; ++filas) {
            int hc = std::max(1, static_cast<int>(std::lround(cols * t))), hf = std::max(1, static_cast<int>(std::lround(filas * t)));
            if ((cols - hc) % 2 != 0) ++hc; // centrado: lo mismo a cada lado
            if ((filas - hf) % 2 != 0) ++hf;
            if (hc >= cols || hf >= filas) continue;
            const int libres = cols * filas - hc * hf;
            if (libres < n || libres > n + cols + filas) continue; // de sobra: otra más justa deja celdas más grandes
            const double cw = static_cast<double>(lienzo.w - (cols - 1) * sep) / cols;
            const double ch = static_cast<double>(lienzo.h - (filas - 1) * sep) / filas;
            if (cw < 1 || ch < 1) continue;
            double w = cw, h = ch;
            if (c.aspecto > 0) {
                w = std::min(cw, ch * c.aspecto);
                h = w / c.aspecto;
            }
            if (h < kMinCelda) continue;
            if (w * h > mejorArea + 0.5) {
                mejorArea = w * h;
                mc = cols;
                mf = filas;
                mhc = hc;
                mhf = hf;
                mw = static_cast<int>(std::floor(w));
                mh = static_cast<int>(std::floor(h));
            }
        }
    if (mejorArea < 0) {
        // No entran alrededor de un hueco: como destacar con la franja abajo.
        Config f = c;
        f.franjaCostado = false;
        f.destacarIzquierda = false;
        f.destacarTamano = std::clamp(c.superponerTamano, 40, 90);
        return disponerDestacar(n, f);
    }
    const int bloqueW = mc * mw + (mc - 1) * sep, bloqueH = mf * mh + (mf - 1) * sep;
    const int x0 = lienzo.x + (lienzo.w - bloqueW) / 2, y0 = lienzo.y + (lienzo.h - bloqueH) / 2;
    const int c0 = (mc - mhc) / 2, f0 = (mf - mhf) / 2;
    for (int f = 0; f < mf && static_cast<int>(d.resto.size()) < n; ++f)
        for (int k = 0; k < mc && static_cast<int>(d.resto.size()) < n; ++k) {
            if (k >= c0 && k < c0 + mhc && f >= f0 && f < f0 + mhf) continue; // el hueco
            d.resto.push_back({x0 + k * (mw + sep), y0 + f * (mh + sep), mw, mh});
        }
    const Rect hueco{x0 + c0 * (mw + sep), y0 + f0 * (mh + sep), mhc * mw + (mhc - 1) * sep, mhf * mh + (mhf - 1) * sep};
    d.grande = encajar(hueco, c.aspecto);
    return d;
}

// ---------------------------------------------------------------------------
// Capas y rótulos
// ---------------------------------------------------------------------------

PaseEscenaOrador::Orden PaseEscenaOrador::paso(PasarEscena modo, const std::string& orador, const std::string& escenaOrador,
                                                const std::string& alAire) {
    Orden o;
    if (orador == orador_) return o;
    orador_ = orador;
    if (modo == PasarEscena::AMano || escenaOrador.empty()) {
        volverA_.clear();
        return o;
    }
    const Orden::Tipo t = modo == PasarEscena::Asistente ? Orden::Proponer : Orden::Poner;
    if (!orador.empty()) {
        // Un orador (nuevo u otro): a su escena. Si ya está al aire, su grilla lo sigue sola.
        if (alAire == escenaOrador) return o;
        volverA_ = alAire;
        o.tipo = t;
        o.escena = escenaOrador;
        o.orador = orador;
        return o;
    }
    // Ya no hay orador: de vuelta a lo que estaba (si seguimos en la escena del orador).
    if (alAire == escenaOrador && !volverA_.empty() && volverA_ != escenaOrador) {
        o.tipo = t;
        o.escena = volverA_;
        o.volver = true;
    }
    volverA_.clear();
    return o;
}

bool todasEnBaja(int personas, int altaHasta, bool antes) {
    if (altaHasta <= 0) return false;
    return personas > (antes ? altaHasta - kMargenAltaHasta : altaHasta);
}

std::map<std::string, bool> CapasGrilla::decidir(const std::map<std::string, Pedido>& pedidos, int maxSubidas,
                                                 int altaMax) {
    std::map<std::string, bool> out;
    std::map<std::string, Por> alta;
    // El orden de prioridad (para el tope): orador, presentador, precargado, fijado, el resto.
    // En cada grupo, el que ya estaba en alta primero (sin parpadeo en el borde del tope) y
    // después el más grande (si hay que esperar para subir, espera el más chico).
    const auto grupo = [](const Pedido& p) { return p.orador ? 0 : p.presentador ? 1 : p.precarga ? 2 : p.fijo ? 3 : 4; };
    std::vector<std::pair<std::string, Pedido>> orden(pedidos.begin(), pedidos.end());
    std::stable_sort(orden.begin(), orden.end(), [&](const auto& a, const auto& b) {
        const int ga = grupo(a.second), gb = grupo(b.second);
        if (ga != gb) return ga < gb;
        const bool ya = alta_.count(a.first) > 0, yb = alta_.count(b.first) > 0;
        if (ya != yb) return ya;
        return a.second.alto > b.second.alto;
    });
    int subidas = 0;
    const auto hayLugar = [&] { return altaMax <= 0 || static_cast<int>(alta.size()) < altaMax; };
    for (const auto& [k, p] : orden) {
        const auto it = alta_.find(k);
        if (!hayLugar()) {
            // El tope: el resto en baja, sea cual sea su razón.
        } else if (p.orador || p.presentador || p.precarga) {
            alta[k] = Por::Orador; // el orador, el presentador y el precargado: alta ya (sin esperar subidas)
        } else if (it != alta_.end() && it->second == Por::Tamano) {
            if (p.alto >= kBajarAlta) alta[k] = Por::Tamano; // ya en alta por su lugar: baja recién por debajo del umbral de bajada
        } else if (it != alta_.end()) {
            // Estaba en alta por ser orador y ya no lo es: la regla por tamaño, sin histéresis
            // (la alta no la ganó por su lugar). Si su lugar la pide, sigue sin cuadro clave nuevo.
            if (p.alto > kSubirAlta) alta[k] = Por::Tamano;
        } else if (p.alto > kSubirAlta && subidas < maxSubidas) {
            alta[k] = Por::Tamano;
            ++subidas;
        }
        out[k] = alta.count(k) > 0;
    }
    alta_ = std::move(alta);
    return out;
}

std::string idRotulo(Nombres n, const std::string& nombre, const std::string& cargo) {
    if (n == Nombres::No || nombre.empty()) return {};
    // FNV-1a de 64 bits sobre el texto (y la versión del dibujo, por si cambia la letra).
    const std::string texto = "r1\x1f" + nombre + "\x1f" + (n == Nombres::NombreCargo ? cargo : std::string());
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : texto) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    char out[24];
    std::snprintf(out, sizeof out, "r%016llx", static_cast<unsigned long long>(h));
    return out;
}

std::vector<std::string> fuentesQueSeVen(
    const std::vector<std::string>& raices,
    const std::function<std::optional<std::vector<std::string>>(const std::string&)>& itemsDe) {
    std::vector<std::string> out;
    std::set<std::string> vistas, escenas;
    const std::function<void(const std::string&)> mirar = [&](const std::string& n) {
        if (const auto hijos = itemsDe(n)) {
            if (!escenas.insert(n).second) return; // ya recorrida (o se contiene a sí misma)
            for (const std::string& h : *hijos) mirar(h);
            return;
        }
        if (vistas.insert(n).second) out.push_back(n);
    };
    for (const std::string& r : raices) mirar(r);
    return out;
}

// ---------------------------------------------------------------------------
// Reparto
// ---------------------------------------------------------------------------

std::vector<Resultado> Repartidor::calcular(const std::vector<Config>& grillas, const std::vector<Persona>& gente,
                                            const Manual& manual, std::int64_t ahora) {
    std::map<std::string, const Persona*> porClave;
    for (const Persona& p : gente) porClave[p.clave] = &p;

    // Los grupos: las grillas con el mismo «grupo»; sin grupo, cada una sola. Las que
    // muestran solo al orador de otra no reparten: van al final.
    std::map<std::string, std::vector<const Config*>> grupos;
    for (const Config& c : grillas)
        if (!c.soloOrador) grupos[c.grupo.empty() ? "\x01" + c.fuente : "\x02" + c.grupo].push_back(&c);
    // Los que tienen una grilla al aire, primero (ver «GRILLAS AL AIRE PRIMERO» en el .hpp);
    // entre ellos, el orden del mapa (estable de una vuelta a la otra).
    std::vector<std::vector<const Config*>*> orden;
    for (auto& [clave, gs] : grupos) orden.push_back(&gs);
    const auto conAlAire = [](const std::vector<const Config*>* gs) {
        return std::any_of(gs->begin(), gs->end(), [](const Config* c) { return c->alAire; });
    };
    std::stable_sort(orden.begin(), orden.end(), [&](const auto* a, const auto* b) { return conAlAire(a) && !conAlAire(b); });
    std::set<std::string> enAire; // los que ya se ven en una grilla al aire (de los grupos ya repartidos)

    std::map<std::string, Resultado> porFuente;
    std::map<std::string, std::vector<std::string>> nuevos;
    std::map<std::string, std::int64_t> reacomodo; // los grupos que siguen (los demás se olvidan)
    proximo_ = 0;
    for (std::vector<const Config*>* pgs : orden) {
        std::vector<const Config*>& gs = *pgs;
        std::sort(gs.begin(), gs.end(), [](const Config* a, const Config* b) {
            return a->ordenEnGrupo != b->ordenEnGrupo ? a->ordenEnGrupo < b->ordenEnGrupo : a->fuente < b->fuente;
        });
        const Config& jefe = *gs.front();
        const int G = static_cast<int>(gs.size());
        const std::string claveGrupo = jefe.grupo.empty() ? "\x01" + jefe.fuente : "\x02" + jefe.grupo;

        // LAS ENTRADAS SE JUNTAN (ver el .hpp): los que ya se veían en este grupo y, si
        // entró alguien hace menos de reacomodoMinMs, los nuevos esperan al próximo reacomodo.
        std::set<std::string> previos;
        for (const Config* g : gs)
            if (const auto it = lugares_.find(g->fuente); it != lugares_.end()) previos.insert(it->second.begin(), it->second.end());
        const auto ult = reacomodo_.find(claveGrupo);
        const std::int64_t ultimaEntrada = ult != reacomodo_.end() ? ult->second : std::int64_t(0);
        const bool retener = reacomodoMinMs_ > 0 && !previos.empty() && ult != reacomodo_.end() &&
                             ahora - ultimaEntrada < reacomodoMinMs_;
        if (ult != reacomodo_.end()) reacomodo[claveGrupo] = ultimaEntrada;
        int retenidos = 0;
        bool entro = false;

        // Lo que se decidió a mano para este grupo: fijada en la grilla i (-1 = en ninguna de
        // este grupo) y sacada de alguna grilla del grupo.
        // El orador de cada grilla (lo llena la máquina más abajo): en un grupo, va en la
        // suya como si estuviera fijado ahí (si no, el reparto podía mandarlo a otra).
        std::vector<std::string> oradorDe(static_cast<std::size_t>(G)), siguienteDe(static_cast<std::size_t>(G));
        const auto fijaEn = [&](const std::string& clave) {
            const auto it = manual.fijos.find(clave);
            if (it != manual.fijos.end()) {
                for (int i = 0; i < G; ++i)
                    if (gs[i]->fuente == it->second) return i;
                return -1;
            }
            for (int i = 0; i < G; ++i)
                if (!clave.empty() && oradorDe[static_cast<std::size_t>(i)] == clave) return i;
            return -1;
        };
        const auto sacada = [&](const std::string& clave) {
            return std::any_of(gs.begin(), gs.end(), [&](const Config* g) { return manual.sacado(g->fuente, clave); });
        };
        // ¿La grilla i del grupo acepta a p en esta vuelta?
        const auto aceptaAca = [&](int i, const Persona& p) {
            const int f = fijaEn(p.clave);
            if (f >= 0) return f == i; // fijada: en la suya, digan lo que digan los filtros
            if (sacada(p.clave) || !acepta(*gs[i], p)) return false;
            return !(gs[i]->sinAlAire && enAire.count(p.clave));
        };

        // Los candidatos: los que alguna grilla del grupo acepta, en el orden de la primera;
        // los fijados, antes que nadie (el operador los quiere ver).
        std::vector<const Persona*> cands;
        for (const Persona& p : gente)
            for (int i = 0; i < G; ++i)
                if (aceptaAca(i, p)) {
                    cands.push_back(&p);
                    break;
                }
        // El orador de cada grilla (con su tiempo mínimo): entre los que esa grilla acepta.
        std::set<std::string> oradoresGrupo;
        for (int i = 0; i < G; ++i) {
            const Config& gi = *gs[i];
            if (gi.alHablar == AlHablar::Nada) {
                oradores_.erase(gi.fuente);
                continue;
            }
            std::vector<const Persona*> suyos;
            for (const Persona* p : cands)
                if (aceptaAca(i, *p)) suyos.push_back(p);
            MaquinaOrador& m = oradores_[gi.fuente];
            oradorDe[static_cast<std::size_t>(i)] = m.paso(gi, suyos, manual.destacado(gi.fuente), ahora);
            siguienteDe[static_cast<std::size_t>(i)] = MaquinaOrador::siguiente(gi, suyos, m.actual(), ahora);
            if (!m.actual().empty()) oradoresGrupo.insert(m.actual());
        }
        std::sort(cands.begin(), cands.end(), [&](const Persona* a, const Persona* b) {
            const bool fa = fijaEn(a->clave) >= 0, fb = fijaEn(b->clave) >= 0;
            if (fa != fb) return fa;
            // El orador entra aunque no haya lugar para todos (después de los fijados).
            const bool oa = oradoresGrupo.count(a->clave) > 0, ob = oradoresGrupo.count(b->clave) > 0;
            if (oa != ob) return oa;
            return antes(jefe.orden, *a, *b);
        });

        std::vector<int> cap(G);
        int capTotal = 0;
        for (int i = 0; i < G; ++i) {
            cap[i] = gs[i]->tope > 0 ? std::min(gs[i]->tope, kMaxCeldas) : kMaxCeldas;
            capTotal += cap[i];
        }
        // No entran todos: la última celda de la última grilla es el «+N».
        const bool desborda = static_cast<int>(cands.size()) > capTotal;
        if (desborda) {
            cap[G - 1] -= 1;
            capTotal -= 1;
        }
        const int n = std::min(static_cast<int>(cands.size()), capTotal);
        std::set<std::string> elegidos;
        for (int i = 0; i < n; ++i) elegidos.insert(cands[static_cast<std::size_t>(i)]->clave);

        // Cuántos quiere cada grilla.
        std::vector<int> meta(G, 0);
        if (jefe.reparto == Reparto::EnOrden) {
            int resto = n;
            for (int i = 0; i < G; ++i) {
                meta[i] = std::min(cap[i], resto);
                resto -= meta[i];
            }
        } else {
            int resto = n;
            bool algo = true;
            while (resto > 0 && algo) {
                algo = false;
                for (int i = 0; i < G && resto > 0; ++i)
                    if (meta[i] < cap[i]) {
                        ++meta[i];
                        --resto;
                        algo = true;
                    }
            }
        }
        // Una grilla quiere por lo menos a sus fijados: se los saca a las otras (de atrás para
        // adelante), así nadie larga gente que después tiene que volver a tomar.
        std::vector<int> fijados(G, 0);
        for (int i = 0; i < n; ++i)
            if (const int f = fijaEn(cands[static_cast<std::size_t>(i)]->clave); f >= 0) ++fijados[f];
        for (int i = 0; i < G; ++i)
            while (meta[i] < std::min(fijados[i], cap[i])) {
                int j = G - 1;
                while (j >= 0 && (j == i || meta[j] <= fijados[j])) --j;
                if (j < 0) break;
                --meta[j];
                ++meta[i];
            }
        const auto esFijo = [&](int i, const std::optional<std::string>& k) { return k.has_value() && fijaEn(*k) == i; };

        // 1) Los que siguen elegidos se quedan en su lugar (con hueco donde había alguien que se fue).
        std::vector<std::vector<std::optional<std::string>>> lugares(G);
        std::set<std::string> ubicados;
        for (int i = 0; i < G; ++i) {
            const auto it = lugares_.find(gs[i]->fuente);
            if (it == lugares_.end()) continue;
            for (const std::string& k : it->second) {
                const auto p = porClave.find(k);
                const bool queda = p != porClave.end() && elegidos.count(k) && !ubicados.count(k) && aceptaAca(i, *p->second);
                if (queda) ubicados.insert(k);
                lugares[i].push_back(queda ? std::optional<std::string>(k) : std::nullopt);
            }
        }
        const auto cuenta = [&](int i) {
            return static_cast<int>(std::count_if(lugares[i].begin(), lugares[i].end(), [](const auto& x) { return x.has_value(); }));
        };
        // Larga al ÚLTIMO que llegó a la grilla i y no está fijado ahí. false si no hay a quién.
        const auto largarUno = [&](int i) {
            for (auto it = lugares[i].rbegin(); it != lugares[i].rend(); ++it)
                if (it->has_value() && !esFijo(i, *it)) {
                    ubicados.erase(**it);
                    *it = std::nullopt;
                    return true;
                }
            return false;
        };
        // 2) Una grilla con más de los que le tocan (otra del grupo quedó con menos, o bajó el
        //    tope) larga a los ÚLTIMOS que llegaron a ella: los de adelante no se mueven.
        for (int i = 0; i < G; ++i)
            while (cuenta(i) > meta[i] && largarUno(i)) {
            }
        // 3) Los nuevos, en orden: a la primera grilla que los acepta y quiere más; si ninguna
        //    quiere, a la primera con lugar. Primero a los huecos, después al final. El fijado va
        //    a la suya aunque esté llena: se le hace lugar largando al último que no está fijado.
        for (int idx = 0; idx < n; ++idx) {
            const Persona& p = *cands[static_cast<std::size_t>(idx)];
            if (ubicados.count(p.clave)) continue;
            int dest = fijaEn(p.clave);
            if (dest >= 0 && cuenta(dest) >= cap[dest] && !largarUno(dest)) continue; // llena de fijados: «+N»
            for (int i = 0; i < G && dest < 0; ++i)
                if (cuenta(i) < meta[i] && aceptaAca(i, p)) dest = i;
            for (int i = 0; i < G && dest < 0; ++i)
                if (cuenta(i) < cap[i] && aceptaAca(i, p)) dest = i;
            if (dest < 0) continue; // ninguna lo acepta con lugar: cuenta en el «+N»
            const bool nuevo = !previos.count(p.clave);
            // Recién llegado dentro del intervalo: espera al próximo reacomodo (salvo el
            // orador y los fijados, que fijaEn devuelve: esos entran ya).
            if (nuevo && retener && fijaEn(p.clave) < 0) {
                ++retenidos;
                continue;
            }
            if (nuevo) entro = true;
            auto hueco = std::find_if(lugares[dest].begin(), lugares[dest].end(), [](const auto& x) { return !x.has_value(); });
            if (hueco != lugares[dest].end())
                *hueco = p.clave;
            else
                lugares[dest].push_back(p.clave);
            ubicados.insert(p.clave);
        }
        // 4) Los huecos que quedan los llena el último de esa grilla (se mueve uno, no todos).
        for (int i = 0; i < G; ++i) {
            auto& l = lugares[i];
            while (!l.empty() && !l.back().has_value()) l.pop_back();
            for (std::size_t h = 0; h < l.size(); ++h) {
                if (l[h].has_value()) continue;
                l[h] = l.back();
                l.pop_back();
                while (!l.empty() && !l.back().has_value()) l.pop_back();
            }
        }
        // Solo las entradas marcan la hora del reacomodo (las salidas son en el acto y no
        // demoran a nadie): así los que esperan entran a lo sumo reacomodoMinMs después.
        if (entro) reacomodo[claveGrupo] = ahora;
        if (retenidos > 0) {
            const std::int64_t cuando = ultimaEntrada + reacomodoMinMs_;
            proximo_ = proximo_ == 0 ? cuando : std::min(proximo_, cuando);
        }
        const int mas = static_cast<int>(cands.size()) - static_cast<int>(ubicados.size()) - retenidos;
        for (int i = 0; i < G; ++i) {
            Resultado r;
            r.fuente = gs[i]->fuente;
            r.canal = gs[i]->canal;
            r.entrando = retenidos;
            for (const auto& k : lugares[i]) r.personas.push_back(*k);
            if (i == G - 1 && desborda) r.mas = std::max(0, mas);
            // El orador de esta grilla, si quedó en ella (en un grupo pudo tocarle a otra).
            const std::string& o = oradorDe[static_cast<std::size_t>(i)];
            if (!o.empty() && std::find(r.personas.begin(), r.personas.end(), o) != r.personas.end()) r.orador = o;
            r.siguiente = siguienteDe[static_cast<std::size_t>(i)];
            disponerCeldas(*gs[i], r, porClave);
            // Quien quedó en una grilla al aire ya se ve: los grupos que siguen lo cuentan así.
            if (gs[i]->alAire) enAire.insert(r.personas.begin(), r.personas.end());
            nuevos[r.fuente] = r.personas;
            porFuente[r.fuente] = std::move(r);
        }
    }
    lugares_ = std::move(nuevos); // las grillas que ya no están se olvidan
    reacomodo_ = std::move(reacomodo);

    // Las que muestran solo al orador de otra (la escena del orador): grande, con su nombre.
    for (const Config& c : grillas) {
        if (!c.soloOrador) continue;
        Resultado r;
        r.fuente = c.fuente;
        r.canal = c.canal;
        for (const Config& o : grillas) {
            if (o.soloOrador || (!c.sigue.empty() && o.fuente != c.sigue)) continue;
            const auto it = porFuente.find(o.fuente);
            if (it == porFuente.end() || it->second.orador.empty()) continue;
            r.orador = it->second.orador;
            r.siguiente = it->second.siguiente;
            break;
        }
        if (!r.orador.empty()) {
            r.personas.push_back(r.orador);
            const auto rects = disponer(1, c.ancho, c.alto, c.separacion, c.margen, c.aspecto);
            Celda x;
            x.clave = r.orador;
            if (!rects.empty()) x.r = rects.front();
            if (const auto p = porClave.find(r.orador); p != porClave.end())
                x.rotulo = idRotulo(c.nombres, p->second->nombre, p->second->cargo);
            r.celdas.push_back(std::move(x));
        }
        porFuente[r.fuente] = std::move(r);
    }
    // Las grillas que ya no existen (o que dejaron de tener orador) olvidan a su orador.
    for (auto it = oradores_.begin(); it != oradores_.end();)
        it = std::any_of(grillas.begin(), grillas.end(), [&](const Config& c) { return c.fuente == it->first; })
                 ? std::next(it)
                 : oradores_.erase(it);

    // La capa de cada uno: el orador, el presentador y el precargado, alta; los demás según
    // el lugar más grande donde se los ve, con histéresis y de a una subida por vuelta. En una
    // grilla con más de «alta_hasta» personas las miniaturas no piden la alta por grandes que
    // sean. Y nunca más de «alta_max» en alta, por prioridad (CapasGrilla).
    std::map<std::string, CapasGrilla::Pedido> pedidos;
    std::set<std::string> enBaja;
    for (const Config& c : grillas) {
        Resultado& r = porFuente[c.fuente];
        const AlHablar m = c.soloOrador ? AlHablar::Destacar : c.alHablar;
        const bool grande = m == AlHablar::SinCambios || m == AlHablar::Resaltar || m == AlHablar::Destacar ||
                            m == AlHablar::Superponer;
        r.enBaja = !c.soloOrador &&
                   todasEnBaja(static_cast<int>(r.personas.size()), c.altaHasta, enBaja_.count(c.fuente) > 0);
        if (r.enBaja) enBaja.insert(c.fuente);
        for (const Celda& x : r.celdas) {
            auto& p = pedidos[x.clave];
            const bool orador = grande && x.clave == r.orador;
            if (!r.enBaja || orador) p.alto = std::max(p.alto, x.r.h);
            p.orador = p.orador || orador;
            p.precarga = p.precarga || x.clave == r.siguiente;
            const auto pe = porClave.find(x.clave);
            p.presentador = p.presentador || (pe != porClave.end() && pe->second->presentador);
            p.fijo = p.fijo || manual.fijo(c.fuente, x.clave);
        }
    }
    enBaja_ = std::move(enBaja);
    // El tope duro: el «alta_max» más chico de las grillas (la capa es de la persona).
    int altaMax = 0;
    for (const Config& c : grillas)
        if (c.altaMax > 0) altaMax = altaMax == 0 ? c.altaMax : std::min(altaMax, c.altaMax);
    const std::map<std::string, bool> alta = capas_.decidir(pedidos, 1, altaMax);
    for (auto& [f, r] : porFuente)
        for (Celda& x : r.celdas)
            if (const auto it = alta.find(x.clave); it != alta.end()) x.capa = it->second ? "alta" : "baja";

    std::vector<Resultado> out;
    for (const Config& c : grillas) out.push_back(porFuente[c.fuente]);
    return out;
}

void Repartidor::disponerCeldas(const Config& c, Resultado& r, const std::map<std::string, const Persona*>& porClave) const {
    const auto persona = [&](const std::string& k) -> const Persona* {
        const auto it = porClave.find(k);
        return it != porClave.end() ? it->second : nullptr;
    };
    const auto celda = [&](const std::string& k, const Rect& rect) {
        const Persona* p = persona(k);
        Celda x;
        x.clave = k;
        x.r = rect;
        // El borde: a quien habla (como siempre) y al orador.
        x.resaltar = c.resaltar && p != nullptr &&
                     (p->hablando || (k == r.orador && c.alHablar != AlHablar::Nada && c.alHablar != AlHablar::SinCambios));
        if (p != nullptr) x.rotulo = idRotulo(c.nombres, p->nombre, p->cargo);
        return x;
    };
    const bool conGrande = !r.orador.empty() && (c.alHablar == AlHablar::Destacar || c.alHablar == AlHablar::Superponer);
    if (!conGrande) {
        // La grilla de siempre (nadie se mueve). Agrandar (resaltar y escena, si se eligió):
        // «encima», el orador crece un poco y va al final de la lista (el gateway lo pinta
        // encima de sus vecinas); «achicando», las demás al 94 % y el orador entero.
        const int celdas = static_cast<int>(r.personas.size()) + (r.mas > 0 ? 1 : 0);
        const auto rects = disponer(celdas, c.ancho, c.alto, c.separacion, c.margen, c.aspecto);
        const bool puede = (c.alHablar == AlHablar::Resaltar || c.alHablar == AlHablar::Escena) && c.agrandar != Agrandar::No;
        std::optional<Celda> delOrador;
        for (std::size_t j = 0; j < r.personas.size() && j < rects.size(); ++j) {
            Rect rect = rects[j];
            if (puede && c.agrandar == Agrandar::Encima && r.personas[j] == r.orador && !r.orador.empty()) {
                Rect g = achicar(rect, kResaltarOrador);
                g.x = std::clamp(g.x, 0, std::max(0, c.ancho - g.w));
                g.y = std::clamp(g.y, 0, std::max(0, c.alto - g.h));
                delOrador = celda(r.personas[j], g);
                continue;
            }
            // «Achicando»: siempre (haya o no orador), así nadie se mueve cuando alguien empieza a hablar.
            if (puede && c.agrandar == Agrandar::Achicando && r.personas[j] != r.orador) rect = achicar(rect, kResaltarResto);
            r.celdas.push_back(celda(r.personas[j], rect));
        }
        if (delOrador) r.celdas.push_back(*delOrador);
        if (r.mas > 0 && !rects.empty()) r.rMas = rects.back();
        return;
    }
    // El orador al lugar grande; los demás en el orden de siempre (los lugares estables).
    std::vector<std::string> otros;
    for (const std::string& k : r.personas)
        if (k != r.orador) otros.push_back(k);
    const int n = static_cast<int>(otros.size());
    const int extra = r.mas; // los que ya no entraban en el reparto
    const Disposicion d = c.alHablar == AlHablar::Destacar ? disponerDestacar(n + (extra > 0 ? 1 : 0), c)
                                                           : disponerSuperponer(n + (extra > 0 ? 1 : 0), c);
    r.celdas.push_back(celda(r.orador, d.grande));
    if (c.alHablar == AlHablar::Superponer && c.superponerCompleta) {
        r.mas = 0; // a pantalla completa: los demás no se ven (ni su «+N»)
        return;
    }
    const int entran = std::min(n, static_cast<int>(d.resto.size()));
    for (int j = 0; j < entran; ++j) r.celdas.push_back(celda(otros[static_cast<std::size_t>(j)], d.resto[static_cast<std::size_t>(j)]));
    if (d.hayMas) {
        r.mas = extra + (n - entran);
        r.rMas = d.rMas;
    } else if (extra > 0 && static_cast<int>(d.resto.size()) > n) {
        r.rMas = d.resto[static_cast<std::size_t>(n)];
    } else {
        r.mas = extra + (n - entran);
    }
}

json pedidoGateway(const std::vector<Config>& grillas, const std::vector<Resultado>& res) {
    json lista = json::array();
    std::set<int> canales;
    std::set<std::string> precarga;
    int fundido = -1;
    for (std::size_t i = 0; i < grillas.size() && i < res.size(); ++i) {
        const Config& c = grillas[i];
        const Resultado& r = res[i];
        if (c.canal < 1 || c.canal > kMaxGrillas || !canales.insert(c.canal).second) continue;
        json celdas = json::array();
        for (const Celda& x : r.celdas) {
            json j{{"persona", x.clave}, {"x", x.r.x}, {"y", x.r.y}, {"w", x.r.w}, {"h", x.r.h}, {"resaltar", x.resaltar}};
            if (!x.capa.empty()) j["capa"] = x.capa;
            if (!x.rotulo.empty()) j["rotulo"] = x.rotulo;
            celdas.push_back(std::move(j));
        }
        json g{{"canal", c.canal},       {"ancho", c.ancho},
               {"alto", c.alto},         {"fondo", c.fondo},
               {"resaltar_color", c.colorResaltar}, {"transicion_ms", c.transicionMs},
               {"celdas", celdas}};
        if (c.nombres != Nombres::No)
            g["rotulos"] = {{"color", c.nombreColor},
                            {"fondo", c.nombreFondo},
                            {"tamano", c.nombreTamano / 100.0},
                            {"lugar", c.nombreArriba ? "arriba" : "abajo"}};
        if (r.mas > 0) g["mas"] = {{"n", r.mas}, {"x", r.rMas.x}, {"y", r.rMas.y}, {"w", r.rMas.w}, {"h", r.rMas.h}};
        lista.push_back(std::move(g));
        if (!r.siguiente.empty()) precarga.insert(r.siguiente);
        fundido = std::max(fundido, c.fundidoMs);
    }
    json out{{"grillas", lista}, {"precarga", json(precarga)}};
    if (fundido >= 0) out["fundido_ms"] = fundido;
    return out;
}

std::map<std::string, std::pair<std::string, std::string>> rotulosDe(const std::vector<Config>& grillas,
                                                                     const std::vector<Resultado>& res,
                                                                     const std::vector<Persona>& gente) {
    std::map<std::string, const Persona*> porClave;
    for (const Persona& p : gente) porClave[p.clave] = &p;
    std::map<std::string, std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < grillas.size() && i < res.size(); ++i)
        for (const Celda& x : res[i].celdas) {
            if (x.rotulo.empty() || out.count(x.rotulo)) continue;
            const auto it = porClave.find(x.clave);
            if (it == porClave.end()) continue;
            out[x.rotulo] = {it->second->nombre, grillas[i].nombres == Nombres::NombreCargo ? it->second->cargo : std::string()};
        }
    return out;
}

} // namespace ba::app::grilla
