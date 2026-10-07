// ============================================================================
// src/app/evento/EventoStore.cpp — ver EventoStore.hpp
// ============================================================================
#include "evento/EventoStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace ba::evento {
namespace {

using nlohmann::json;

constexpr const char* kGuion = "evento.json";
constexpr const char* kFicha = "ficha.json";
constexpr const char* kEnCurso = "en-curso.json";

/// Escribe un JSON entero o nada (temporal + rename: un corte de luz no lo deja a medias).
bool escribirJson(const fs::path& archivo, const json& j, std::string* err) {
    const fs::path tmp = archivo.string() + ".nuevo";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (err != nullptr) *err = "no se pudo escribir " + archivo.filename().string();
            return false;
        }
        out << j.dump(2) << "\n";
        if (!out.good()) {
            if (err != nullptr) *err = "no se pudo escribir " + archivo.filename().string() + " entero";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, archivo, ec);
    if (ec) {
        if (err != nullptr) *err = "no se pudo reemplazar " + archivo.filename().string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

json leerJson(const fs::path& archivo) {
    std::ifstream in(archivo);
    if (!in) return json();
    std::stringstream ss;
    ss << in.rdbuf();
    return json::parse(ss.str(), nullptr, false);
}

/// "a/b" + "c" (sin «/» de más si `a` está vacía).
std::string unir(const std::string& a, const std::string& b) { return a.empty() ? b : a + "/" + b; }

/// Las carpetas internas de un evento y lo escondido no son carpetas del histórico.
bool esInterna(const std::string& nombre) { return nombre.empty() || nombre[0] == '.'; }

/// Caracteres que Windows no acepta en un nombre de archivo (Linux solo
/// prohibe '/', pero el disco es portable y tiene que abrir en los dos).
bool prohibido(char c) {
    if (static_cast<unsigned char>(c) < 0x20) return true;
    static const std::string kMalos = R"(<>:"/\|?*)";
    return kMalos.find(c) != std::string::npos;
}

std::string ahoraParaNombre() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void ponerErr(std::string* err, const std::string& texto) {
    if (err != nullptr) *err = texto;
}

} // namespace

std::string nombreDeCarpeta(const std::string& nombre) {
    std::string out;
    out.reserve(nombre.size());
    for (const char c : nombre) {
        if (prohibido(c)) continue;
        out.push_back(c == ' ' ? '-' : c);
    }
    // Un nombre que termina en punto o espacio no se puede crear en Windows.
    while (!out.empty() && (out.back() == '.' || out.back() == '-'))
        out.pop_back();
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    if (out.size() > 80) out.resize(80);
    return out;
}

namespace {

/// Resuelve la ruta de un medio DENTRO de la subcarpeta que le corresponde.
/// Devuelve vacio si el guion intenta salirse de ahi (un ".." en un evento
/// que vino de otro lado no tiene por que leer el disco del que lo abre).
fs::path dentroDe(const fs::path& base, const std::string& sub, std::string rel) {
    // "Medios/x.wav" y "x.wav" valen lo mismo: el operador escribe lo que le
    // sale y las dos formas apuntan al mismo lado.
    const std::string prefijo = sub + "/";
    if (rel.rfind(prefijo, 0) == 0) rel = rel.substr(prefijo.size());
    const fs::path dir = base / sub;
    const fs::path abs = (dir / rel).lexically_normal();
    const fs::path raiz = dir.lexically_normal();
    const auto rel2 = abs.lexically_relative(raiz);
    // `generic_string()` y no `native()`: en Windows `native()` es un wstring
    // y no se puede comparar con ".." literal, asi que el build cruzado no
    // pasaba de aca. `generic_string` da std::string en las tres plataformas.
    if (rel2.empty() || rel2.generic_string().rfind("..", 0) == 0) return {};
    return abs;
}

} // namespace

