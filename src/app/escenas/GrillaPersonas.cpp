// ============================================================================
// src/app/escenas/GrillaPersonas.cpp — ver GrillaPersonas.hpp
// ============================================================================

#include "escenas/GrillaPersonas.hpp"

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
                {"color_resaltar", 0xFF07C1FFu}}; // ámbar #FFC107
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
    return c;
}

std::string Config::validar(const json& cambios) {
    if (!cambios.is_object()) return "los ajustes van en un objeto";
    const auto enumera = [&](const char* k, std::initializer_list<const char*> ok) -> std::string {
        if (!cambios.contains(k)) return {};
        const json& v = cambios[k];
        if (v.is_string())
            for (const char* x : ok)
                if (v.get<std::string>() == x) return {};
        std::string lista;
        for (const char* x : ok) lista += std::string(lista.empty() ? "" : ", ") + x;
        return std::string("«") + k + "» es uno de: " + lista;
    };
    const auto numero = [&](const char* k, int mn, int mx) -> std::string {
        if (!cambios.contains(k)) return {};
        const json& v = cambios[k];
        if (!v.is_number() || v.get<double>() < mn || v.get<double>() > mx)
            return std::string("«") + k + "» va de " + std::to_string(mn) + " a " + std::to_string(mx);
        return {};
    };
    const auto booleano = [&](const char* k) -> std::string {
        if (cambios.contains(k) && !cambios[k].is_boolean()) return std::string("«") + k + "» es true o false";
        return {};
    };
    static const std::set<std::string> kConocidos = {
        "canal",      "origen", "grupo",      "orden_en_grupo", "reparto",       "tope",  "orden",
        "solo_con_camara", "sin_al_aire", "resaltar", "ancho", "alto", "separacion", "margen",
        "proporcion", "transicion_ms", "fondo", "color_resaltar"};
    for (const auto& [k, v] : cambios.items())
        if (!kConocidos.count(k)) return "«" + k + "» no es un ajuste de la grilla de personas";
    for (const std::string& e :
         {enumera("origen", {"sala", "asamblea", "ambas"}), enumera("reparto", {"en_orden", "parejo"}),
          enumera("orden", {"llegada", "habla", "prioridad"}),
          enumera("proporcion", {kProporciones[0], kProporciones[1], kProporciones[2], kProporciones[3], kProporciones[4],
                                 kProporciones[5]}),
          numero("canal", 0, kMaxGrillas), numero("orden_en_grupo", -1000, 1000), numero("tope", 0, kMaxCeldas),
          numero("ancho", 64, 3840), numero("alto", 64, 2160), numero("separacion", 0, 200), numero("margen", 0, 500),
          numero("transicion_ms", 0, 3000), booleano("solo_con_camara"), booleano("sin_al_aire"), booleano("resaltar")})
        if (!e.empty()) return e;
    if (cambios.contains("grupo") && !cambios["grupo"].is_string()) return "«grupo» es un texto (vacío = sola)";
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
                                            const Manual& manual) {
    std::map<std::string, const Persona*> porClave;
    for (const Persona& p : gente) porClave[p.clave] = &p;

    // Los grupos: las grillas con el mismo «grupo»; sin grupo, cada una sola.
    std::map<std::string, std::vector<const Config*>> grupos;
    for (const Config& c : grillas) grupos[c.grupo.empty() ? "\x01" + c.fuente : "\x02" + c.grupo].push_back(&c);
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
    for (std::vector<const Config*>* pgs : orden) {
        std::vector<const Config*>& gs = *pgs;
        std::sort(gs.begin(), gs.end(), [](const Config* a, const Config* b) {
            return a->ordenEnGrupo != b->ordenEnGrupo ? a->ordenEnGrupo < b->ordenEnGrupo : a->fuente < b->fuente;
        });
        const Config& jefe = *gs.front();
        const int G = static_cast<int>(gs.size());

        // Lo que se decidió a mano para este grupo: fijada en la grilla i (-1 = en ninguna de
        // este grupo) y sacada de alguna grilla del grupo.
        const auto fijaEn = [&](const std::string& clave) {
            const auto it = manual.fijos.find(clave);
            if (it == manual.fijos.end()) return -1;
            for (int i = 0; i < G; ++i)
                if (gs[i]->fuente == it->second) return i;
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
        std::sort(cands.begin(), cands.end(), [&](const Persona* a, const Persona* b) {
            const bool fa = fijaEn(a->clave) >= 0, fb = fijaEn(b->clave) >= 0;
            if (fa != fb) return fa;
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
        const int mas = static_cast<int>(cands.size()) - static_cast<int>(ubicados.size());
        for (int i = 0; i < G; ++i) {
            Resultado r;
            r.fuente = gs[i]->fuente;
            r.canal = gs[i]->canal;
            for (const auto& k : lugares[i]) r.personas.push_back(*k);
            if (i == G - 1 && desborda) r.mas = std::max(0, mas);
            const int celdas = static_cast<int>(r.personas.size()) + (r.mas > 0 ? 1 : 0);
            const auto rects = disponer(celdas, gs[i]->ancho, gs[i]->alto, gs[i]->separacion, gs[i]->margen, gs[i]->aspecto);
            for (std::size_t j = 0; j < r.personas.size() && j < rects.size(); ++j) {
                const Persona* p = porClave[r.personas[j]];
                r.celdas.push_back({r.personas[j], rects[j], gs[i]->resaltar && p != nullptr && p->hablando});
            }
            if (r.mas > 0 && !rects.empty()) r.rMas = rects.back();
            // Quien quedó en una grilla al aire ya se ve: los grupos que siguen lo cuentan así.
            if (gs[i]->alAire) enAire.insert(r.personas.begin(), r.personas.end());
            nuevos[r.fuente] = r.personas;
            porFuente[r.fuente] = std::move(r);
        }
    }
    lugares_ = std::move(nuevos); // las grillas que ya no están se olvidan

    std::vector<Resultado> out;
    for (const Config& c : grillas) out.push_back(porFuente[c.fuente]);
    return out;
}

json pedidoGateway(const std::vector<Config>& grillas, const std::vector<Resultado>& res) {
    json lista = json::array();
    std::set<int> canales;
    for (std::size_t i = 0; i < grillas.size() && i < res.size(); ++i) {
        const Config& c = grillas[i];
        const Resultado& r = res[i];
        if (c.canal < 1 || c.canal > kMaxGrillas || !canales.insert(c.canal).second) continue;
        json celdas = json::array();
        for (const Celda& x : r.celdas)
            celdas.push_back({{"persona", x.clave}, {"x", x.r.x}, {"y", x.r.y}, {"w", x.r.w}, {"h", x.r.h}, {"resaltar", x.resaltar}});
        json g{{"canal", c.canal},       {"ancho", c.ancho},
               {"alto", c.alto},         {"fondo", c.fondo},
               {"resaltar_color", c.colorResaltar}, {"transicion_ms", c.transicionMs},
               {"celdas", celdas}};
        if (r.mas > 0) g["mas"] = {{"n", r.mas}, {"x", r.rMas.x}, {"y", r.rMas.y}, {"w", r.rMas.w}, {"h", r.rMas.h}};
        lista.push_back(std::move(g));
    }
    return {{"grillas", lista}};
}

} // namespace ba::app::grilla
