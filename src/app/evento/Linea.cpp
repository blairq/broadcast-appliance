// ============================================================================
// src/app/evento/Linea.cpp — ver Linea.hpp
// ============================================================================
#include "evento/Linea.hpp"

#include "evento/Condicion.hpp"

#include "evento/Evento.hpp"
#include "evento/LeerJson.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace ba::evento::linea {
namespace {

using nlohmann::json;

// Leer sin tirar excepciones: evento/LeerJson.hpp (los mismos lectores que el v1).

template <class T, class F>
std::vector<T> lista(const json& j, const char* k, F deJson) {
    std::vector<T> v;
    const auto it = j.find(k);
    if (it == j.end() || !it->is_array()) return v;
    for (const json& x : *it)
        if (x.is_object()) v.push_back(deJson(x));
    return v;
}

template <class T>
json listaJson(const std::vector<T>& v) {
    json a = json::array();
    for (const T& x : v)
        a.push_back(x.aJson());
    return a;
}

// --- Nombres de los enums (los del archivo: nunca se traducen) -----------------------

template <class E>
struct Nombre {
    E valor;
    const char* nombre;
};

constexpr Nombre<ModoInicio> kModos[] = {{ModoInicio::Go, "go"},         {ModoInicio::Tras, "tras"},
                                         {ModoInicio::Con, "con"},       {ModoInicio::Hora, "hora"},
                                         {ModoInicio::Cuando, "cuando"}, {ModoInicio::Ancla, "ancla"}};
constexpr Nombre<Politica> kPoliticas[] = {
    {Politica::Avisar, "avisar"}, {Politica::Esperar, "esperar"}, {Politica::Cortar, "cortar"}};
constexpr Nombre<Continuar> kContinuar[] = {{Continuar::No, "no"}, {Continuar::Sigue, "sigue"}, {Continuar::Con, "con"}};
constexpr Nombre<Vida> kVidas[] = {{Vida::Paso, "paso"},
                                   {Vida::Bloque, "bloque"},
                                   {Vida::HastaQueSeSaque, "hastaQueSeSaque"},
                                   {Vida::Evento, "evento"},
                                   {Vida::AlAvanzar, "alAvanzar"}};
constexpr Nombre<SiSePerdio> kSiSePerdio[] = {
    {SiSePerdio::Ejecutar, "ejecutar"}, {SiSePerdio::Saltar, "saltar"}, {SiSePerdio::Preguntar, "preguntar"}};
constexpr Nombre<ComoEntro> kComo[] = {{ComoEntro::Go, "go"},
                                       {ComoEntro::Auto, "auto"},
                                       {ComoEntro::Hora, "hora"},
                                       {ComoEntro::Cuando, "cuando"},
                                       {ComoEntro::Ancla, "ancla"}};
constexpr Nombre<EstadoPaso> kEstados[] = {{EstadoPaso::Pasado, "pasado"},
                                           {EstadoPaso::AlAire, "alAire"},
                                           {EstadoPaso::Proximo, "proximo"},
                                           {EstadoPaso::Futuro, "futuro"},
                                           {EstadoPaso::Salteado, "salteado"}};

template <class E, std::size_t N>
const char* nombreEn(const Nombre<E> (&t)[N], E v) {
    for (const auto& n : t)
        if (n.valor == v) return n.nombre;
    return t[0].nombre;
}

/// El valor de ese nombre; si no está (un archivo de una versión más nueva), el primero.
template <class E, std::size_t N>
E valorEn(const Nombre<E> (&t)[N], const std::string& s) {
    for (const auto& n : t)
        if (s == n.nombre) return n.valor;
    return t[0].valor;
}

} // namespace

const char* nombreDe(EstadoPaso e) { return nombreEn(kEstados, e); }
const char* nombreDe(ComoEntro c) { return nombreEn(kComo, c); }
const char* nombreDe(SiSePerdio s) { return nombreEn(kSiSePerdio, s); }
const char* nombreDe(Vida v) { return nombreEn(kVidas, v); }
const char* nombreDe(Politica p) { return nombreEn(kPoliticas, p); }
const char* nombreDe(Continuar c) { return nombreEn(kContinuar, c); }

// --- JSON del modelo -----------------------------------------------------------------
//
// Las claves son las del plan (3.2) y no se traducen. Se escribe lo que dice algo:
// un Inicio "tras" no lleva hora, un Fin abierto no lleva duración. Así el archivo
// se lee a mano igual que el v1.

json Inicio::aJson() const {
    json j{{"modo", nombreEn(kModos, modo)}};
    switch (modo) {
        case ModoInicio::Go: break;
        case ModoInicio::Tras: j["de"] = de; break;
        case ModoInicio::Con:
            j["de"] = de;
            j["masMs"] = masMs;
            break;
        case ModoInicio::Hora:
            j["hora"] = hora;
            j["politica"] = nombreEn(kPoliticas, politica);
            break;
        case ModoInicio::Cuando: j["condicion"] = condicion; break;
        case ModoInicio::Ancla:
            j["salida"] = salida;
            j["paso"] = paso;
            j["masMs"] = masMs;
            break;
    }
    return j;
}

Inicio Inicio::deJson(const json& j) {
    Inicio i;
    if (!j.is_object()) return i;
    i.modo = valorEn(kModos, leerTexto(j, "modo"));
    i.de = leerTexto(j, "de", "anterior");
    i.masMs = leerNumero(j, "masMs");
    i.hora = leerTexto(j, "hora");
    i.politica = valorEn(kPoliticas, leerTexto(j, "politica"));
    i.condicion = leerTexto(j, "condicion");
    i.salida = leerTexto(j, "salida");
    i.paso = leerTexto(j, "paso");
    return i;
}

json Fin::aJson() const {
    switch (modo) {
        case ModoFin::Duracion: return json{{"duracion", duracionSeg}};
        case ModoFin::AlTerminar: return json{{"alTerminar", alTerminar}};
        case ModoFin::Abierto: return json{{"abierto", true}};
        case ModoFin::Mientras: return json{{"mientras", mientras}};
    }
    return json::object();
}

Fin Fin::deJson(const json& j) {
    Fin f;
    if (!j.is_object()) return f;
    if (leerBool(j, "abierto")) {
        f.modo = ModoFin::Abierto;
    } else if (j.contains("mientras")) {
        f.modo = ModoFin::Mientras;
        f.mientras = leerTexto(j, "mientras");
    } else if (j.contains("alTerminar")) {
        f.modo = ModoFin::AlTerminar;
        f.alTerminar = leerTexto(j, "alTerminar", "medio");
    } else {
        f.duracionSeg = static_cast<int>(std::max<int64_t>(0, leerNumero(j, "duracion")));
    }
    return f;
}

json Ancla::aJson() const {
    switch (modo) {
        case Modo::Ms: return json{{"ms", ms}};
        case Modo::AntesDelFin: return json{{"antesDelFin", ms}};
        case Modo::Manual: return ms > 0 ? json{{"manual", true}, {"ms", ms}} : json{{"manual", true}};
        case Modo::Cuando: return json{{"cuando", cuando}};
    }
    return json::object();
}

Ancla Ancla::deJson(const json& j) {
    Ancla a;
    if (!j.is_object()) return a;
    if (leerBool(j, "manual")) {
        a.modo = Modo::Manual;
        a.ms = std::max<int64_t>(0, leerNumero(j, "ms"));
    } else if (j.contains("cuando")) {
        a.modo = Modo::Cuando;
        a.cuando = leerTexto(j, "cuando");
    } else if (j.contains("antesDelFin")) {
        a.modo = Modo::AntesDelFin;
        a.ms = std::max<int64_t>(0, leerNumero(j, "antesDelFin"));
    } else {
        a.ms = std::max<int64_t>(0, leerNumero(j, "ms"));
    }
    return a;
}

json Dura::aJson() const {
    switch (modo) {
        case Modo::Vida: return json::object();
        case Modo::Ms: return json{{"ms", ms}};
        case Modo::AlTerminar: return json{{"alTerminar", alTerminar}};
    }
    return json::object();
}

Dura Dura::deJson(const json& j) {
    Dura d;
    if (!j.is_object()) return d;
    if (j.contains("ms")) {
        d.modo = Modo::Ms;
        d.ms = std::max<int64_t>(0, leerNumero(j, "ms"));
    } else if (j.contains("alTerminar")) {
        d.modo = Modo::AlTerminar;
        d.alTerminar = leerTexto(j, "alTerminar", "medio");
    }
    return d;
}

SiSePerdio Pieza::siSePerdioEfectivo() const {
    if (siSePerdio) return *siSePerdio;
    if (tipo == "asistente") return SiSePerdio::Preguntar;
    // Lo de vida larga (la música, el gráfico fijo, el DSK) se repone: es el estado
    // que el aire tiene que tener. Lo puntual que ya pasó, no.
    return vida == Vida::Paso || vida == Vida::AlAvanzar ? SiSePerdio::Saltar : SiSePerdio::Ejecutar;
}

json Pieza::aJson() const {
    json j{{"id", id},
           {"pista", pista},
           {"tipo", tipo},
           {"texto", texto},
           {"datos", datos},
           {"desde", desde.aJson()},
           {"dura", dura.aJson()},
           {"vida", nombreEn(kVidas, vida)},
           {"prioridad", prioridad}};
    if (siSePerdio) j["siSePerdio"] = nombreEn(kSiSePerdio, *siSePerdio);
    return j;
}

Pieza Pieza::deJson(const json& j) {
    Pieza p;
    p.id = leerTexto(j, "id");
    p.pista = leerTexto(j, "pista");
    p.tipo = leerTexto(j, "tipo");
    p.texto = leerTexto(j, "texto");
    if (j.contains("datos") && j["datos"].is_object()) p.datos = j["datos"];
    if (j.contains("desde")) p.desde = Ancla::deJson(j["desde"]);
    if (j.contains("dura")) p.dura = Dura::deJson(j["dura"]);
    p.vida = valorEn(kVidas, leerTexto(j, "vida"));
    p.prioridad = static_cast<int>(leerNumero(j, "prioridad"));
    if (j.contains("siSePerdio") && j["siSePerdio"].is_string())
        p.siSePerdio = valorEn(kSiSePerdio, j["siSePerdio"].get<std::string>());
    return p;
}