std::vector<Reparo> revisarArchivos(const Evento& e, const fs::path& carpeta) {
    std::vector<Reparo> rs;
    std::error_code ec;
    for (std::size_t bi = 0; bi < e.bloques.size(); ++bi) {
        const Bloque& b = e.bloques[bi];
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            const Cue& c = b.cues[ci];
            std::string clave;
            std::string sub;
            switch (c.tipo) {
                case Tipo::Musica:
                    if (c.datos.value("accion", std::string("arrancar")) == "parar") continue;
                    clave = "archivo";
                    sub = "Medios";
                    break;
                case Tipo::Video:
                    clave = "archivo";
                    sub = "Medios";
                    break;
                case Tipo::Teleprompter:
                    clave = "discurso";
                    sub = "Discursos";
                    break;
                default: continue;
            }
            const std::string rel = c.datos.value(clave, std::string());
            if (rel.empty()) continue; // ya lo dijo la revision del guion
            const int ib = static_cast<int>(bi);
            const int ic = static_cast<int>(ci);
            const fs::path p = dentroDe(carpeta, sub, rel);
            if (p.empty()) {
                rs.push_back({ib, ic, true, "\"" + rel + "\" apunta fuera de " + sub + "/"});
                continue;
            }
            if (!fs::exists(p, ec)) {
                rs.push_back({ib, ic, true, "falta el archivo " + sub + "/" + rel});
                continue;
            }
            if (fs::is_directory(p, ec)) {
                rs.push_back({ib, ic, true, sub + "/" + rel + " es una carpeta, no un archivo"});
                continue;
            }
            if (fs::file_size(p, ec) == 0 && !ec)
                rs.push_back({ib, ic, true, sub + "/" + rel + " esta vacio"});
        }
    }
    return rs;
}

json DatosDelEvento::aJson() const {
    json j = resto.is_object() ? resto : json::object();
    j["estado"] = estado;
    j["iniciado_ms"] = iniciadoMs;
    j["terminado_ms"] = terminadoMs;
    j["lugar"] = lugar;
    j["etiquetas"] = etiquetas;
    j["notas"] = notas;
    j["automatico"] = automatico;
    if (!dia.empty()) j["dia"] = dia;
    else j.erase("dia");
    return j;
}

DatosDelEvento DatosDelEvento::deJson(const json& j) {
    DatosDelEvento d;
    if (!j.is_object()) return d;
    d.resto = j;
    d.estado = j.value("estado", std::string("planificado"));
    if (d.estado != "planificado" && d.estado != "en_curso" && d.estado != "terminado") d.estado = "planificado";
    d.iniciadoMs = j.value("iniciado_ms", std::int64_t{0});
    d.terminadoMs = j.value("terminado_ms", std::int64_t{0});
    d.lugar = j.value("lugar", std::string());
    d.notas = j.value("notas", std::string());
    d.automatico = j.value("automatico", false);
    d.dia = j.value("dia", std::string());
    if (j.contains("etiquetas") && j["etiquetas"].is_array())
        for (const auto& e : j["etiquetas"])
            if (e.is_string()) d.etiquetas.push_back(e.get<std::string>());
    return d;
}

bool EventoStore::rutaValida(const std::string& ruta) {
    if (ruta.empty() || ruta.front() == '/' || ruta.back() == '/' || ruta.find('\\') != std::string::npos ||
        ruta.find(':') != std::string::npos || ruta.find("//") != std::string::npos)
        return false;
    std::stringstream ss(ruta);
    std::string parte;
    while (std::getline(ss, parte, '/'))
        if (parte.empty() || parte == "." || parte == ".." || esInterna(parte)) return false;
    return true;
}

EventoStore::EventoStore(fs::path raiz) : raiz_(std::move(raiz)) {
    std::error_code ec;
    fs::create_directories(raiz_, ec);
}

std::vector<Ficha> EventoStore::listar() const {
    std::vector<Ficha> fichas;
    std::error_code ec;
    if (!fs::is_directory(raiz_, ec)) return fichas;
    // Recorre las carpetas: la que tiene guion es un evento (no se entra), la
    // que no, agrupa (se entra). Con un tope de profundidad por las dudas.
    std::function<void(const std::string&, int)> recorrer = [&](const std::string& grupo, int hondo) {
        std::error_code e2;
        for (const fs::directory_entry& d : fs::directory_iterator(grupo.empty() ? raiz_ : raiz_ / grupo, e2)) {
            if (!d.is_directory(e2)) continue;
            const std::string nombre = d.path().filename().string();
            if (esInterna(nombre)) continue;
            const std::string carpeta = unir(grupo, nombre);
            if (!fs::exists(d.path() / kGuion, e2)) {
                if (hondo < 8) recorrer(carpeta, hondo + 1);
                continue;
            }
            bool ok = false;
            const Evento e = cargar(carpeta, &ok);
            // Un guion roto no se inventa como evento vacío.
            if (!ok) continue;
            Ficha f;
            f.carpeta = carpeta;
            f.grupo = grupo;
            f.nombre = e.nombre.empty() ? nombre : e.nombre;
            f.fecha = e.fecha;
            f.inicio = e.inicio;
            f.huso = e.huso;
            f.duracionSeg = e.duracionSeg();
            f.bloques = e.bloques.size();
            f.cues = e.cantidadDeCues();
            f.datos = datos(carpeta);
            const auto cuando = fs::last_write_time(d.path() / kGuion, e2);
            if (!e2) {
                f.modificadoTic = static_cast<std::int64_t>(cuando.time_since_epoch().count());
                f.tieneModificado = true;
            }
            fichas.push_back(std::move(f));
        }
    };
    recorrer({}, 0);
    std::sort(fichas.begin(), fichas.end(),
              [](const Ficha& a, const Ficha& b) { return a.nombre < b.nombre; });
    return fichas;
}

