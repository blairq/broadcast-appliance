// ============================================================================
// src/core/diag/LectorDeLogs.cpp — ver LectorDeLogs.hpp
// ============================================================================
#include "diag/LectorDeLogs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>

namespace ba::diag {
namespace {

namespace fs = std::filesystem;

/// `AAAA-MM-DDTHH:MM:SS.mmmZ` -> época ms. 0 si no tiene esa forma.
std::int64_t deSelloDeTiempo(const std::string& s) {
    int a = 0, me = 0, d = 0, h = 0, mi = 0, se = 0, ms = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%3dZ", &a, &me, &d, &h, &mi, &se, &ms) < 6) return 0;
    std::tm tm{};
    tm.tm_year = a - 1900;
    tm.tm_mon = me - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) return 0;
    return static_cast<std::int64_t>(t) * 1000 + ms;
}

std::string enMinusculas(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string recortar(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// Se queda con los últimos `kib` KiB del archivo, desde el principio de una
/// línea entera (leer a media línea daría una entrada partida al principio).
std::vector<std::string> ultimasLineas(const fs::path& p, std::size_t kib) {
    std::vector<std::string> out;
    std::error_code ec;
    const auto tam = fs::file_size(p, ec);
    if (ec) return out;

    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    const std::uintmax_t tope = static_cast<std::uintmax_t>(kib) * 1024u;
    if (tam > tope) f.seekg(static_cast<std::streamoff>(tam - tope));

    std::string linea;
    bool primera = tam > tope;
    while (std::getline(f, linea)) {
        if (primera) { // puede venir cortada por la mitad
            primera = false;
            continue;
        }
        if (!linea.empty()) out.push_back(linea);
    }
    return out;
}

/// El componente que se le asigna a lo que no viene estructurado: el nombre
/// del archivo sin extensiones (app.log.2 -> app).
std::string componenteDeArchivo(const fs::path& p) {
    std::string n = p.filename().string();
    const auto punto = n.find('.');
    if (punto != std::string::npos) n.resize(punto);
    return n.empty() ? "log" : n;
}

bool pasa(const Registrado& r, const Filtro& f, const std::string& buscaMin) {
    if (static_cast<int>(r.nivel) < static_cast<int>(f.minimo)) return false;
    if (f.desdeMs > 0 && r.ms < f.desdeMs) return false;
    // Un componente que TERMINA EN PUNTO filtra por prefijo: "agente." trae
    // agente.recepcion y agente.tecnico de una. Sin esto, mirar lo que
    // hicieron los agentes obligaría a filtrar rol por rol.
    const auto coincide = [&r](const std::string& c) {
        if (!c.empty() && c.back() == '.')
            return r.componente.size() > c.size() && r.componente.compare(0, c.size(), c) == 0;
        return r.componente == c;
    };
    if (!f.componentes.empty() && std::none_of(f.componentes.begin(), f.componentes.end(), coincide))
        return false;
    if (!buscaMin.empty()) {
        // Se busca en el texto Y en el componente: «sala» tiene que encontrar
        // lo de la sala aunque el texto no diga la palabra.
        if (enMinusculas(r.texto).find(buscaMin) == std::string::npos &&
            enMinusculas(r.componente).find(buscaMin) == std::string::npos)
            return false;
    }
    return true;
}

std::vector<fs::path> archivosDeLog(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const std::string n = e.path().filename().string();
        // `app.log`, `gateway.log.2`: todo lo que tenga ".log" en el nombre.
        if (n.find(".log") != std::string::npos) out.push_back(e.path());
    }
    return out;
}

} // namespace

bool partirLinea(const std::string& linea, const std::string& componentePorDefecto, Registrado& out) {
    out = Registrado{};
    out.componente = componentePorDefecto;
    out.texto = recortar(linea);

    // Forma del Registro: `<sello> nivel | componente | codigo | texto`. Y
    // también la vieja del launcher y del gateway: `[<sello>] nivel | ...`.
    std::string resto = out.texto;
    if (!resto.empty() && resto[0] == '[') {
        const auto cierre = resto.find(']');
        if (cierre == std::string::npos) return false;
        out.ms = deSelloDeTiempo(resto.substr(1, cierre - 1));
        resto = recortar(resto.substr(cierre + 1));
    } else {
        // Sin sello: puede ser una linea estructurada que perdio la hora (lo
        // que el gateway espeja a stderr y el launcher captura tal cual), asi
        // que se sigue intentando partir. La hora la hereda de la anterior.
        const auto esp = resto.find(' ');
        if (esp != std::string::npos) {
            const std::int64_t ms = deSelloDeTiempo(resto.substr(0, esp));
            if (ms > 0) {
                out.ms = ms;
                resto = recortar(resto.substr(esp + 1));
            }
        }
    }

    // nivel | componente | codigo | texto
    const auto b1 = resto.find('|');
    if (b1 == std::string::npos) return false;
    Nivel n{};
    if (!nivelDeNombre(enMinusculas(recortar(resto.substr(0, b1))), n)) return false;
    const auto b2 = resto.find('|', b1 + 1);
    if (b2 == std::string::npos) return false;
    const auto b3 = resto.find('|', b2 + 1);
    if (b3 == std::string::npos) return false;

    out.nivel = n;
    out.componente = recortar(resto.substr(b1 + 1, b2 - b1 - 1));
    out.codigo = std::atoi(recortar(resto.substr(b2 + 1, b3 - b2 - 1)).c_str());
    out.texto = recortar(resto.substr(b3 + 1));
    if (out.componente.empty()) out.componente = componentePorDefecto;
    return true;
}

std::vector<Registrado> leerLogs(const fs::path& dir, const Filtro& f, std::size_t porArchivoKiB) {
    std::vector<Registrado> todo;
    const std::string buscaMin = enMinusculas(f.busca);

    for (const fs::path& p : archivosDeLog(dir)) {
        const std::string comp = componenteDeArchivo(p);
        std::int64_t ultimoMs = 0;
        for (const std::string& l : ultimasLineas(p, porArchivoKiB)) {
            Registrado r;
            partirLinea(l, comp, r);
            // Una línea sin hora propia (la salida cruda de libobs, por
            // ejemplo) hereda la de la anterior: así no se va al principio de
            // los tiempos al ordenar y queda donde de verdad pasó.
            if (r.ms == 0)
                r.ms = ultimoMs;
            else
                ultimoMs = r.ms;
            if (pasa(r, f, buscaMin)) todo.push_back(std::move(r));
        }
    }

    std::stable_sort(todo.begin(), todo.end(),
                     [](const Registrado& a, const Registrado& b) { return a.ms < b.ms; });
    if (f.cuantas > 0 && todo.size() > f.cuantas)
        todo.erase(todo.begin(), todo.end() - static_cast<std::ptrdiff_t>(f.cuantas));
    return todo;
}

std::vector<std::string> componentesEnLogs(const fs::path& dir, std::size_t porArchivoKiB) {
    std::set<std::string> vistos;
    for (const fs::path& p : archivosDeLog(dir)) {
        const std::string comp = componenteDeArchivo(p);
        for (const std::string& l : ultimasLineas(p, porArchivoKiB)) {
            Registrado r;
            partirLinea(l, comp, r);
            if (!r.componente.empty()) vistos.insert(r.componente);
        }
    }
    return {vistos.begin(), vistos.end()};
}

} // namespace ba::diag