int64_t Paso::duracionPlanMs() const {
    if (fin.modo == ModoFin::Duracion && fin.duracionSeg > 0) return int64_t{fin.duracionSeg} * 1000;
    return int64_t{std::max(0, duracionPrevistaSeg)} * 1000;
}

json Paso::aJson() const {
    json j{{"id", id},
           {"nombre", nombre},
           {"salida", salida},
           {"guion", guion},
           {"inicio", inicio.aJson()},
           {"fin", fin.aJson()},
           {"continuar", nombreEn(kContinuar, continuar)},
           {"preEsperaMs", preEsperaMs},
           {"postEsperaMs", postEsperaMs},
           {"duracionPrevistaSeg", duracionPrevistaSeg},
           {"notaOperador", notaOperador},
           {"etiquetas", etiquetas},
           {"piezas", listaJson(piezas)}};
    if (!transicion.is_null()) j["transicion"] = transicion;
    return j;
}

Paso Paso::deJson(const json& j) {
    Paso p;
    p.id = leerTexto(j, "id");
    p.nombre = leerTexto(j, "nombre");
    p.salida = leerTexto(j, "salida");
    p.guion = leerTexto(j, "guion");
    if (j.contains("inicio")) p.inicio = Inicio::deJson(j["inicio"]);
    if (j.contains("fin")) p.fin = Fin::deJson(j["fin"]);
    p.continuar = valorEn(kContinuar, leerTexto(j, "continuar"));
    p.preEsperaMs = std::max<int64_t>(0, leerNumero(j, "preEsperaMs"));
    p.postEsperaMs = std::max<int64_t>(0, leerNumero(j, "postEsperaMs"));
    if (j.contains("transicion")) p.transicion = j["transicion"];
    p.duracionPrevistaSeg = static_cast<int>(std::max<int64_t>(0, leerNumero(j, "duracionPrevistaSeg")));
    p.notaOperador = leerTexto(j, "notaOperador");
    if (j.contains("etiquetas") && j["etiquetas"].is_array())
        for (const json& e : j["etiquetas"])
            if (e.is_string()) p.etiquetas.push_back(e.get<std::string>());
    p.piezas = lista<Pieza>(j, "piezas", Pieza::deJson);
    return p;
}

json Bloque::aJson() const {
    json j{{"id", id}, {"nombre", nombre}, {"nota", nota}, {"pasos", listaJson(pasos)}};
    if (presupuestoSeg) j["presupuestoSeg"] = *presupuestoSeg;
    if (inicio) j["inicio"] = inicio->aJson();
    if (!plantilla.empty()) j["plantilla"] = plantilla;
    if (!origen.empty()) j["origen"] = origen;
    return j;
}

Bloque Bloque::deJson(const json& j) {
    Bloque b;
    b.id = leerTexto(j, "id");
    b.nombre = leerTexto(j, "nombre");
    b.nota = leerTexto(j, "nota");
    if (j.contains("presupuestoSeg") && j["presupuestoSeg"].is_number())
        b.presupuestoSeg = static_cast<int>(std::max<int64_t>(0, leerNumero(j, "presupuestoSeg")));
    if (j.contains("inicio") && j["inicio"].is_object()) b.inicio = Inicio::deJson(j["inicio"]);
    b.plantilla = leerTexto(j, "plantilla");
    b.origen = leerTexto(j, "origen");
    b.pasos = lista<Paso>(j, "pasos", Paso::deJson);
    return b;
}

json Pista::aJson() const {
    return json{{"id", id},
                {"tipo", tipo},
                {"salida", salida.empty() ? json(nullptr) : json(salida)},
                {"nombre", nombre},
                {"color", color},
                {"oculta", oculta}};
}

Pista Pista::deJson(const json& j) {
    Pista p;
    p.id = leerTexto(j, "id");
    p.tipo = leerTexto(j, "tipo");
    p.salida = leerTexto(j, "salida");
    p.nombre = leerTexto(j, "nombre");
    p.color = leerTexto(j, "color");
    p.oculta = leerBool(j, "oculta");
    return p;
}

json ItemEstante::aJson() const {
    json j{{"id", id}, {"tecla", tecla}, {"boton", boton}, {"color", color}};
    if (pieza) j["pieza"] = pieza->aJson();
    if (paso) j["paso"] = paso->aJson();
    return j;
}

ItemEstante ItemEstante::deJson(const json& j) {
    ItemEstante i;
    i.id = leerTexto(j, "id");
    i.tecla = leerTexto(j, "tecla");
    i.boton = leerTexto(j, "boton");
    i.color = leerTexto(j, "color");
    if (j.contains("pieza") && j["pieza"].is_object()) i.pieza = Pieza::deJson(j["pieza"]);
    if (j.contains("paso") && j["paso"].is_object()) i.paso = Paso::deJson(j["paso"]);
    return i;
}

json Disparador::aJson() const {
    return json{{"id", id}, {"cuando", cuando}, {"hacer", hacer}, {"activo", nombreEn(kVidas, activo)}};
}

Disparador Disparador::deJson(const json& j) {
    Disparador d;
    d.id = leerTexto(j, "id");
    d.cuando = leerTexto(j, "cuando");
    if (j.contains("hacer")) d.hacer = j["hacer"];
    d.activo = j.contains("activo") ? valorEn(kVidas, leerTexto(j, "activo")) : Vida::Evento;
    return d;
}

std::string errorDeHacer(const json& h) {
    using ba::i18n::traducir;
    if (!h.is_object()) return traducir("linea", "«hacer» tiene que ser {accion, salida?, id?}");
    const std::string a = leerTexto(h, "accion");
    static const std::set<std::string> sinId{"go", "hold", "soltar", "saltar"}, conId{"adlib", "insertar", "ir"};
    if (sinId.count(a)) return {};
    if (conId.count(a)) return leerTexto(h, "id").empty() ? traducir("linea", "a esa acción le falta el «id»") : std::string();
    return ba::i18n::llenar(traducir("linea", "«%1» no es una acción (go, hold, soltar, saltar, adlib, insertar, ir)"), {a});
}

json Plantilla::aJson() const { return json{{"id", id}, {"nombre", nombre}, {"pasos", listaJson(pasos)}}; }

Plantilla Plantilla::deJson(const json& j) {
    Plantilla p;
    p.id = leerTexto(j, "id");
    p.nombre = leerTexto(j, "nombre");
    p.pasos = lista<Paso>(j, "pasos", Paso::deJson);
    return p;
}

// --- Linea -------------------------------------------------------------------------

std::vector<std::string> Linea::salidasEfectivas() const {
    if (salidas.empty()) return {"principal"};
    return salidas;
}

std::string Linea::salidaDe(const Paso& p) const {
    if (!p.salida.empty()) return p.salida;
    return salidasEfectivas().front();
}

bool Linea::vaPor(const Paso& p, const std::string& s) const { return p.salida == "*" || salidaDe(p) == s; }

const Pista* Linea::pista(const std::string& id) const {
    for (const Pista& p : pistas)
        if (p.id == id) return &p;
    return nullptr;
}

const Paso* Linea::paso(const std::string& id) const {
    for (const Bloque& b : bloques)
        for (const Paso& p : b.pasos)
            if (p.id == id) return &p;
    return nullptr;
}

const Bloque* Linea::bloqueDe(const std::string& pasoId) const {
    for (const Bloque& b : bloques)
        for (const Paso& p : b.pasos)
            if (p.id == pasoId) return &b;
    return nullptr;
}

const Pieza* piezaDeEscena(const Linea& l, const Paso& p, const std::string& s) {
    for (const Pieza& z : p.piezas) {
        const Pista* pi = l.pista(z.pista);
        const bool deEscena = pi ? pi->tipo == "escena" : z.tipo == "escena";
        if (!deEscena) continue;
        // La pista de escena de otra salida (en un momento compartido, cada una la suya).
        if (pi && !pi->salida.empty() && pi->salida != s) continue;
        if (!leerTexto(z.datos, "escena").empty()) return &z;
    }
    return nullptr;
}

std::string escenaDe(const Linea& l, const Paso& p, const std::string& s) {
    const Pieza* z = piezaDeEscena(l, p, s);
    return z ? leerTexto(z->datos, "escena") : std::string();
}

json Linea::aJson() const {
    return json{{"version", kVersion},
                {"salidas", salidas},
                {"pistas", listaJson(pistas)},
                {"bloques", listaJson(bloques)},
                {"estante", listaJson(estante)},
                {"disparadores", listaJson(disparadores)},
                {"plantillas", listaJson(plantillas)}};
}

Linea Linea::deJson(const json& j) {
    Linea l;
    if (!j.is_object()) return l;
    if (j.contains("salidas") && j["salidas"].is_array())
        for (const json& s : j["salidas"])
            if (s.is_string()) l.salidas.push_back(s.get<std::string>());
    l.pistas = lista<Pista>(j, "pistas", Pista::deJson);
    l.bloques = lista<Bloque>(j, "bloques", Bloque::deJson);
    l.estante = lista<ItemEstante>(j, "estante", ItemEstante::deJson);
    l.disparadores = lista<Disparador>(j, "disparadores", Disparador::deJson);
    l.plantillas = lista<Plantilla>(j, "plantillas", Plantilla::deJson);
    return l;
}

// --- Migración v1 → v2 ---------------------------------------------------------------

bool abrePaso(Tipo t) { return t == Tipo::Escena || t == Tipo::Orador || t == Tipo::Video || t == Tipo::Espera || t == Tipo::Pausa; }

namespace {

/// La pista (por tipo) donde cae cada cue v1, según la tabla de 3.2.
const char* pistaDeTipo(Tipo t) {
    switch (t) {
        case Tipo::Escena: return "escena";
        case Tipo::Musica:
        case Tipo::Audio: return "audio";
        case Tipo::Video:
        case Tipo::Carrusel: return "medios";
        case Tipo::Teleprompter: return "prompter";
        case Tipo::Orador: return "sala";
        case Tipo::Grafico: return "graficos";
        case Tipo::Pantalla: return "pantallas";
        case Tipo::Pausa:
        case Tipo::Espera:
        case Tipo::Nota: return "notas";
    }
    return "notas";
}

} // namespace

bool pistaPorSalida(const std::string& tipo) {
    return tipo == "escena" || tipo == "graficos" || tipo == "dsk" || tipo == "audio" || tipo == "medios";
}

