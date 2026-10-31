// ============================================================================
// src/app/evento/LineaIntercambio.cpp — ver LineaIntercambio.hpp
// ============================================================================
#include "evento/LineaIntercambio.hpp"

#include "evento/LineaProductor.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace ba::evento::linea {

using ba::i18n::llenar;
using ba::i18n::traducir;
using nlohmann::json;

namespace {

std::string recortar(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Un id que sirva (sin espacios ni barras), desde un texto cualquiera.
std::string idDe(const std::string& texto, const std::string& siVacio) {
    std::string id;
    for (char c : texto) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || u >= 0x80) id += c;
        else if (!id.empty() && id.back() != '-') id += '-';
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    return id.empty() ? siVacio : id;
}

// --- CSV ------------------------------------------------------------------------------

std::string campoCsv(const std::string& v, char sep) {
    if (v.find_first_of(std::string{sep, '"', '\n', '\r'}) == std::string::npos) return v;
    std::string o = "\"";
    for (char c : v) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}

/// Los renglones con sus campos (comillas a lo RFC 4180: un campo entre comillas puede
/// llevar el separador, saltos de línea y "" por una comilla).
std::vector<std::vector<std::string>> renglonesCsv(const std::string& t, char sep) {
    std::vector<std::vector<std::string>> out;
    std::vector<std::string> fila;
    std::string campo;
    bool comillas = false, hay = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (comillas) {
            if (c == '"' && i + 1 < t.size() && t[i + 1] == '"') {
                campo += '"';
                ++i;
            } else if (c == '"') {
                comillas = false;
            } else {
                campo += c;
            }
            continue;
        }
        if (c == '"') {
            comillas = true;
            hay = true;
        } else if (c == sep) {
            fila.push_back(campo);
            campo.clear();
            hay = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < t.size() && t[i + 1] == '\n') ++i;
            if (hay || !campo.empty()) {
                fila.push_back(campo);
                out.push_back(fila);
            }
            fila.clear();
            campo.clear();
            hay = false;
        } else {
            campo += c;
            hay = true;
        }
    }
    if (hay || !campo.empty()) {
        fila.push_back(campo);
        out.push_back(fila);
    }
    return out;
}

/// El separador: el que más aparece en el encabezado (fuera de comillas).
char adivinarSeparador(const std::string& t) {
    const std::string primera = t.substr(0, t.find('\n'));
    int coma = 0, pyc = 0, tab = 0;
    bool comillas = false;
    for (char c : primera) {
        if (c == '"') comillas = !comillas;
        if (comillas) continue;
        coma += c == ',';
        pyc += c == ';';
        tab += c == '\t';
    }
    if (tab > coma && tab > pyc) return '\t';
    return pyc > coma ? ';' : ',';
}

/// La columna (clave nuestra) de un encabezado, también en inglés o con mayúsculas.
std::string columnaDe(const std::string& encabezado) {
    static const std::map<std::string, std::string> alias{
        {"bloque", "bloque"},     {"segment", "bloque"},   {"segmento", "bloque"},   {"block", "bloque"},
        {"id", "id"},             {"nombre", "nombre"},    {"paso", "nombre"},       {"title", "nombre"},
        {"titulo", "nombre"},     {"título", "nombre"},    {"name", "nombre"},       {"slug", "nombre"},
        {"story", "nombre"},      {"entra", "entra"},      {"start", "entra"},       {"inicio", "entra"},
        {"hora", "hora"},         {"time", "hora"},        {"front", "hora"},        {"duracion", "duracion"},
        {"duración", "duracion"}, {"duration", "duracion"}, {"dur", "duracion"},     {"fin", "fin"},
        {"end", "fin"},           {"continuar", "continuar"}, {"autonext", "continuar"}, {"escena", "escena"},
        {"scene", "escena"},      {"camera", "escena"},    {"camara", "escena"},     {"guion", "guion"},
        {"guión", "guion"},       {"script", "guion"},     {"texto", "guion"},       {"nota", "nota"},
        {"notas", "nota"},        {"notes", "nota"},       {"note", "nota"}};
    const auto it = alias.find(minusculas(recortar(encabezado)));
    return it == alias.end() ? std::string() : it->second;
}

std::string entraDe(const Inicio& i) {
    switch (i.modo) {
        case ModoInicio::Go: return "go";
        case ModoInicio::Tras: return i.de == "anterior" ? "tras" : "tras " + i.de;
        case ModoInicio::Con: return i.de == "anterior" ? "con" : "con " + i.de;
        case ModoInicio::Hora: {
            const char* p = i.politica == Politica::Esperar ? " esperar" : i.politica == Politica::Cortar ? " cortar" : "";
            return "hora " + i.hora + p;
        }
        case ModoInicio::Cuando: return "cuando " + i.condicion;
        case ModoInicio::Ancla: return "ancla " + i.salida + "/" + i.paso;
    }
    return "go";
}