std::vector<std::string> EventoStore::carpetas() const {
    std::vector<std::string> out;
    std::function<void(const std::string&, int)> recorrer = [&](const std::string& grupo, int hondo) {
        std::error_code ec;
        for (const fs::directory_entry& d : fs::directory_iterator(grupo.empty() ? raiz_ : raiz_ / grupo, ec)) {
            if (!d.is_directory(ec)) continue;
            const std::string nombre = d.path().filename().string();
            if (esInterna(nombre) || fs::exists(d.path() / kGuion, ec)) continue;
            const std::string carpeta = unir(grupo, nombre);
            out.push_back(carpeta);
            if (hondo < 8) recorrer(carpeta, hondo + 1);
        }
    };
    recorrer({}, 0);
    std::sort(out.begin(), out.end());
    return out;
}

std::string EventoStore::crearCarpeta(const std::string& ruta, std::string* err) {
    std::string limpia;
    std::stringstream ss(ruta);
    std::string parte;
    while (std::getline(ss, parte, '/')) {
        const std::string n = nombreDeCarpeta(parte);
        if (n.empty() || n == "." || n == ".." || esInterna(n)) continue;
        limpia = unir(limpia, n);
    }
    if (limpia.empty()) {
        ponerErr(err, "ese nombre no sirve para una carpeta");
        return {};
    }
    std::error_code ec;
    // No adentro de un evento: una carpeta con guion no agrupa.
    std::string tramo;
    std::stringstream s2(limpia);
    while (std::getline(s2, parte, '/')) {
        tramo = unir(tramo, parte);
        if (fs::exists(raiz_ / tramo / kGuion, ec)) {
            ponerErr(err, "«" + tramo + "» es un evento, no una carpeta");
            return {};
        }
    }
    fs::create_directories(raiz_ / limpia, ec);
    if (ec) {
        ponerErr(err, "no se pudo crear la carpeta: " + ec.message());
        return {};
    }
    return limpia;
}

std::string EventoStore::ultimoTocado() const {
    std::string mejor;
    std::int64_t masReciente = 0;
    bool hay = false;
    for (const Ficha& f : listar()) {
        // El del día se crea solo: no es «el que se estaba trabajando».
        if (!f.tieneModificado || f.datos.automatico) continue;
        // Ojo con comparar contra cero o contra -1: estos tics son negativos
        // en libstdc++ (ver Ficha::modificadoTic). El primero que aparece
        // marca el piso.
        if (!hay || f.modificadoTic > masReciente) {
            masReciente = f.modificadoTic;
            mejor = f.carpeta;
            hay = true;
        }
    }
    return mejor;
}

std::string EventoStore::crear(const std::string& nombre, std::string* err, const std::string& enCarpeta,
                               const std::string& nombreCarpeta) {
    const std::string base = nombreDeCarpeta(nombreCarpeta.empty() ? nombre : nombreCarpeta);
    if (base.empty()) {
        ponerErr(err, "ese nombre no sirve para una carpeta");
        return {};
    }
    std::string grupo;
    if (!enCarpeta.empty()) {
        grupo = crearCarpeta(enCarpeta, err);
        if (grupo.empty()) return {};
    }
    std::error_code ec;
    // Si ya hay una con ese nombre, se le agrega un numero: pisar el evento
    // de otro dia por tener el mismo titulo seria imperdonable.
    std::string carpeta = unir(grupo, base);
    for (int i = 2; fs::exists(raiz_ / carpeta, ec) && i < 1000; ++i)
        carpeta = unir(grupo, base + "-" + std::to_string(i));

    const fs::path dir = raiz_ / carpeta;
    if (!fs::create_directories(dir, ec)) {
        ponerErr(err, "no se pudo crear la carpeta: " + ec.message());
        return {};
    }
    for (const char* sub : {"Medios", "Discursos", "Respaldos"})
        fs::create_directories(dir / sub, ec);

    Evento e;
    e.nombre = nombre;
    if (!guardar(carpeta, e, err)) return {};
    return carpeta;
}