/// La vida que tenía cada cosa en el v1. La música seguía sonando hasta el tema
/// siguiente («Música del evento»), el rack quedaba como se lo dejaba, el gráfico
/// se iba según su `seVa`.
void vidaDeCue(const Cue& c, Pieza& p) {
    switch (c.tipo) {
        case Tipo::Musica:
        case Tipo::Audio: p.vida = Vida::Evento; return;
        case Tipo::Carrusel:
        case Tipo::Pantalla:
        case Tipo::Teleprompter: p.vida = Vida::HastaQueSeSaque; return;
        case Tipo::Grafico: {
            const std::string seVa = c.datos.value("seVa", std::string());
            if (seVa == "al-avanzar") {
                p.vida = Vida::AlAvanzar;
                return;
            }
            p.vida = Vida::HastaQueSeSaque;
            // «sola»: se va a los `salidaEn` ms. Si no dice, lo sabe el catálogo del
            // gráfico y lo decide el ejecutor (los datos quedan tal cual).
            const auto v = c.datos.find("valores");
            if (seVa == "sola" && v != c.datos.end() && v->is_object() && leerNumero(*v, "salidaEn") > 0) {
                p.dura.modo = Dura::Modo::Ms;
                p.dura.ms = leerNumero(*v, "salidaEn");
            }
            return;
        }
        default: p.vida = Vida::Paso; return;
    }
}

Linea migrar(const Evento& v1) {
    Linea l;
    l.salidas = {"principal"};
    const std::string salida = "principal";
    std::set<std::string> pistasUsadas;
    auto pistaPara = [&](Tipo t) {
        const std::string tipo = pistaDeTipo(t);
        pistasUsadas.insert(tipo);
        return pistaPorSalida(tipo) ? tipo + "-" + salida : tipo;
    };
    pistasUsadas.insert("escena"); // obligatoria: una por salida

    for (const ba::evento::Bloque& b1 : v1.bloques) {
        Bloque b;
        b.id = b1.id;
        b.nombre = b1.nombre;
        b.nota = b1.nota;
        Paso* actual = nullptr;
        int64_t enPasoMs = 0;         // lo que va del paso hasta el cue de ahora
        bool anteriorEncadena = true; // el cue de antes encadenaba solo (o es el que abre)
        for (const Cue& c : b1.cues) {
            if (abrePaso(c.tipo) || actual == nullptr) {
                Paso p;
                if (abrePaso(c.tipo)) {
                    p.id = c.id;
                    p.nombre = c.texto;
                } else {
                    // Un bloque que arranca con música o un gráfico: el paso no
                    // tiene escena propia, pero el GO sigue siendo el del cue.
                    p.id = b1.id + "-inicio";
                    p.nombre = b1.nombre;
                }
                p.salida = salida;
                if (c.tipo == Tipo::Espera) {
                    // Espera a la persona: no sabe cuánto dura (abierto) y nunca se va sola.
                    p.notaOperador = c.texto;
                    p.fin.modo = ModoFin::Abierto;
                }
                if (c.tipo == Tipo::Pausa) p.continuar = Continuar::Sigue;
                b.pasos.push_back(std::move(p));
                actual = &b.pasos.back();
                enPasoMs = 0;
                anteriorEncadena = true;
            }
            // Pausa y espera no hacen nada en el aire: son el paso mismo.
            if (c.tipo != Tipo::Pausa && c.tipo != Tipo::Espera) {
                Pieza z;
                z.id = abrePaso(c.tipo) ? c.id + "-" + nombreDeTipo(c.tipo) : c.id;
                z.pista = pistaPara(c.tipo);
                z.tipo = nombreDeTipo(c.tipo);
                z.texto = c.texto;
                z.datos = c.datos;
                // Donde estaba en el v1 (la suma de lo de antes). Si esperaba el clic
                // queda como adlib del paso, pero con su lugar en el plan: así la
                // escaleta lo muestra donde iba y el guion de cues se vuelve a leer
                // igual (proyectar).
                z.desde.ms = enPasoMs;
                if (!anteriorEncadena) z.desde.modo = Ancla::Modo::Manual;
                vidaDeCue(c, z);
                actual->piezas.push_back(std::move(z));
            }
            enPasoMs += int64_t{std::max(0, c.duracionSeg)} * 1000;
            actual->duracionPrevistaSeg += std::max(0, c.duracionSeg);
            actual->fin.duracionSeg = actual->duracionPrevistaSeg;
            // El último cue del paso decide si el siguiente paso entra solo.
            // (Una espera nunca pasa sola, ni marcada automática: como en el v1.)
            if (c.tipo != Tipo::Pausa) actual->continuar = c.automatico && c.tipo != Tipo::Espera ? Continuar::Sigue : Continuar::No;
            // La pausa siempre sigue sola: lo que viene después en su paso va a tiempo.
            anteriorEncadena = c.automatico || c.tipo == Tipo::Pausa;
        }
        l.bloques.push_back(std::move(b));
    }
    // Las pistas, en el orden de 3.2 y solo las que se usan.
    for (const char* tipo : {"escena", "graficos", "audio", "medios", "prompter", "sala", "pantallas", "notas"}) {
        if (!pistasUsadas.count(tipo)) continue;
        Pista p;
        p.tipo = tipo;
        p.salida = pistaPorSalida(tipo) ? salida : std::string();
        p.id = pistaPorSalida(tipo) ? std::string(tipo) + "-" + salida : std::string(tipo);
        p.nombre = tipo;
        l.pistas.push_back(std::move(p));
    }
    return l;
}

// --- Instancias y estado vivo --------------------------------------------------------

json Instancia::aJson() const {
    return json{{"salida", salida}, {"paso", paso}, {"desdeMs", desdeMs}, {"hastaMs", hastaMs}, {"comoEntro", nombreDe(comoEntro)}};
}

Instancia Instancia::deJson(const json& j) {
    Instancia i;
    i.salida = leerTexto(j, "salida");
    i.paso = leerTexto(j, "paso");
    i.desdeMs = leerNumero(j, "desdeMs");
    i.hastaMs = leerNumero(j, "hastaMs");
    i.comoEntro = valorEn(kComo, leerTexto(j, "comoEntro"));
    return i;
}

json EstadoAire::aJson() const {
    json ss = json::array();
    for (const SalidaAire& s : salidas)
        ss.push_back(json{{"salida", s.salida},
                          {"alAire", s.alAire},
                          {"alAireDesdeMs", s.alAireDesdeMs},
                          {"proximo", s.proximo},
                          {"hold", s.hold}});
    json ps = json::array();
    for (const PiezaAire& p : piezas)
        ps.push_back(json{{"pieza", p.pieza}, {"salida", p.salida}, {"desdeMs", p.desdeMs}});
    json sal = json::array();
    for (const RefPaso& r : salteados)
        sal.push_back(json{{"salida", r.salida}, {"paso", r.paso}});
    return json{{"version", 1},       {"escritoMs", escritoMs},
                {"salidas", ss},      {"piezas", ps},
                {"instancias", listaJson(instancias)}, {"salteados", sal},
                {"pideDescripcion", pideDescripcion}, {"descripcion", descripcion}};
}

EstadoAire EstadoAire::deJson(const json& j) {
    EstadoAire e;
    if (!j.is_object()) return e;
    e.escritoMs = leerNumero(j, "escritoMs");
    e.salidas = lista<SalidaAire>(j, "salidas", [](const json& x) {
        SalidaAire s;
        s.salida = leerTexto(x, "salida");
        s.alAire = leerTexto(x, "alAire");
        s.alAireDesdeMs = leerNumero(x, "alAireDesdeMs");
        s.proximo = leerTexto(x, "proximo");
        s.hold = leerBool(x, "hold");
        return s;
    });
    e.piezas = lista<PiezaAire>(j, "piezas", [](const json& x) {
        return PiezaAire{leerTexto(x, "pieza"), leerTexto(x, "salida"), leerNumero(x, "desdeMs")};
    });
    e.instancias = lista<Instancia>(j, "instancias", Instancia::deJson);
    e.salteados = lista<RefPaso>(j, "salteados", [](const json& x) { return RefPaso{leerTexto(x, "salida"), leerTexto(x, "paso")}; });
    e.pideDescripcion = leerBool(j, "pideDescripcion");
    e.descripcion = leerTexto(j, "descripcion");
    return e;
}

// --- Resolvedor ------------------------------------------------------------------------

const PasoResuelto* SalidaResuelta::paso(const std::string& id) const {
    for (const PasoResuelto& p : pasos)
        if (p.paso == id) return &p;
    return nullptr;
}

const SalidaResuelta* Resolucion::salida(const std::string& id) const {
    for (const SalidaResuelta& s : salidas)
        if (s.salida == id) return &s;
    return nullptr;
}

std::vector<Activa> Resolucion::estadoEn(int64_t t, bool soloEmitidas) const {
    std::map<std::string, const PiezaResuelta*> gana;
    for (const PiezaResuelta& p : piezas) {
        if (!p.sigueEn(t) || (soloEmitidas && !p.emitida)) continue;
        const PiezaResuelta*& g = gana[p.pista];
        // Como en SuperFly: prioridad; si empatan, la que arrancó más tarde; si
        // arrancaron juntas, la que está más abajo en el modelo.
        if (g == nullptr || std::tie(p.prioridad, p.desdeMs, p.orden) > std::tie(g->prioridad, g->desdeMs, g->orden)) g = &p;
    }
    std::vector<Activa> v;
    for (const auto& [pista, p] : gana)
        v.push_back({pista, *p});
    return v;
}

namespace {

constexpr int64_t kDiaMs = 24LL * 3600 * 1000;

/// Un paso en la secuencia de una salida.
struct Item {
    const Bloque* b;
    const Paso* p;
    std::size_t bloque; ///< índice del bloque en el modelo
};

/// Lo que el resolvedor arma por salida además de lo que devuelve.
struct Trabajo {
    std::vector<Item> seq;
    std::vector<int> futuras;   ///< índices de `seq`, en el orden en que van a entrar
    int alAire = -1;
    std::vector<bool> alcanzaSolo; ///< por índice: entra sin que nadie apriete nada
    std::vector<bool> trasAlAire;  ///< por índice: entra cuando TERMINA el que está al aire
};

std::string clave(const std::string& salida, const std::string& paso) { return salida + "/" + paso; }

class Resolvedor {
   public:
    Resolvedor(const Linea& l, const Entrada& e) : l_(l), e_(e), salidas_(l.salidasEfectivas()) {
        int n = 0;
        for (const Bloque& b : l.bloques)
            for (const Paso& p : b.pasos)
                for (const Pieza& z : p.piezas)
                    orden_[&z] = n++;
    }