/// `entra` → Inicio. false si no se entiende.
bool inicioDe(const std::string& texto, Inicio& i) {
    const std::string t = recortar(texto);
    const std::string palabra = minusculas(t.substr(0, t.find(' ')));
    const std::string resto = t.find(' ') == std::string::npos ? std::string() : recortar(t.substr(t.find(' ') + 1));
    i = Inicio{};
    if (palabra.empty() || palabra == "go" || palabra == "manual") return true;
    if (palabra == "tras" || palabra == "auto" || palabra == "sigue") {
        i.modo = ModoInicio::Tras;
        if (!resto.empty()) i.de = resto;
        return true;
    }
    if (palabra == "con") {
        i.modo = ModoInicio::Con;
        if (!resto.empty()) i.de = resto;
        return true;
    }
    if (palabra == "hora" || palabra == "hard") {
        const std::string hora = resto.substr(0, resto.find(' '));
        const std::string politica = resto.find(' ') == std::string::npos ? "avisar" : recortar(resto.substr(resto.find(' ') + 1));
        if (sumarMinutos(hora, 0).empty()) return false;
        i = Inicio::deJson(json{{"modo", "hora"}, {"hora", sumarMinutos(hora, 0)}, {"politica", minusculas(politica)}});
        return true;
    }
    if (palabra == "cuando" && !resto.empty()) {
        i.modo = ModoInicio::Cuando;
        i.condicion = resto;
        return true;
    }
    if (palabra == "ancla" && resto.find('/') != std::string::npos) {
        i.modo = ModoInicio::Ancla;
        i.salida = resto.substr(0, resto.find('/'));
        i.paso = resto.substr(resto.find('/') + 1);
        return true;
    }
    // Una hora suelta («19:30»): un hard start que avisa.
    if (!sumarMinutos(t, 0).empty()) {
        i = Inicio::deJson(json{{"modo", "hora"}, {"hora", sumarMinutos(t, 0)}, {"politica", "avisar"}});
        return true;
    }
    return false;
}

std::string finDe(const Fin& f) {
    switch (f.modo) {
        case ModoFin::Duracion: return {};
        case ModoFin::AlTerminar: return "medio";
        case ModoFin::Abierto: return "abierto";
        case ModoFin::Mientras: return "mientras " + f.mientras;
    }
    return {};
}

const char* continuarDe(Continuar c) { return c == Continuar::Sigue ? "sigue" : c == Continuar::Con ? "con" : "no"; }

/// Las pistas de una línea leída: una por tipo, de la primera salida (lineaDeGuia las
/// lleva a las de la línea de destino por tipo).
void sumarPista(Linea& l, const std::string& tipo) {
    if (l.pista(tipo) != nullptr) return;
    Pista p;
    p.id = tipo;
    p.tipo = tipo;
    p.nombre = tipo;
    l.pistas.push_back(p);
}

Pieza piezaEscena(const std::string& pasoId, const std::string& escena) {
    Pieza z;
    z.id = pasoId + "-escena";
    z.pista = "escena";
    z.tipo = "escena";
    z.texto = escena;
    z.datos = {{"escena", escena}};
    return z;
}

// --- XML (lo justo para MOS) --------------------------------------------------------------

std::string escaparXml(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string desescaparXml(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        const std::size_t f = s.find(';', i);
        if (f == std::string::npos) {
            o += s[i];
            continue;
        }
        const std::string e = s.substr(i + 1, f - i - 1);
        if (e == "amp") o += '&';
        else if (e == "lt") o += '<';
        else if (e == "gt") o += '>';
        else if (e == "quot") o += '"';
        else if (e == "apos") o += '\'';
        else if (!e.empty() && e[0] == '#') {
            const long cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtol(e.c_str() + 2, nullptr, 16) : std::strtol(e.c_str() + 1, nullptr, 10);
            // A UTF-8.
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) {
                o += static_cast<char>(0xC0 | (cp >> 6));
                o += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                o += static_cast<char>(0xE0 | (cp >> 12));
                o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                o += static_cast<char>(0x80 | (cp & 0x3F));
            }
        } else {
            o += s.substr(i, f - i + 1);
        }
        i = f;
    }
    return o;
}

