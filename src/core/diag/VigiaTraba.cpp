// src/core/diag/VigiaTraba.cpp — ver VigiaTraba.hpp

#include "diag/VigiaTraba.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/Escritor.hpp"
#include "platform/Hilo.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace ba::diag {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::size_t kGuardar = 10; ///< las últimas capturas de trabas que se guardan

std::string fecha() {
    const std::time_t ahora = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &ahora);
#else
    localtime_r(&ahora, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
    return b;
}

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

/// El .json de la captura con `cambios` encima (lo que se sabe después: el resumen, lo que duró).
void completarJson(const fs::path& ruta, const json& cambios) {
    if (ruta.empty()) return;
    json j = json::parse(leer(ruta), nullptr, false);
    if (!j.is_object()) j = json::object();
    for (auto it = cambios.begin(); it != cambios.end(); ++it) j[it.key()] = it.value();
    ba::persist::escribir(ruta, j.dump(1, ' ', false, json::error_handler_t::replace));
}

/// Se quedan las últimas kGuardar capturas de trabas de `proceso` (con su .json y su .txt).
void podar(const fs::path& dir, const std::string& proceso) {
    std::error_code ec;
    std::vector<fs::path> v;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string f = e.path().filename().string();
        const std::string ext = e.path().extension().string();
        if ((ext == ".pila" || ext == ".dmp") && f.rfind(proceso + "-", 0) == 0 && f.find("-traba-") != std::string::npos)
            v.push_back(e.path());
    }
    std::sort(v.begin(), v.end()); // por nombre = por hora
    for (std::size_t i = 0; i + kGuardar < v.size(); ++i) {
        fs::remove(v[i], ec);
        fs::path o = v[i];
        fs::remove(o.replace_extension(".json"), ec);
        fs::remove(o.replace_extension(".txt"), ec);
    }
}

} // namespace