    Resolucion resolver() {
        Resolucion r;
        std::vector<Trabajo> trabajos;
        // Las anclas entre salidas se resuelven en pasadas: cada una usa los
        // frentes de la anterior. Sin ciclos, en tantas pasadas como salidas
        // queda fijo; con un ciclo (lo marca revisar()), se corta igual.
        for (std::size_t pasada = 0; pasada <= salidas_.size(); ++pasada) {
            r.salidas.clear();
            trabajos.clear();
            for (const std::string& s : salidas_) {
                trabajos.emplace_back();
                r.salidas.push_back(resolverSalida(s, trabajos.back()));
            }
            std::map<std::string, int64_t> nuevas;
            for (std::size_t i = 0; i < r.salidas.size(); ++i)
                for (const PasoResuelto& p : r.salidas[i].pasos)
                    if (p.estado != EstadoPaso::Salteado && !p.cortadoPorHora) nuevas[clave(r.salidas[i].salida, p.paso)] = p.frenteMs;
            if (nuevas == anclas_) break;
            anclas_ = std::move(nuevas);
        }
        for (std::size_t i = 0; i < r.salidas.size(); ++i)
            generarPiezas(r.salidas[i], trabajos[i], r.piezas);
        for (std::size_t i = 0; i < r.salidas.size(); ++i)
            proximos(r.salidas[i], trabajos[i], r);
        std::stable_sort(r.proximosEventos.begin(), r.proximosEventos.end(),
                         [](const ProximoEvento& a, const ProximoEvento& b) { return a.ms < b.ms; });
        conflictos(r, trabajos);
        return r;
    }

   private:
    // --- Tiempos ----------------------------------------------------------------

    std::optional<int64_t> horaAMs(const std::string& hhmm) const {
        if (!horaValida(hhmm)) return std::nullopt;
        const int64_t h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
        const int64_t m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
        int64_t ms = e_.medianocheMs + h * 3600000 + m * 60000;
        if (e_.inicioPlaneadoMs > 0 && ms < e_.inicioPlaneadoMs) ms += kDiaMs;
        return ms;
    }

    const EstadoCondicion* condicion(const Condicion& c) const {
        const auto it = e_.condiciones.find(c);
        return it == e_.condiciones.end() ? nullptr : &it->second;
    }

    bool enHold(const std::string& s, const std::string& paso) const {
        for (const RefPaso& h : e_.holds)
            if (h.salida == s && (h.paso.empty() || h.paso == paso)) return true;
        return false;
    }

    bool salteado(const std::string& s, const std::string& paso) const {
        for (const RefPaso& h : e_.salteados)
            if (h.salida == s && h.paso == paso) return true;
        return false;
    }

    /// La pieza que manda en un paso que dura «lo que dure el medio».
    static const Pieza* piezaPrincipal(const Paso& p) {
        for (const Pieza& z : p.piezas)
            if (z.tipo == "video" || z.tipo == "musica" || z.tipo == "medios" || z.tipo == "playlist") return &z;
        return nullptr;
    }

    /// Lo que dura el paso arrancando en `frente`, con lo que se sabe hasta ahora.
    /// `estimado` queda en true si es un supuesto (regla 5).
    int64_t duracion(const Paso& p, int64_t frente, bool& estimado) const {
        const int64_t plan = p.duracionPlanMs();
        switch (p.fin.modo) {
            case ModoFin::Duracion: return plan;
            case ModoFin::Abierto: estimado = true; return plan;
            case ModoFin::AlTerminar: {
                const Pieza* z = piezaPrincipal(p);
                const EstadoCondicion* c = z ? condicion("medio:" + z->id + ".termino") : nullptr;
                if (c && c->cumple && c->desdeMs >= frente) return c->desdeMs - frente;
                estimado = true;
                return plan;
            }
            case ModoFin::Mientras: {
                const EstadoCondicion* c = condicion(p.fin.mientras);
                if (c && !c->cumple && c->desdeMs >= frente) return c->desdeMs - frente;
                estimado = true;
                return plan;
            }
        }
        return plan;
    }

    /// ¿El paso arrancado en `frente` todavía no sabe cuándo termina? Abierto, o un
    /// «mientras» cuya condición no dejó de cumplirse desde que entró.
    bool sinFinConocido(const Paso& p, int64_t frente) const {
        if (p.fin.modo == ModoFin::Abierto) return true;
        if (p.fin.modo != ModoFin::Mientras) return false;
        const EstadoCondicion* c = condicion(p.fin.mientras);
        return !(c && !c->cumple && c->desdeMs >= frente);
    }