/// Los bloques <tag>…</tag> que hay en `xml` (sin anidar el mismo tag), desde `desde`.
std::vector<std::string> bloquesXml(const std::string& xml, const std::string& tag) {
    std::vector<std::string> out;
    const std::string abre = "<" + tag, cierra = "</" + tag + ">";
    std::size_t i = 0;
    while ((i = xml.find(abre, i)) != std::string::npos) {
        const std::size_t finAbre = xml.find('>', i);
        if (finAbre == std::string::npos) break;
        const char sig = xml[i + abre.size()];
        if (sig != '>' && sig != ' ' && sig != '/' && sig != '\n' && sig != '\t' && sig != '\r') {
            i += abre.size();
            continue;
        }
        if (xml[finAbre - 1] == '/') { // <tag/>
            out.emplace_back();
            i = finAbre + 1;
            continue;
        }
        const std::size_t f = xml.find(cierra, finAbre);
        if (f == std::string::npos) break;
        out.push_back(xml.substr(finAbre + 1, f - finAbre - 1));
        i = f + cierra.size();
    }
    return out;
}

/// El texto del primer <tag> (desescapado y sin espacios a los costados); "" si no está.
std::string textoXml(const std::string& xml, const std::string& tag) {
    const auto b = bloquesXml(xml, tag);
    if (b.empty()) return {};
    std::string t = b.front();
    // Un CDATA, tal cual.
    if (t.find("<![CDATA[") != std::string::npos) {
        const std::size_t a = t.find("<![CDATA[") + 9, f = t.find("]]>", a);
        return t.substr(a, f == std::string::npos ? std::string::npos : f - a);
    }
    return recortar(desescaparXml(t));
}

constexpr const char* kEsquema = "urn:broadcast-appliance:linea";

/// El payload nuestro de un story o un item (si lo es), como JSON.
json payloadPropio(const std::string& xml) {
    for (const std::string& meta : bloquesXml(xml, "mosExternalMetadata")) {
        if (textoXml(meta, "mosSchema") != kEsquema) continue;
        const json j = json::parse(textoXml(meta, "linea"), nullptr, false);
        if (j.is_object()) return j;
    }
    return json::object();
}

std::string metadata(const json& j) {
    return std::string("<mosExternalMetadata><mosScope>PLAYLIST</mosScope><mosSchema>") + kEsquema + "</mosSchema><mosPayload><linea>" +
           escaparXml(j.dump(-1, ' ', false, json::error_handler_t::replace)) + "</linea></mosPayload></mosExternalMetadata>";
}

} // namespace

int segundosDe(const std::string& texto) {
    const std::string t = recortar(texto);
    if (t.empty()) return -1;
    int total = 0, parte = 0, partes = 0;
    bool digito = false;
    for (char c : t) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            parte = parte * 10 + (c - '0');
            digito = true;
        } else if (c == ':' && digito) {
            total = total * 60 + parte;
            parte = 0;
            digito = false;
            if (++partes > 2) return -1;
        } else if (c == '.' || c == ',') {
            break; // fracciones de segundo: no cuentan
        } else {
            return -1;
        }
    }
    if (!digito) return -1;
    return total * 60 + parte;
}

std::string textoDeSegundos(int seg) {
    seg = std::max(0, seg);
    char b[32];
    if (seg >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", seg / 3600, seg / 60 % 60, seg % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", seg / 60, seg % 60);
    return b;
}

// --- Exportar -----------------------------------------------------------------------------

std::string escaletaCsv(const Linea& l, const Resolucion* r, const std::string& salida, const Hora& hora) {
    const char sep = ',';
    const SalidaResuelta* sr = r != nullptr ? r->salida(salida.empty() ? l.salidasEfectivas().front() : salida) : nullptr;
    std::string o = "bloque,id,nombre,entra,hora,duracion,fin,continuar,escena,guion,nota\r\n";
    for (const Bloque& b : l.bloques)
        for (const Paso& p : b.pasos) {
            std::string h;
            if (sr != nullptr)
                if (const PasoResuelto* pr = sr->paso(p.id)) h = hora ? hora(pr->frenteMs) : std::to_string(pr->frenteMs);
            const std::vector<std::string> campos{b.nombre,
                                                  p.id,
                                                  p.nombre,
                                                  entraDe(p.inicio),
                                                  h,
                                                  textoDeSegundos(static_cast<int>(p.duracionPlanMs() / 1000)),
                                                  finDe(p.fin),
                                                  continuarDe(p.continuar),
                                                  escenaDePaso(l, p),
                                                  p.guion,
                                                  p.notaOperador};
            for (std::size_t i = 0; i < campos.size(); ++i) o += (i ? std::string(1, sep) : std::string()) + campoCsv(campos[i], sep);
            o += "\r\n";
        }
    return o;
}