std::uint64_t VigiaTraba::hiloPorNombre(const std::string& texto, const std::string& nombre) {
    std::istringstream in(texto);
    std::string l;
    while (std::getline(in, l)) {
        if (l.rfind("== hilo ", 0) != 0) continue;
        const std::size_t esp = l.find(' ', 8);
        if (esp == std::string::npos || l.find(nombre, esp) == std::string::npos) continue;
        try {
            return std::stoull(l.substr(8, esp - 8));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

void VigiaTraba::iniciar(Opciones o, std::uint64_t hiloInterfaz) {
    detener();
    o_ = std::move(o);
    hiloInterfaz_ = hiloInterfaz;
    if (!o_.capturar) o_.capturar = capturarPilas;
    if (!o_.avisar)
        o_.avisar = [](const std::string& t) { registro().linea(Nivel::Aviso, "interfaz", 0, t); };
    if (!o_.umbralMs) o_.umbralMs = []() { return 2000; };
    if (!o_.cadaS) o_.cadaS = []() { return 60; };
    if (!o_.edadMs) return; // sin latido no hay nada que mirar
    {
        std::lock_guard<std::mutex> l(esperaMu_);
        corriendo_ = true;
    }
    hilo_ = std::thread([this]() {
        ba::platform::nombrarHilo("vigia-traba");
        bucle();
    });
}

void VigiaTraba::detener() {
    {
        std::lock_guard<std::mutex> l(esperaMu_);
        corriendo_ = false;
    }
    espera_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

json VigiaTraba::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json out{{"capturas", capturas_}};
    if (!ultima_.is_null()) out["ultima"] = ultima_;
    return out;
}

void VigiaTraba::bucle() {
    using reloj = std::chrono::steady_clock;
    bool enTraba = false, capturada = false;
    std::int64_t maxEdad = 0;
    reloj::time_point ultimaCaptura{};
    bool huboCaptura = false;
    for (;;) {
        {
            std::unique_lock<std::mutex> l(esperaMu_);
            espera_.wait_for(l, std::chrono::milliseconds(std::max(10, o_.sondeoMs)), [this]() { return !corriendo_; });
            if (!corriendo_) return;
        }
        const int umbral = o_.umbralMs();
        const std::int64_t edad = o_.edadMs();
        if (umbral <= 0 || edad < 0) {
            enTraba = capturada = false;
            continue;
        }
        if (edad >= umbral) {
            enTraba = true;
            maxEdad = std::max(maxEdad, edad);
            const auto cada = std::chrono::seconds(std::max(0, o_.cadaS()));
            if (!capturada && (!huboCaptura || reloj::now() - ultimaCaptura >= cada)) {
                capturada = true;
                huboCaptura = true;
                ultimaCaptura = reloj::now();
                capturar(edad, umbral);
            }
            continue;
        }
        // La interfaz volvió a latir: termina la traba (si había).
        if (enTraba && capturada) terminoLaTraba(maxEdad + o_.sondeoMs / 2);
        enTraba = capturada = false;
        maxEdad = 0;
    }
}

void VigiaTraba::capturar(std::int64_t edad, int umbral) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    const std::string corria = o_.corria ? o_.corria() : std::string();
    const std::string base = o_.proceso + "-" + fecha() + "-traba-" + std::to_string(edad);
    const json extra{{"trabada_ms", edad}, {"umbral_ms", umbral}, {"corria", corria}};
    const auto t0 = std::chrono::steady_clock::now();
    const CapturaPilas c = o_.capturar(o_.dir, base, "traba-interfaz", hiloInterfaz_,
                                       extra.dump(-1, ' ', false, json::error_handler_t::replace));
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    json ultima{{"trabada_ms", edad}, {"captura_ms", ms}, {"corria", corria}};
    if (c.archivo.empty()) {
        ultima["error"] = c.error;
        {
            std::lock_guard<std::mutex> l(mu_);
            ultima_ = std::move(ultima);
            ultimoJson_.clear();
        }
        o_.avisar(llenar(traducir("diag", "la interfaz lleva %1 ms trabada y no se pudieron sacar las pilas: %2"),
                         {std::to_string(edad), c.error}));
        return;
    }
    // Con nombres (el .pila de macOS y Linux): qué hacían la interfaz y el render, en una línea.
    std::string interfaz, render;
    if (c.archivo.extension() == ".pila") {
        const std::string texto = leer(c.archivo);
        interfaz = resumirPila(texto, hiloInterfaz_);
        if (const std::uint64_t r = hiloPorNombre(texto, "QSGRenderThread"); r != 0) render = resumirPila(texto, r);
        json cambios{{"captura_ms", ms}};
        if (!interfaz.empty()) cambios["resumen_interfaz"] = interfaz;
        if (!render.empty()) cambios["resumen_render"] = render;
        completarJson(c.json, cambios);
    }
    podar(c.archivo.parent_path(), o_.proceso);
    ultima["archivo"] = utf8(c.archivo);
    if (!interfaz.empty()) ultima["interfaz"] = interfaz;
    if (!render.empty()) ultima["render"] = render;
    {
        std::lock_guard<std::mutex> l(mu_);
        ++capturas_;
        ultima_ = std::move(ultima);
        ultimoJson_ = c.json;
    }
    std::string linea = llenar(traducir("diag", "la interfaz lleva %1 ms trabada: las pilas de todos los hilos quedaron en %2"),
                               {std::to_string(edad), utf8(c.archivo)});
    if (!interfaz.empty()) linea += llenar(traducir("diag", " · la interfaz: %1"), {interfaz});
    if (!render.empty()) linea += llenar(traducir("diag", " · el render de Qt: %1"), {render});
    if (!corria.empty() && corria != "?") linea += llenar(traducir("diag", " · corría: %1"), {corria});
    o_.avisar(linea);
}

void VigiaTraba::terminoLaTraba(std::int64_t duracionMs) {
    fs::path js;
    json ultima;
    {
        std::lock_guard<std::mutex> l(mu_);
        js = ultimoJson_;
        if (ultima_.is_object()) {
            ultima_["duracion_ms"] = duracionMs;
            ultima = ultima_;
        }
    }
    if (js.empty()) return;
    completarJson(js, {{"duracion_ms", duracionMs}});
    o_.avisar(ba::i18n::llenar(ba::i18n::traducir("diag", "la traba de la interfaz duró unos %1 ms (pilas en %2)"),
                               {std::to_string(duracionMs), ultima.value("archivo", std::string())}));
}

} // namespace ba::diag