    SalidaResuelta resolverSalida(const std::string& s, Trabajo& w) {
        SalidaResuelta out;
        out.salida = s;
        const int64_t ahora = e_.ahoraMs;
        for (std::size_t bi = 0; bi < l_.bloques.size(); ++bi)
            for (const Paso& p : l_.bloques[bi].pasos)
                if (l_.vaPor(p, s)) w.seq.push_back({&l_.bloques[bi], &p, bi});
        const int n = static_cast<int>(w.seq.size());
        out.pasos.resize(w.seq.size());
        w.alcanzaSolo.assign(w.seq.size(), false);
        w.trasAlAire.assign(w.seq.size(), false);
        auto indice = [&](const std::string& id) {
            for (int i = 0; i < n; ++i)
                if (w.seq[i].p->id == id) return i;
            return -1;
        };
        for (int i = 0; i < n; ++i) {
            out.pasos[i].paso = w.seq[i].p->id;
            out.pasos[i].bloque = w.seq[i].b->id;
        }

        // Lo emitido: la última instancia de cada paso, y el cursor.
        std::vector<Instancia> inst;
        for (const Instancia& i : e_.instancias)
            if (i.salida == s && indice(i.paso) >= 0) inst.push_back(i);
        std::stable_sort(inst.begin(), inst.end(), [](const Instancia& a, const Instancia& b) { return a.desdeMs < b.desdeMs; });
        // Una instancia que quedó abierta y no es la última se cerró cuando entró la siguiente.
        for (std::size_t i = 0; i + 1 < inst.size(); ++i)
            if (inst[i].hastaMs == 0) inst[i].hastaMs = inst[i + 1].desdeMs;
        std::vector<const Instancia*> ultima(w.seq.size(), nullptr);
        for (const Instancia& i : inst)
            ultima[indice(i.paso)] = &i;
        int cursor = -1;
        if (!inst.empty()) {
            cursor = indice(inst.back().paso);
            if (inst.back().hastaMs == 0) w.alAire = cursor;
        }

        // Lo que viene: desde el próximo (movido a mano o el que sigue al cursor).
        int desde = cursor + 1;
        for (const RefPaso& r : e_.proximos)
            if (r.salida == s && indice(r.paso) >= 0) desde = indice(r.paso);
        for (int i = desde; i < n; ++i) {
            if (i == w.alAire) continue;
            if (salteado(s, w.seq[i].p->id)) continue;
            w.futuras.push_back(i);
        }
        std::vector<bool> esFutura(w.seq.size(), false);
        for (int i : w.futuras)
            esFutura[i] = true;
        for (int i = 0; i < n; ++i) {
            if (esFutura[i] || i == w.alAire) continue;
            PasoResuelto& r = out.pasos[i];
            // Emitido y no vuelve a venir: pasado. Lo demás (saltado con «Ir a» o
            // con «Saltar») no entró ni va a entrar.
            if (ultima[i]) {
                r.estado = EstadoPaso::Pasado;
                r.frenteMs = ultima[i]->desdeMs;
                r.finMs = ultima[i]->hastaMs;
                r.como = ultima[i]->comoEntro;
            } else {
                r.estado = EstadoPaso::Salteado;
            }
        }

        // El que está al aire: si se pasó o es abierto, empuja todo (regla 2).
        int prev = -1;
        int64_t prevFin = 0;
        bool prevEstimado = false;
        int64_t arranque = std::max(ahora, e_.inicioPlaneadoMs);
        if (w.alAire >= 0) {
            PasoResuelto& r = out.pasos[w.alAire];
            const Paso& p = *w.seq[w.alAire].p;
            r.estado = EstadoPaso::AlAire;
            r.frenteMs = ultima[w.alAire]->desdeMs;
            r.como = ultima[w.alAire]->comoEntro;
            r.finMs = std::max(r.frenteMs + duracion(p, r.frenteMs, r.estimado), ahora);
            r.enHold = enHold(s, p.id);
            prev = w.alAire;
            prevFin = r.finMs;
            prevEstimado = r.estimado;
        } else if (cursor >= 0) {
            prev = cursor;
            prevFin = std::max(ultima[cursor]->hastaMs, ahora);
        } else {
            prevFin = arranque;
        }

        // Los que vienen, en orden.
        std::vector<int> hechos; // los futuros ya calculados (para el «cortar»)
        std::vector<std::optional<int64_t>> horaFija(w.seq.size());
        for (int k : w.futuras) {
            const Item& it = w.seq[k];
            const Paso& p = *it.p;
            PasoResuelto& r = out.pasos[k];
            const bool primeroDelBloque = std::none_of(w.seq.begin(), w.seq.begin() + k, [&](const Item& x) { return x.b == it.b; });
            Inicio ini = p.inicio;
            if (ini.modo == ModoInicio::Go && primeroDelBloque && it.b->inicio) ini = *it.b->inicio;
            const Paso* pp = prev >= 0 ? w.seq[prev].p : nullptr;
            const bool prevHold = prev >= 0 && enHold(s, pp->id);
            // El `continuar` del anterior completa un GO: sigue = tras, con = con.
            if (ini.modo == ModoInicio::Go && pp && pp->continuar != Continuar::No) {
                ini.modo = pp->continuar == Continuar::Sigue ? ModoInicio::Tras : ModoInicio::Con;
                ini.de = "anterior";
                ini.masMs = pp->postEsperaMs;
            }
            // Un HOLD congela lo automático: lo que venía solo pasa a esperar el GO
            // (el hard start se sigue mostrando, pero ya no corta).
            if (prevHold) {
                if (ini.modo == ModoInicio::Hora) ini.politica = Politica::Avisar;
                else ini.modo = ModoInicio::Go;
            }
            // Una ancla a la misma salida es un «con» de ese paso.
            if (ini.modo == ModoInicio::Ancla && ini.salida == s) {
                ini.modo = ModoInicio::Con;
                ini.de = ini.paso;
            }

            int64_t t = prevFin;
            bool propio = false; // estimado por algo de este paso
            bool fijo = false;   // la hora manda: no hereda lo estimado de antes
            r.automatico = true;
            r.como = ComoEntro::Auto;
            switch (ini.modo) {
                case ModoInicio::Go:
                    r.automatico = false;
                    r.como = ComoEntro::Go;
                    break;
                case ModoInicio::Tras:
                case ModoInicio::Con: {
                    int ref = prev;
                    if (ini.de != "anterior") {
                        ref = indice(ini.de);
                        // Solo vale un paso que ya tiene tiempos: emitido, al aire o ya calculado.
                        const bool conTiempos = ref >= 0 && (ref == w.alAire || out.pasos[ref].estado == EstadoPaso::Pasado ||
                                                             std::find(hechos.begin(), hechos.end(), ref) != hechos.end());
                        if (!conTiempos) ref = -1;
                    }
                    if (ref < 0) {
                        if (ini.de == "anterior") {
                            // El primero de todos: no hay de quién seguir, espera el GO.
                            r.automatico = false;
                            r.como = ComoEntro::Go;
                        } else {
                            propio = true; // una referencia que no sirve (la marca revisar())
                        }
                        break;
                    }
                    const PasoResuelto& rr = out.pasos[ref];
                    if (ini.modo == ModoInicio::Tras) t = rr.finMs + w.seq[ref].p->postEsperaMs;
                    else t = rr.frenteMs + ini.masMs;
                    if (ini.modo == ModoInicio::Tras && ref == w.alAire) w.trasAlAire[k] = true;
                    if (ref != prev && rr.estimado) propio = true;
                    break;
                }
                case ModoInicio::Hora: {
                    const std::optional<int64_t> h = horaAMs(ini.hora);
                    if (!h) {
                        r.automatico = false;
                        r.como = ComoEntro::Go;
                        break;
                    }
                    horaFija[k] = *h;
                    HardStart hs{s, p.id, *h, ini.politica, *h - prevFin};
                    out.hardStarts.push_back(hs);
                    r.como = ini.politica == Politica::Avisar ? ComoEntro::Go : ComoEntro::Hora;
                    r.automatico = ini.politica != Politica::Avisar;
                    t = ini.politica == Politica::Cortar ? *h : std::max(*h, prevFin);
                    fijo = t == *h;
                    break;
                }
                case ModoInicio::Cuando: {
                    r.como = ComoEntro::Cuando;
                    const EstadoCondicion* c = condicion(ini.condicion);
                    if (c && c->cumple) {
                        // Lo de afuera corta al que está al aire solo si este es el
                        // próximo; más adelante espera su turno (ver alcanzaSolo).
                        t = hechos.empty() ? c->desdeMs : std::max(c->desdeMs, prevFin);
                        fijo = true;
                    } else {
                        propio = true;
                    }
                    break;
                }
                case ModoInicio::Ancla: {
                    r.como = ComoEntro::Ancla;
                    const auto a = anclas_.find(clave(ini.salida, ini.paso));
                    if (a != anclas_.end()) {
                        t = a->second + ini.masMs;
                    } else {
                        propio = true;
                    }
                    break;
                }
            }
            if (ini.modo != ModoInicio::Hora) t += p.preEsperaMs;
            // Lo que todavía no entró no puede entrar en el pasado.
            r.frenteMs = std::max(t, ahora);
            r.enHold = enHold(s, p.id);

            // «Cortar»: lo de antes que no llega a la hora no entra, y el que está
            // en el medio se corta ahí.
            if (ini.modo == ModoInicio::Hora && ini.politica == Politica::Cortar) {
                for (auto j = hechos.rbegin(); j != hechos.rend(); ++j) {
                    PasoResuelto& q = out.pasos[*j];
                    if (q.frenteMs >= r.frenteMs) {
                        q.cortadoPorHora = true;
                        q.frenteMs = q.finMs = r.frenteMs;
                    } else {
                        q.finMs = std::min(q.finMs, r.frenteMs);
                        break;
                    }
                }
                if (w.alAire >= 0 && out.pasos[w.alAire].finMs > r.frenteMs)
                    out.pasos[w.alAire].finMs = std::max(r.frenteMs, ahora);
            }

            r.estimado = propio || (prevEstimado && !fijo);
            r.finMs = r.frenteMs + duracion(p, r.frenteMs, r.estimado);
            // Un paso por vez en la salida: el anterior sale cuando entra este.
            if (prev >= 0 && (prev == w.alAire || esFutura[prev])) {
                PasoResuelto& q = out.pasos[prev];
                if (!q.cortadoPorHora) q.finMs = std::max(r.frenteMs, q.frenteMs);
            }
            r.estado = hechos.empty() ? EstadoPaso::Proximo : EstadoPaso::Futuro;
            hechos.push_back(k);
            prev = k;
            prevFin = r.finMs;
            prevEstimado = r.estimado;
        }

        // Fin de la salida, fin planeado y over/under del evento (regla 4).
        out.finMs = ahora;
        for (const PasoResuelto& p : out.pasos)
            if (p.estado != EstadoPaso::Salteado && !p.cortadoPorHora) out.finMs = std::max(out.finMs, p.finMs);
        if (e_.finPlaneadoMs > 0) {
            out.finPlaneadoMs = e_.finPlaneadoMs;
        } else if (e_.inicioPlaneadoMs > 0) {
            out.finPlaneadoMs = e_.inicioPlaneadoMs;
            for (const Item& it : w.seq)
                out.finPlaneadoMs += it.p->duracionPlanMs();
        }
        if (out.finPlaneadoMs > 0) out.overUnderMs = out.finMs - out.finPlaneadoMs;

        // Over/under por bloque contra su presupuesto (o contra lo previsto).
        for (std::size_t bi = 0; bi < l_.bloques.size(); ++bi) {
            BloqueResuelto br;
            br.bloque = l_.bloques[bi].id;
            bool hay = false, alguno = false;
            for (int i = 0; i < n; ++i) {
                if (w.seq[i].bloque != bi) continue;
                alguno = true;
                br.presupuestoMs += w.seq[i].p->duracionPlanMs();
                const PasoResuelto& p = out.pasos[i];
                if (p.estado == EstadoPaso::Salteado || p.cortadoPorHora) continue;
                br.desdeMs = hay ? std::min(br.desdeMs, p.frenteMs) : p.frenteMs;
                br.hastaMs = hay ? std::max(br.hastaMs, p.finMs) : p.finMs;
                hay = true;
            }
            if (!alguno) continue;
            if (l_.bloques[bi].presupuestoSeg) br.presupuestoMs = int64_t{*l_.bloques[bi].presupuestoSeg} * 1000;
            br.overUnderMs = (br.hastaMs - br.desdeMs) - br.presupuestoMs;
            out.bloques.push_back(br);
        }

        // Back times, de atrás para adelante: lo último que puede arrancar cada paso
        // para llegar al próximo hard start, a un back time pisado o al fin (regla 6).
        std::optional<int64_t> limite;
        if (out.finPlaneadoMs > 0) limite = out.finPlaneadoMs;
        std::vector<int> vivos = w.futuras;
        if (w.alAire >= 0) vivos.insert(vivos.begin(), w.alAire);
        for (auto j = vivos.rbegin(); j != vivos.rend(); ++j) {
            PasoResuelto& r = out.pasos[*j];
            if (r.cortadoPorHora) continue;
            const Paso& p = *w.seq[*j].p;
            std::optional<int64_t> back;
            if (limite) back = *limite - p.duracionPlanMs();
            if (horaFija[*j]) back = back ? std::min(*back, *horaFija[*j]) : *horaFija[*j];
            const auto pis = e_.backTimesPisados.find(p.id);
            if (pis != e_.backTimesPisados.end()) {
                back = back ? std::min(*back, pis->second) : pis->second;
                r.backPisado = true;
            }
            r.backMs = back;
            r.tarde = back && r.frenteMs > *back && r.estado != EstadoPaso::AlAire;
            if (back) limite = back;
        }

        if (w.alAire >= 0) out.alAire = w.seq[w.alAire].p->id;
        if (!w.futuras.empty()) out.proximo = w.seq[w.futuras.front()].p->id;

        // Qué entra solo: lo que se encadena desde lo que está al aire sin un GO en
        // el medio; un hard start o un ancla entran solos igual. Una condición, solo
        // si es el próximo: lo de afuera («cerró la votación») no se saltea los
        // pasos que tiene adelante; cuando le llega el turno, entra si ya se cumple.
        bool cadena = w.alAire >= 0 && !out.pasos[w.alAire].enHold;
        // Un paso al aire que no sabe cuándo termina (abierto, o «mientras» que se sigue
        // cumpliendo) no se termina por la hora: lo que va «tras» él espera.
        const bool sinFin = w.alAire >= 0 && sinFinConocido(*w.seq[w.alAire].p, out.pasos[w.alAire].frenteMs);
        for (int k : w.futuras) {
            const PasoResuelto& r = out.pasos[k];
            if (r.cortadoPorHora) continue;
            if (sinFin && w.trasAlAire[k]) {
                cadena = false;
                continue;
            }
            const bool independiente = r.como == ComoEntro::Hora || r.como == ComoEntro::Ancla ||
                                       (r.como == ComoEntro::Cuando && k == w.futuras.front());
            w.alcanzaSolo[k] = r.automatico && (cadena || independiente);
            cadena = w.alcanzaSolo[k] && !r.enHold;
        }
        return out;
    }

    // --- Piezas -------------------------------------------------------------------