std::string escaletaMos(const Linea& l, const std::string& roId, const std::string& roSlug) {
    std::string o = "<mos><mosID>broadcast-appliance</mosID><ncsID>broadcast-appliance</ncsID><messageID>1</messageID><roCreate><roID>" +
                    escaparXml(roId) + "</roID><roSlug>" + escaparXml(roSlug) + "</roSlug>";
    int n = 0;
    for (const Bloque& b : l.bloques)
        for (const Paso& p : b.pasos) {
            // Lo del paso, sin sus piezas (cada una va en su item).
            json pj = p.aJson();
            pj.erase("piezas");
            json meta{{"bloque", {{"id", b.id}, {"nombre", b.nombre}}}, {"paso", pj}};
            o += "<story><storyID>" + escaparXml(p.id) + "</storyID><storySlug>" + escaparXml(p.nombre) + "</storySlug><storyNum>" +
                 std::to_string(++n) + "</storyNum>" + metadata(meta);
            for (const Pieza& z : p.piezas) {
                const Pista* pi = l.pista(z.pista);
                json zj = z.aJson();
                zj["pistaTipo"] = pi != nullptr ? pi->tipo : z.pista;
                o += "<item><itemID>" + escaparXml(z.id) + "</itemID><itemSlug>" + escaparXml(z.texto.empty() ? z.tipo : z.texto) +
                     "</itemSlug><objID>" + escaparXml(z.id) + "</objID><mosID>broadcast-appliance</mosID>" + metadata(zj) + "</item>";
            }
            o += "</story>";
        }
    return o + "</roCreate></mos>";
}

// --- Leer ---------------------------------------------------------------------------------

Leida leerCsv(const std::string& texto, const std::string& nombreBloque) {
    Leida out;
    std::string t = texto;
    if (t.rfind("\xEF\xBB\xBF", 0) == 0) t = t.substr(3); // el BOM de las planillas
    const auto filas = renglonesCsv(t, adivinarSeparador(t));
    if (filas.size() < 2) {
        out.error = traducir("linea", "el CSV no trae pasos (hace falta el encabezado y al menos un renglón)");
        return out;
    }
    std::vector<std::string> col;
    bool alguna = false;
    for (const std::string& h : filas[0]) {
        col.push_back(columnaDe(h));
        alguna = alguna || col.back() == "nombre";
    }
    if (!alguna) {
        out.error = traducir("linea", "el CSV no tiene la columna «nombre» (o «title»)");
        return out;
    }
    std::set<std::string> ids;
    Linea& l = out.linea;
    for (std::size_t f = 1; f < filas.size(); ++f) {
        std::map<std::string, std::string> v;
        for (std::size_t i = 0; i < filas[f].size() && i < col.size(); ++i)
            if (!col[i].empty()) v[col[i]] = recortar(filas[f][i]);
        if (v["nombre"].empty() && v["guion"].empty()) continue;
        const std::string nombreB = !v["bloque"].empty() ? v["bloque"] : !nombreBloque.empty() ? nombreBloque : traducir("linea", "Importado");
        if (l.bloques.empty() || l.bloques.back().nombre != nombreB) {
            Bloque b;
            b.nombre = nombreB;
            b.id = idDe(nombreB, "bloque");
            for (int k = 2; ids.count(b.id); ++k) b.id = idDe(nombreB, "bloque") + "-" + std::to_string(k);
            ids.insert(b.id);
            l.bloques.push_back(b);
        }
        Paso p;
        p.nombre = v["nombre"].empty() ? v["guion"].substr(0, 40) : v["nombre"];
        p.id = idDe(v["id"].empty() ? p.nombre : v["id"], "paso");
        const std::string base = p.id;
        for (int k = 2; ids.count(p.id); ++k) p.id = base + "-" + std::to_string(k);
        ids.insert(p.id);
        const std::string renglon = std::to_string(f + 1);
        if (!inicioDe(v["entra"], p.inicio))
            out.avisos.push_back(llenar(traducir("linea", "renglón %1: «%2» no es una manera de entrar (go, tras, con, hora 19:30…): entra con GO"),
                                        {renglon, v["entra"]}));
        const int dur = v["duracion"].empty() ? 0 : segundosDe(v["duracion"]);
        if (dur < 0)
            out.avisos.push_back(llenar(traducir("linea", "renglón %1: la duración «%2» no se entiende (m:ss)"), {renglon, v["duracion"]}));
        const std::string fin = minusculas(v["fin"]);
        if (fin == "abierto" || fin == "open") p.fin = Fin::deJson(json{{"abierto", true}});
        else if (fin == "medio" || fin == "media") p.fin = Fin::deJson(json{{"alTerminar", "medio"}});
        else if (fin.rfind("mientras ", 0) == 0) p.fin = Fin::deJson(json{{"mientras", recortar(v["fin"].substr(9))}});
        else p.fin.duracionSeg = std::max(0, dur);
        p.duracionPrevistaSeg = std::max(0, dur);
        const std::string cont = minusculas(v["continuar"]);
        p.continuar = cont == "sigue" || cont == "si" || cont == "sí" || cont == "true" ? Continuar::Sigue
                      : cont == "con"                                                  ? Continuar::Con
                                                                                       : Continuar::No;
        p.guion = v["guion"];
        p.notaOperador = v["nota"];
        if (!v["escena"].empty()) {
            p.piezas.push_back(piezaEscena(p.id, v["escena"]));
            sumarPista(l, "escena");
        }
        l.bloques.back().pasos.push_back(std::move(p));
    }
    if (l.bloques.empty()) {
        out.error = traducir("linea", "el CSV no trae pasos con nombre");
        return out;
    }
    out.ok = true;
    return out;
}