Evento EventoStore::cargar(const std::string& carpeta, bool* ok, std::string* err) const {
    if (ok != nullptr) *ok = false;
    const fs::path archivo = raiz_ / carpeta / kGuion;
    std::ifstream in(archivo);
    if (!in) {
        ponerErr(err, "no hay guion en " + archivo.string());
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded()) {
        // El archivo esta ahi pero roto. No se devuelve un evento vacio como
        // si nada: el operador tiene que enterarse ANTES de empezar a editar
        // encima, porque el respaldo del guardado siguiente seria el roto.
        ponerErr(err, "el guion esta dañado (JSON invalido): mira en Respaldos/");
        return {};
    }
    if (ok != nullptr) *ok = true;
    return Evento::deJson(j);
}

bool EventoStore::guardar(const std::string& carpeta, const Evento& e, std::string* err) const {
    const fs::path dir = raiz_ / carpeta;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        ponerErr(err, "no existe la carpeta del evento");
        return false;
    }
    const fs::path archivo = dir / kGuion;

    // Respaldo del que estaba. Que falle el respaldo no impide guardar, pero
    // se avisa: es peor quedarse sin poder guardar en medio de un evento.
    if (fs::exists(archivo, ec)) {
        fs::create_directories(dir / "Respaldos", ec);
        fs::copy_file(archivo, dir / "Respaldos" / ("evento-" + ahoraParaNombre() + ".json"),
                      fs::copy_options::overwrite_existing, ec);
    }

    // Se escribe a un temporal y se renombra: si se corta la luz en el medio,
    // el guion que estaba sigue entero.
    const fs::path tmp = dir / "evento.json.nuevo";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            ponerErr(err, "no se pudo escribir en la carpeta del evento");
            return false;
        }
        out << e.aJson().dump(2) << "\n";
        if (!out.good()) {
            ponerErr(err, "no se pudo escribir el guion entero");
            return false;
        }
    }
    fs::rename(tmp, archivo, ec);
    if (ec) {
        ponerErr(err, "no se pudo reemplazar el guion: " + ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool EventoStore::borrar(const std::string& carpeta, std::string* err) const {
    if (!rutaValida(carpeta)) {
        ponerErr(err, "nombre de carpeta invalido");
        return false;
    }
    std::error_code ec;
    const fs::path dir = raiz_ / carpeta;
    // Solo eventos: una carpeta que agrupa puede tener años de histórico adentro.
    if (!fs::is_directory(dir, ec) || !fs::exists(dir / kGuion, ec)) {
        ponerErr(err, "no existe ese evento");
        return false;
    }
    if (enCurso() == carpeta) fs::remove(raiz_ / kEnCurso, ec);
    fs::remove_all(dir, ec);
    if (ec) {
        ponerErr(err, "no se pudo borrar: " + ec.message());
        return false;
    }
    return true;
}

std::string EventoStore::mover(const std::string& evento, const std::string& aCarpeta, std::string* err) {
    std::error_code ec;
    if (!rutaValida(evento) || !fs::exists(raiz_ / evento / kGuion, ec)) {
        ponerErr(err, "no existe ese evento");
        return {};
    }
    std::string grupo;
    if (!aCarpeta.empty()) {
        grupo = crearCarpeta(aCarpeta, err);
        if (grupo.empty()) return {};
    }
    const std::string nombre = fs::path(evento).filename().string();
    std::string destino = unir(grupo, nombre);
    if (destino == evento) return evento;
    if (destino.rfind(evento + "/", 0) == 0) {
        ponerErr(err, "no se puede mover un evento adentro de sí mismo");
        return {};
    }
    for (int i = 2; fs::exists(raiz_ / destino, ec) && i < 1000; ++i)
        destino = unir(grupo, nombre + "-" + std::to_string(i));
    const bool eraEnCurso = enCurso() == evento;
    fs::rename(raiz_ / evento, raiz_ / destino, ec);
    if (ec) {
        ponerErr(err, "no se pudo mover: " + ec.message());
        return {};
    }
    if (eraEnCurso) {
        json j = leerJson(raiz_ / kEnCurso);
        if (!j.is_object()) j = json::object();
        j["evento"] = destino;
        escribirJson(raiz_ / kEnCurso, j, nullptr);
    }
    return destino;
}

DatosDelEvento EventoStore::datos(const std::string& evento) const {
    if (!rutaValida(evento)) return {};
    return DatosDelEvento::deJson(leerJson(raiz_ / evento / kFicha));
}

bool EventoStore::guardarDatos(const std::string& evento, const DatosDelEvento& d, std::string* err) const {
    std::error_code ec;
    if (!rutaValida(evento) || !fs::exists(raiz_ / evento / kGuion, ec)) {
        ponerErr(err, "no existe ese evento");
        return false;
    }
    return escribirJson(raiz_ / evento / kFicha, d.aJson(), err);
}

std::string EventoStore::enCurso() const {
    const json j = leerJson(raiz_ / kEnCurso);
    if (!j.is_object()) return {};
    const std::string e = j.value("evento", std::string());
    std::error_code ec;
    if (!rutaValida(e) || !fs::exists(raiz_ / e / kGuion, ec)) return {};
    return e;
}

bool EventoStore::iniciar(const std::string& evento, std::int64_t ahoraMs, std::string* err) {
    std::error_code ec;
    if (!rutaValida(evento) || !fs::exists(raiz_ / evento / kGuion, ec)) {
        ponerErr(err, "no existe ese evento");
        return false;
    }
    const std::string antes = enCurso();
    if (antes == evento) return true;
    if (!antes.empty()) terminar(ahoraMs, nullptr);
    DatosDelEvento d = datos(evento);
    d.estado = "en_curso";
    // Uno que vuelve a empezar conserva cuándo empezó la primera vez.
    if (d.iniciadoMs == 0) d.iniciadoMs = ahoraMs;
    d.terminadoMs = 0;
    if (!guardarDatos(evento, d, err)) return false;
    return escribirJson(raiz_ / kEnCurso, json{{"evento", evento}, {"desde_ms", ahoraMs}}, err);
}

bool EventoStore::terminar(std::int64_t ahoraMs, std::string* err) {
    const std::string e = enCurso();
    std::error_code ec;
    fs::remove(raiz_ / kEnCurso, ec);
    if (e.empty()) {
        ponerErr(err, "no hay ningún evento en curso");
        return false;
    }
    DatosDelEvento d = datos(e);
    d.estado = "terminado";
    d.terminadoMs = ahoraMs;
    return guardarDatos(e, d, err);
}

std::string EventoStore::asegurarDelDia(const std::string& dia, std::int64_t ahoraMs, std::string* err) {
    if (dia.size() != 10) {
        ponerErr(err, "día inválido");
        return {};
    }
    const std::string actual = enCurso();
    if (!actual.empty()) {
        const DatosDelEvento d = datos(actual);
        if (!d.automatico || d.dia == dia) return actual;
        terminar(ahoraMs, nullptr); // el automático de ayer
    }
    static const char* kMeses[] = {"Enero", "Febrero", "Marzo",     "Abril",   "Mayo",      "Junio",
                                   "Julio", "Agosto",  "Septiembre", "Octubre", "Noviembre", "Diciembre"};
    const int mes = std::atoi(dia.substr(5, 2).c_str());
    if (mes < 1 || mes > 12) {
        ponerErr(err, "día inválido");
        return {};
    }
    const std::string grupo = dia.substr(0, 4) + "/" + kMeses[mes - 1];
    const std::string carpeta = unir(grupo, dia + "-jornada");
    std::error_code ec;
    if (!fs::exists(raiz_ / carpeta / kGuion, ec)) {
        const std::string nombre = "Jornada del " + dia.substr(8, 2) + "/" + dia.substr(5, 2) + "/" + dia.substr(0, 4);
        const std::string hecha = crear(nombre, err, grupo, dia + "-jornada");
        if (hecha != carpeta) return {};
        Evento e = cargar(carpeta);
        e.nombre = nombre;
        e.fecha = dia;
        guardar(carpeta, e, nullptr);
        DatosDelEvento d;
        d.automatico = true;
        d.dia = dia;
        d.notas = "Creado solo: no había ningún evento en curso.";
        if (!guardarDatos(carpeta, d, err)) return {};
    }
    if (!iniciar(carpeta, ahoraMs, err)) return {};
    return carpeta;
}

} // namespace ba::evento