    void generarPiezas(const SalidaResuelta& sr, const Trabajo& w, std::vector<PiezaResuelta>& out) {
        const std::string& s = sr.salida;
        std::map<std::size_t, int64_t> finDeBloque;
        for (std::size_t i = 0; i < w.seq.size(); ++i) {
            const PasoResuelto& r = sr.pasos[i];
            if (r.estado == EstadoPaso::Salteado || r.cortadoPorHora) continue;
            int64_t& f = finDeBloque[w.seq[i].bloque];
            f = std::max(f, r.finMs);
        }
        for (std::size_t i = 0; i < w.seq.size(); ++i) {
            const PasoResuelto& r = sr.pasos[i];
            if (r.estado == EstadoPaso::Salteado || r.cortadoPorHora) continue;
            const Paso& p = *w.seq[i].p;
            for (const Pieza& z : p.piezas) {
                const Pista* pi = l_.pista(z.pista);
                const bool global = pi == nullptr || pi->salida.empty();
                if (global) {
                    // Una pista global en un paso "*" es de todas: va una sola vez.
                    if (!generadas_.insert(p.id + "/" + z.id).second) continue;
                } else if (p.salida == "*" && pi->salida != s) {
                    continue; // la pista de otra salida, en un momento compartido
                }
                PiezaResuelta pr;
                pr.pieza = z.id;
                pr.paso = p.id;
                pr.bloque = w.seq[i].b->id;
                pr.salida = s;
                pr.pista = z.pista;
                pr.compartida = global && p.salida == "*";
                pr.prioridad = z.prioridad;
                pr.orden = orden_[&z];
                pr.estimado = r.estimado;
                // Lo que ya arrancó de verdad tiene su hora, sea cual sea su `desde`.
                const auto real = r.estado == EstadoPaso::AlAire || r.estado == EstadoPaso::Pasado
                                      ? e_.arranques.find(claveArranque(s, p.id, z.id))
                                      : e_.arranques.end();
                if (real != e_.arranques.end() && real->second >= r.frenteMs) {
                    pr.desdeMs = real->second;
                } else switch (z.desde.modo) {
                    case Ancla::Modo::Ms: pr.desdeMs = r.frenteMs + z.desde.ms; break;
                    case Ancla::Modo::AntesDelFin:
                        pr.desdeMs = std::max(r.frenteMs, r.finMs - z.desde.ms);
                        if (p.fin.modo != ModoFin::Duracion || r.estado == EstadoPaso::AlAire) pr.estimado = true;
                        break;
                    case Ancla::Modo::Manual: continue; // un adlib del paso: no tiene hora
                    case Ancla::Modo::Cuando: {
                        const EstadoCondicion* c = condicion(z.desde.cuando);
                        if (!c || !c->cumple) continue; // sin hora conocida, no se programa
                        pr.desdeMs = std::max(r.frenteMs, c->desdeMs);
                        break;
                    }
                }
                int64_t vida = r.finMs;
                switch (z.vida) {
                    case Vida::Paso:
                    case Vida::AlAvanzar: vida = r.finMs; break;
                    case Vida::Bloque: vida = finDeBloque[w.seq[i].bloque]; break;
                    case Vida::Evento: vida = sr.finMs; break;
                    case Vida::HastaQueSeSaque: vida = kNunca; break;
                }
                pr.hastaMs = vida;
                if (z.dura.modo == Dura::Modo::Ms) {
                    pr.hastaMs = std::min(vida, pr.desdeMs + z.dura.ms);
                } else if (z.dura.modo == Dura::Modo::AlTerminar) {
                    const EstadoCondicion* c = condicion("medio:" + z.id + ".termino");
                    if (c && c->cumple && c->desdeMs >= pr.desdeMs) pr.hastaMs = std::min(vida, c->desdeMs);
                    else pr.estimado = true;
                }
                pr.hastaAbierto = r.estado == EstadoPaso::AlAire && pr.hastaMs == vida && vida == e_.ahoraMs;
                pr.emitida = r.estado == EstadoPaso::AlAire || r.estado == EstadoPaso::Pasado;
                // Lo que arrancaría cuando su paso ya se fue no pasa nunca.
                if (pr.desdeMs >= vida) continue;
                out.push_back(std::move(pr));
            }
        }
    }

    void proximos(const SalidaResuelta& sr, const Trabajo& w, Resolucion& r) const {
        const int64_t ahora = e_.ahoraMs;
        std::set<std::string> alcanzados;
        if (w.alAire >= 0) alcanzados.insert(w.seq[w.alAire].p->id);
        for (int k : w.futuras) {
            if (!w.alcanzaSolo[k]) continue;
            const PasoResuelto& p = sr.pasos[k];
            alcanzados.insert(p.paso);
            ProximoEvento ev;
            ev.que = ProximoEvento::Que::EntraPaso;
            ev.ms = p.frenteMs;
            ev.salida = sr.salida;
            ev.paso = p.paso;
            ev.como = p.como;
            ev.estimado = p.estimado;
            r.proximosEventos.push_back(ev);
        }
        for (const PiezaResuelta& z : r.piezas) {
            if (z.salida != sr.salida || !alcanzados.count(z.paso)) continue;
            const Paso* p = l_.paso(z.paso);
            const Pieza* pz = nullptr;
            for (const Pieza& x : p->piezas)
                if (x.id == z.pieza) pz = &x;
            ProximoEvento ev;
            ev.salida = z.salida;
            ev.paso = z.paso;
            ev.pieza = z.pieza;
            ev.estimado = z.estimado;
            ev.como = ComoEntro::Auto;
            if (z.desdeMs >= ahora) {
                ev.que = ProximoEvento::Que::EntraPieza;
                ev.ms = z.desdeMs;
                r.proximosEventos.push_back(ev);
            }
            // La salida propia de la pieza (no la de su paso: esa la da el paso).
            if (pz && pz->dura.modo == Dura::Modo::Ms && z.hastaMs > ahora && z.hastaMs != kNunca) {
                ev.que = ProximoEvento::Que::SalePieza;
                ev.ms = z.hastaMs;
                r.proximosEventos.push_back(ev);
            }
        }
    }

    // --- Conflictos entre salidas (3.4) ---------------------------------------------

    /// Qué hay al aire y qué está armado en la salida en el instante t.
    std::pair<const Paso*, const Paso*> enSalida(const SalidaResuelta& sr, const Trabajo& w, int64_t t) const {
        int aire = -1;
        for (std::size_t i = 0; i < w.seq.size(); ++i) {
            const PasoResuelto& r = sr.pasos[i];
            if (r.estado == EstadoPaso::Salteado || r.cortadoPorHora) continue;
            if (r.frenteMs <= t && t < r.finMs) aire = static_cast<int>(i);
        }
        if (aire < 0) return {nullptr, nullptr};
        // El armado es el que entra después (el de frente más cercano).
        int armado = -1;
        for (std::size_t i = 0; i < w.seq.size(); ++i) {
            const PasoResuelto& r = sr.pasos[i];
            if (r.estado == EstadoPaso::Salteado || r.cortadoPorHora || static_cast<int>(i) == aire) continue;
            if (r.frenteMs >= sr.pasos[aire].finMs && (armado < 0 || r.frenteMs < sr.pasos[armado].frenteMs))
                armado = static_cast<int>(i);
        }
        return {w.seq[aire].p, armado >= 0 ? w.seq[armado].p : nullptr};
    }

    static std::string comoTexto(const json& v) { return v.is_string() ? v.get<std::string>() : v.dump(); }

    void conflictos(Resolucion& r, const std::vector<Trabajo>& trabajos) const {
        auto tipoDe = [&](const PiezaResuelta& z) -> std::pair<const Pieza*, std::string> {
            const Paso* p = l_.paso(z.paso);
            for (const Pieza& x : p->piezas)
                if (x.id == z.pieza) {
                    const Pista* pi = l_.pista(x.pista);
                    return {&x, x.tipo.empty() && pi ? pi->tipo : x.tipo};
                }
            return {nullptr, {}};
        };
        // PTZ que mueve una cámara que otra salida tiene al aire o armada.
        if (e_.camarasDeEscena) {
            for (const PiezaResuelta& z : r.piezas) {
                const auto [pz, tipo] = tipoDe(z);
                if (!pz || tipo != "ptz" || !pz->datos.contains("camara")) continue;
                const std::string camara = comoTexto(pz->datos["camara"]);
                for (std::size_t i = 0; i < r.salidas.size(); ++i) {
                    const SalidaResuelta& o = r.salidas[i];
                    if (z.compartida || o.salida == z.salida) continue;
                    const auto [aire, armado] = enSalida(o, trabajos[i], z.desdeMs);
                    for (const auto& [paso, armada] : {std::pair{aire, false}, std::pair{armado, true}}) {
                        if (!paso) continue;
                        const std::string escena = linea::escenaDe(l_, *paso, o.salida);
                        if (escena.empty()) continue;
                        const auto cams = e_.camarasDeEscena(escena);
                        if (std::find(cams.begin(), cams.end(), camara) == cams.end()) continue;
                        Conflicto c;
                        c.tipo = Conflicto::Tipo::Ptz;
                        c.ms = z.desdeMs;
                        c.salida = z.salida;
                        c.pieza = z.pieza;
                        c.otraSalida = o.salida;
                        c.otroPaso = paso->id;
                        c.armada = armada;
                        c.recurso = camara;
                        r.conflictos.push_back(c);
                        break; // al aire gana: no se avisa dos veces la misma
                    }
                }
            }
        }
        // La misma fuente de gráfico con contenidos distintos en dos salidas a la vez.
        for (std::size_t a = 0; a < r.piezas.size(); ++a) {
            const auto [pa, ta] = tipoDe(r.piezas[a]);
            if (!pa || ta != "grafico") continue;
            const std::string fuente = leerTexto(pa->datos, "fuente");
            if (fuente.empty()) continue;
            for (std::size_t b = a + 1; b < r.piezas.size(); ++b) {
                const PiezaResuelta& za = r.piezas[a];
                const PiezaResuelta& zb = r.piezas[b];
                if (za.compartida || zb.compartida || za.salida == zb.salida) continue;
                const auto [pb, tb] = tipoDe(zb);
                if (!pb || tb != "grafico" || leerTexto(pb->datos, "fuente") != fuente) continue;
                const int64_t desde = std::max(za.desdeMs, zb.desdeMs);
                if (desde >= std::min(za.hastaMs, zb.hastaMs)) continue;
                auto contenido = [](const Pieza& x) {
                    return json{x.datos.value("grafico", json()), x.datos.value("valores", json()), x.datos.value("accion", json())};
                };
                if (contenido(*pa) == contenido(*pb)) continue;
                Conflicto c;
                c.tipo = Conflicto::Tipo::Grafico;
                c.ms = desde;
                c.salida = za.salida;
                c.pieza = za.pieza;
                c.otraSalida = zb.salida;
                c.otroPaso = zb.paso;
                c.otraPieza = zb.pieza;
                c.recurso = fuente;
                r.conflictos.push_back(c);
            }
        }
    }