Leida leerMos(const std::string& xml) {
    Leida out;
    Linea& l = out.linea;
    const std::string slug = textoXml(xml, "roSlug");
    std::set<std::string> ids;
    const auto libre = [&ids](std::string id) {
        const std::string base = id;
        for (int k = 2; ids.count(id); ++k) id = base + "-" + std::to_string(k);
        ids.insert(id);
        return id;
    };
    for (const std::string& st : bloquesXml(xml, "story")) {
        const json meta = payloadPropio(st.substr(0, st.find("<item")));
        Paso p;
        if (meta.contains("paso") && meta["paso"].is_object()) p = Paso::deJson(meta["paso"]);
        p.piezas.clear();
        const std::string storyId = textoXml(st, "storyID");
        if (p.id.empty()) p.id = idDe(storyId, "paso");
        if (p.nombre.empty()) p.nombre = textoXml(st, "storySlug");
        if (p.nombre.empty()) p.nombre = storyId;
        p.id = libre(p.id);
        // La duración de otro sistema: <storyDuration> o <EditorialDuration> en segundos.
        if (!meta.contains("paso"))
            for (const char* tag : {"storyDuration", "EditorialDuration", "TotalDuration"})
                if (const int s = segundosDe(textoXml(st, tag)); s > 0) {
                    p.fin.duracionSeg = s;
                    p.duracionPrevistaSeg = s;
                    break;
                }
        for (const std::string& it : bloquesXml(st, "item")) {
            const json zj = payloadPropio(it);
            Pieza z;
            if (zj.is_object() && zj.contains("tipo")) {
                z = Pieza::deJson(zj);
                z.pista = zj.value("pistaTipo", z.pista);
            } else {
                // De otro sistema: una nota con el slug (lo que diga, el operador lo lee).
                z.tipo = "nota";
                z.pista = "notas";
                z.texto = textoXml(it, "itemSlug");
                z.desde.modo = Ancla::Modo::Manual;
            }
            if (z.id.empty()) z.id = idDe(textoXml(it, "itemID"), p.id + "-item");
            z.id = libre(z.id);
            sumarPista(l, z.pista);
            p.piezas.push_back(std::move(z));
        }
        // El bloque: el del payload o uno solo con el slug del running order.
        std::string bId = "mos", bNombre = slug.empty() ? traducir("linea", "Importado") : slug;
        if (meta.contains("bloque") && meta["bloque"].is_object()) {
            bId = meta["bloque"].value("id", bId);
            bNombre = meta["bloque"].value("nombre", bNombre);
        }
        if (l.bloques.empty() || l.bloques.back().nombre != bNombre) {
            Bloque b;
            b.id = libre(idDe(bId, "bloque"));
            b.nombre = bNombre;
            l.bloques.push_back(b);
        }
        l.bloques.back().pasos.push_back(std::move(p));
    }
    if (l.bloques.empty()) {
        out.error = traducir("linea", "el MOS no trae ningún «story» (roCreate o roStoryAppend/Insert/Replace)");
        return out;
    }
    out.ok = true;
    return out;
}

json importar(const Linea& l, const Leida& leida) {
    if (!leida.ok) return {{"ok", false}, {"error", leida.error}};
    json r = lineaDeGuia(l, leida.linea);
    for (const json& a : leida.avisos) r["avisos"].push_back(a);
    return r;
}

} // namespace ba::evento::linea