    const Linea& l_;
    const Entrada& e_;
    std::vector<std::string> salidas_;
    std::map<std::string, int64_t> anclas_;
    std::map<const Pieza*, int> orden_;
    std::set<std::string> generadas_;
};

} // namespace

Resolucion resolver(const Linea& l, const Entrada& e) { return Resolvedor(l, e).resolver(); }

// --- Revisión previa -------------------------------------------------------------------

std::vector<Reparo> revisar(const Linea& l, const OpcionesRevision& o) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    std::vector<Reparo> rs;
    auto reparo = [&](const std::string& b, const std::string& p, const std::string& z, bool grave, std::string t) {
        rs.push_back({b, p, z, grave, std::move(t)});
    };
    std::size_t pasos = 0;
    for (const Bloque& b : l.bloques)
        pasos += b.pasos.size();
    if (pasos == 0) {
        reparo({}, {}, {}, true, traducir("linea", "la línea no tiene ningún paso"));
        return rs;
    }
    const std::vector<std::string> salidas = l.salidasEfectivas();
    auto esSalida = [&](const std::string& s) { return std::find(salidas.begin(), salidas.end(), s) != salidas.end(); };

    // Ids repetidos: el MCP edita por id y el recorrido anota por id.
    std::set<std::string> idsBloque, idsPaso, idsPieza;
    for (const Bloque& b : l.bloques) {
        if (!idsBloque.insert(b.id).second)
            reparo(b.id, {}, {}, true, llenar(traducir("linea", "hay dos bloques con el id «%1»"), {b.id}));
        for (const Paso& p : b.pasos) {
            if (p.id.empty() || !idsPaso.insert(p.id).second)
                reparo(b.id, p.id, {}, true, llenar(traducir("linea", "hay dos pasos con el id «%1»"), {p.id}));
            for (const Pieza& z : p.piezas)
                if (z.id.empty() || !idsPieza.insert(z.id).second)
                    reparo(b.id, p.id, z.id, true, llenar(traducir("linea", "hay dos piezas con el id «%1»"), {z.id}));
        }
    }

    // Condiciones que no se entienden: en vivo no se cumplirían nunca.
    const auto condicionMal = [&](const std::string& b, const std::string& p, const std::string& z, const Condicion& c) {
        if (c.empty()) return; // vacía: la reparan las reglas de cada modo
        if (const std::string e = errorDeCondicion(c); !e.empty())
            reparo(b, p, z, true, llenar(traducir("linea", "la condición «%1» no se entiende: %2"), {c, e}));
    };
    for (const Bloque& b : l.bloques) {
        if (b.inicio && b.inicio->modo == ModoInicio::Cuando) condicionMal(b.id, {}, {}, b.inicio->condicion);
        if (!b.plantilla.empty() && std::none_of(l.plantillas.begin(), l.plantillas.end(),
                                                 [&](const Plantilla& t) { return t.id == b.plantilla; }))
            reparo(b.id, {}, {}, true, llenar(traducir("linea", "el bloque se arma con la plantilla «%1», que no existe"), {b.plantilla}));
        for (const Paso& p : b.pasos) {
            if (p.inicio.modo == ModoInicio::Cuando) condicionMal(b.id, p.id, {}, p.inicio.condicion);
            if (p.fin.modo == ModoFin::Mientras) condicionMal(b.id, p.id, {}, p.fin.mientras);
            for (const Pieza& z : p.piezas)
                if (z.desde.modo == Ancla::Modo::Cuando) condicionMal(b.id, p.id, z.id, z.desde.cuando);
        }
    }
    for (const Disparador& d : l.disparadores) {
        if (d.cuando.empty()) reparo({}, {}, {}, true, llenar(traducir("linea", "el disparador «%1» no dice cuándo"), {d.id}));
        condicionMal({}, {}, {}, d.cuando);
        if (const std::string e = errorDeHacer(d.hacer); !e.empty())
            reparo({}, {}, {}, true, llenar(traducir("linea", "el disparador «%1»: %2"), {d.id, e}));
    }

    for (const Pista& pi : l.pistas)
        if (!pi.salida.empty() && !esSalida(pi.salida))
            reparo({}, {}, {}, true, llenar(traducir("linea", "la pista «%1» es de una salida que no existe: %2"), {pi.id, pi.salida}));
    for (const std::string& s : salidas) {
        const bool tiene = std::any_of(l.pistas.begin(), l.pistas.end(),
                                       [&](const Pista& pi) { return pi.tipo == "escena" && pi.salida == s; });
        if (!tiene) reparo({}, {}, {}, false, llenar(traducir("linea", "la salida «%1» no tiene pista de escena"), {s}));
    }

    for (const Bloque& b : l.bloques) {
        if (b.inicio && b.inicio->modo == ModoInicio::Hora && !horaValida(b.inicio->hora))
            reparo(b.id, {}, {}, true, llenar(traducir("linea", "la hora fija del bloque no está en formato HH:MM: %1"), {b.inicio->hora}));
        for (const Paso& p : b.pasos) {
            const std::string sp = l.salidaDe(p);
            if (p.salida != "*" && !esSalida(sp))
                reparo(b.id, p.id, {}, true, llenar(traducir("linea", "el paso sale por una salida que no existe: %1"), {sp}));
            const Inicio& ini = p.inicio;
            switch (ini.modo) {
                case ModoInicio::Go: break;
                case ModoInicio::Tras:
                case ModoInicio::Con: {
                    if (ini.de == "anterior") break;
                    // La referencia tiene que ser un paso ANTERIOR de la misma salida.
                    const bool antes = [&] {
                        for (const Bloque& b2 : l.bloques)
                            for (const Paso& p2 : b2.pasos) {
                                if (&p2 == &p) return false;
                                if (p2.id == ini.de && (l.vaPor(p2, sp) || p.salida == "*")) return true;
                            }
                        return false;
                    }();
                    if (!antes)
                        reparo(b.id, p.id, {}, true,
                               llenar(traducir("linea", "el paso entra con «%1», que no es un paso anterior de su salida"), {ini.de}));
                    break;
                }
                case ModoInicio::Hora:
                    if (!horaValida(ini.hora))
                        reparo(b.id, p.id, {}, true, llenar(traducir("linea", "la hora fija no está en formato HH:MM: %1"), {ini.hora}));
                    break;
                case ModoInicio::Cuando:
                    if (ini.condicion.empty()) reparo(b.id, p.id, {}, true, traducir("linea", "el paso entra «cuando» pero no dice cuándo"));
                    break;
                case ModoInicio::Ancla: {
                    const Paso* a = l.paso(ini.paso);
                    if (!esSalida(ini.salida) || a == nullptr || !l.vaPor(*a, ini.salida)) {
                        reparo(b.id, p.id, {}, true,
                               llenar(traducir("linea", "el paso se ancla a «%1/%2», que no existe"), {ini.salida, ini.paso}));
                        break;
                    }
                    // Anclas en círculo: nadie arranca primero.
                    const Paso* x = a;
                    for (std::size_t i = 0; x && x->inicio.modo == ModoInicio::Ancla && i <= pasos; ++i) {
                        if (x == &p) {
                            reparo(b.id, p.id, {}, true, traducir("linea", "las anclas entre salidas forman un círculo: ninguna arranca"));
                            break;
                        }
                        x = l.paso(x->inicio.paso);
                    }
                    break;
                }
            }
            if (p.fin.modo == ModoFin::Mientras && p.fin.mientras.empty())
                reparo(b.id, p.id, {}, true, traducir("linea", "el paso dura «mientras» pero no dice mientras qué"));

            for (const Pieza& z : p.piezas) {
                const Pista* pi = l.pista(z.pista);
                if (pi == nullptr) {
                    reparo(b.id, p.id, z.id, true, llenar(traducir("linea", "la pieza va en una pista que no existe: %1"), {z.pista}));
                } else if (!pi->salida.empty() && p.salida != "*" && pi->salida != sp) {
                    reparo(b.id, p.id, z.id, false,
                           llenar(traducir("linea", "la pieza va en la pista de otra salida (%1) que la de su paso (%2)"), {pi->salida, sp}));
                }
                // Lo que ya sabía revisar el v1, para los tipos que tenía: la misma regla.
                Tipo t;
                if (tipoDeNombre(z.tipo, t)) {
                    Cue c;
                    c.tipo = t;
                    c.texto = z.texto;
                    c.datos = z.datos;
                    c.duracionSeg = static_cast<int>(std::max(p.duracionPlanMs(), z.dura.ms) / 1000);
                    const std::string falta = c.queLeFalta();
                    if (!falta.empty()) reparo(b.id, p.id, z.id, true, falta);
                }
                if (z.tipo == "ptz" && !z.datos.contains("camara"))
                    reparo(b.id, p.id, z.id, true, traducir("linea", "la pieza PTZ no dice qué cámara mover"));
                if (z.tipo == "asistente" && leerTexto(z.datos, "herramienta").empty())
                    reparo(b.id, p.id, z.id, true, traducir("linea", "la pieza del asistente no dice qué herramienta usar"));
                if (z.tipo == "escena" && o.existeEscena) {
                    const std::string e = leerTexto(z.datos, "escena");
                    if (!e.empty() && !o.existeEscena(e))
                        reparo(b.id, p.id, z.id, true, llenar(traducir("linea", "la escena «%1» no existe: el paso no se va a tomar"), {e}));
                }
                if (z.desde.modo == Ancla::Modo::AntesDelFin && p.fin.modo != ModoFin::Duracion)
                    reparo(b.id, p.id, z.id, false,
                           traducir("linea", "la pieza sale antes del fin de un paso que no sabe cuándo termina: la hora es un supuesto"));
                if (z.desde.modo == Ancla::Modo::Cuando && z.desde.cuando.empty())
                    reparo(b.id, p.id, z.id, true, traducir("linea", "la pieza arranca «cuando» pero no dice cuándo"));
            }
        }
    }

    // Lo que sale de correr el plan entero: cadenas, hard starts y conflictos.
    Entrada e;
    e.inicioPlaneadoMs = o.contexto.inicioPlaneadoMs;
    e.medianocheMs = o.contexto.medianocheMs;
    e.finPlaneadoMs = o.contexto.finPlaneadoMs;
    e.ahoraMs = o.contexto.inicioPlaneadoMs > 0 ? o.contexto.inicioPlaneadoMs : o.contexto.ahoraMs;
    e.camarasDeEscena = o.contexto.camarasDeEscena;
    const Resolucion r = resolver(l, e);
    for (const SalidaResuelta& sr : r.salidas) {
        int seguidos = 0;
        for (const PasoResuelto& p : sr.pasos) {
            seguidos = p.automatico && p.como != ComoEntro::Hora ? seguidos + 1 : 0;
            if (seguidos == o.cadenaLarga)
                reparo(p.bloque, p.paso, {}, false,
                       llenar(traducir("linea", "%1 pasos seguidos entran solos: si algo sale mal la línea sigue sola"),
                              {std::to_string(o.cadenaLarga)}));
            if (p.cortadoPorHora)
                reparo(p.bloque, p.paso, {}, false, traducir("linea", "el paso no llega a entrar: lo corta la hora fija del que sigue"));
        }
        std::optional<int64_t> anterior;
        for (const HardStart& h : sr.hardStarts) {
            const std::string bloque = l.bloqueDe(h.paso) ? l.bloqueDe(h.paso)->id : std::string();
            if (anterior && h.horaMs < *anterior)
                reparo(bloque, h.paso, {}, true, traducir("linea", "la hora fija es anterior a la de un paso que va antes"));
            else if (h.gapMs < 0)
                reparo(bloque, h.paso, {}, h.politica == Politica::Cortar,
                       llenar(traducir("linea", "lo de antes no entra antes de la hora fija: faltan %1 s"), {std::to_string(-h.gapMs / 1000)}));
            anterior = h.horaMs;
        }
    }
    for (const Conflicto& c : r.conflictos) {
        const std::string bloque = l.bloqueDe(c.otroPaso) ? l.bloqueDe(c.otroPaso)->id : std::string();
        if (c.tipo == Conflicto::Tipo::Ptz)
            reparo(bloque, c.otroPaso, c.pieza, true,
                   llenar(c.armada ? traducir("linea", "la PTZ mueve la cámara %1 mientras la salida %2 la tiene armada")
                                   : traducir("linea", "la PTZ mueve la cámara %1 mientras la salida %2 la tiene al aire"),
                          {c.recurso, c.otraSalida}));
        else
            reparo(bloque, c.otroPaso, c.pieza, false,
                   llenar(traducir("linea", "la fuente de gráfico «%1» muestra cosas distintas en %2 y %3 a la vez"),
                          {c.recurso, c.salida, c.otraSalida}));
    }
    return rs;
}

// --- Load to time y retoma -----------------------------------------------------------

namespace {

const Pieza* piezaDe(const Linea& l, const PiezaResuelta& z) {
    const Paso* p = l.paso(z.paso);
    if (p == nullptr) return nullptr;
    for (const Pieza& x : p->piezas)
        if (x.id == z.pieza) return &x;
    return nullptr;
}

} // namespace

Carga cargarEn(const Linea& l, const Entrada& base, const std::string& salida, const std::string& destino,
               const std::vector<std::string>& activasAhora) {
    Carga c;
    // El plan corrido de punta a punta, sin nada emitido: dónde cae el destino y
    // qué hay en cada pista en ese momento.
    Entrada sim;
    sim.inicioPlaneadoMs = base.inicioPlaneadoMs;
    sim.medianocheMs = base.medianocheMs;
    sim.ahoraMs = base.inicioPlaneadoMs;
    const Resolucion r = resolver(l, sim);
    const SalidaResuelta* sr = r.salida(salida);
    const PasoResuelto* d = sr ? sr->paso(destino) : nullptr;
    if (d == nullptr) return c;
    std::set<std::string> van;
    for (const Activa& a : r.estadoEn(d->frenteMs)) {
        if (a.pieza.paso == destino) {
            van.insert(a.pieza.pieza);
            continue; // las del destino las dispara el destino
        }
        if (a.pieza.salida != salida && !a.pieza.compartida) continue;
        const Pieza* z = piezaDe(l, a.pieza);
        if (z == nullptr) continue;
        van.insert(a.pieza.pieza);
        c.deberianEstar.push_back({a.pieza, z->siSePerdioEfectivo(), true});
    }
    for (const std::string& id : activasAhora)
        if (!van.count(id)) c.sobran.push_back(id);
    return c;
}

Retoma retomarTrasCorte(const Linea& l, const EstadoAire& guardado, const Entrada& base) {
    Retoma out;
    const int64_t ahora = base.ahoraMs;
    out.estado = guardado;
    out.estado.pideDescripcion = true;
    out.estado.descripcion.clear();
    for (const std::string& s : l.salidasEfectivas()) {
        auto it = std::find_if(out.estado.salidas.begin(), out.estado.salidas.end(),
                               [&](const SalidaAire& x) { return x.salida == s; });
        if (it == out.estado.salidas.end()) {
            out.estado.salidas.push_back({s, {}, 0, {}, false});
            it = out.estado.salidas.end() - 1;
        }
        it->hold = true;
    }

    // Lo que se ve ahora, con todo en HOLD: lo que estaba al aire sigue, nada avanzó.
    Entrada e = base;
    e.instancias = guardado.instancias;
    e.salteados = guardado.salteados;
    e.holds.clear();
    e.proximos.clear();
    for (const SalidaAire& s : out.estado.salidas) {
        e.holds.push_back({s.salida, {}});
        if (!s.proximo.empty()) e.proximos.push_back({s.salida, s.proximo});
    }
    const Resolucion r = resolver(l, e);
    std::map<std::string, PiezaResuelta> activas;
    for (const Activa& a : r.estadoEn(ahora, true))
        activas[a.pieza.pieza] = a.pieza;
    std::set<std::string> estaban;
    for (const PiezaAire& p : guardado.piezas) {
        estaban.insert(p.pieza);
        const auto it = activas.find(p.pieza);
        if (it != activas.end()) out.reponer.push_back({it->second, SiSePerdio::Ejecutar, true});
        else out.sacar.push_back(p.pieza);
    }
    // Lo que tocaba mientras no había luz: su siSePerdio.
    for (const PiezaResuelta& z : r.piezas) {
        if (estaban.count(z.pieza) || z.desdeMs <= guardado.escritoMs || z.desdeMs > ahora) continue;
        const Pieza* pz = piezaDe(l, z);
        if (pz == nullptr) continue;
        const bool sigue = activas.count(z.pieza) > 0;
        if (!sigue && z.hastaMs > ahora) continue; // tapada por otra: no se ve
        out.perdidas.push_back({z, pz->siSePerdioEfectivo(), sigue});
    }

    // Según el plan, sin el corte: qué tocaría ahora (para que el asistente proponga).
    Entrada plan = e;
    plan.holds.clear();
    plan.ahoraMs = guardado.escritoMs;
    const Resolucion rp = resolver(l, plan);

    // El resumen, compacto y sin traducir: lo lee el asistente.
    auto seg = [](int64_t ms) { return ms / 1000; };
    json salidas = json::array();
    for (const SalidaResuelta& sr : r.salidas) {
        json js{{"salida", sr.salida}, {"hold", true}, {"overUnderSeg", seg(sr.overUnderMs)}};
        js["alAire"] = nullptr;
        js["proximo"] = nullptr;
        js["proximoHardStart"] = nullptr;
        js["segunElPlan"] = nullptr;
        if (const PasoResuelto* p = sr.paso(sr.alAire)) {
            const Paso* m = l.paso(p->paso);
            const int64_t lleva = ahora - p->frenteMs;
            js["alAire"] = {{"paso", p->paso},
                            {"nombre", m->nombre},
                            {"bloque", p->bloque},
                            {"llevaSeg", seg(lleva)},
                            {"previstoSeg", seg(m->duracionPlanMs())},
                            {"excesoSeg", seg(std::max<int64_t>(0, lleva - m->duracionPlanMs()))}};
        }
        if (const PasoResuelto* p = sr.paso(sr.proximo)) {
            const Paso* m = l.paso(p->paso);
            js["proximo"] = {{"paso", p->paso}, {"nombre", m->nombre}, {"entra", nombreDe(p->como)}};
        }
        for (const HardStart& h : sr.hardStarts) {
            if (h.horaMs < ahora) continue;
            js["proximoHardStart"] = {
                {"paso", h.paso}, {"enSeg", seg(h.horaMs - ahora)}, {"gapSeg", seg(h.gapMs)}, {"politica", nombreDe(h.politica)}};
            break;
        }
        if (const SalidaResuelta* sp = rp.salida(sr.salida)) {
            for (const PasoResuelto& p : sp->pasos) {
                if (p.estado == EstadoPaso::Salteado || p.cortadoPorHora || p.frenteMs > ahora || ahora >= p.finMs) continue;
                if (p.paso == sr.alAire) break; // sin corte iría por el mismo paso: nada que contar
                // ¿Llegaba ahí solo, o hacía falta algún GO en el medio?
                bool solo = true;
                for (const PasoResuelto& q : sp->pasos)
                    if ((q.estado == EstadoPaso::Proximo || q.estado == EstadoPaso::Futuro) && q.frenteMs <= p.frenteMs &&
                        !q.automatico)
                        solo = false;
                js["segunElPlan"] = {{"paso", p.paso}, {"nombre", l.paso(p.paso)->nombre}, {"soloConAutomaticos", solo}};
                break;
            }
        }
        salidas.push_back(js);
    }
    auto piezas = [&](const std::vector<Reposicion>& v) {
        json a = json::array();
        for (const Reposicion& x : v) {
            const Pieza* z = piezaDe(l, x.pieza);
            a.push_back({{"pieza", x.pieza.pieza},
                         {"tipo", z ? z->tipo : std::string()},
                         {"pista", x.pieza.pista},
                         {"salida", x.pieza.salida},
                         {"accion", nombreDe(x.accion)},
                         {"sigueActiva", x.sigueActiva}});
        }
        return a;
    };
    out.resumen = {{"corte", {{"desdeMs", guardado.escritoMs}, {"hastaMs", ahora}, {"seg", seg(ahora - guardado.escritoMs)}}},
                   {"hold", true},
                   {"pideDescripcion", true},
                   {"salidas", salidas},
                   {"reponer", piezas(out.reponer)},
                   {"perdidas", piezas(out.perdidas)},
                   {"sacar", out.sacar}};
    return out;
}

} // namespace ba::evento::linea
