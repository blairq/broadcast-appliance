// src/app/sala/ArchivosPrueba.cpp — ver el .hpp

#include "sala/ArchivosPrueba.hpp"

#include <QProcess>

#include <algorithm>
#include <fstream>
#include <system_error>

#include "persist/ColaEscritura.hpp"
#include "persist/Escritor.hpp"

namespace ba::app::sala::archivos {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string crearCarpetas(const std::vector<fs::path>& carpetas) {
    std::string primero;
    for (const fs::path& c : carpetas) {
        if (c.empty()) continue;
        std::error_code ec;
        fs::create_directories(c, ec);
        if (ec && primero.empty()) primero = c.string() + ": " + ec.message();
    }
    return primero;
}

std::string escribirCredenciales(const fs::path& archivo, const std::string& contenido) {
    if (const std::string e = crearCarpetas({archivo.parent_path()}); !e.empty()) return e;
    std::string err;
    if (!ba::persist::escribir(archivo, contenido, ba::persist::Durabilidad::Normal, ba::persist::Permisos::Privados, &err))
        return err.empty() ? archivo.string() : err;
    return {};
}

json informeHinchada(const fs::path& salida) {
    if (salida.empty()) return json::object();
    std::string s;
    if (!ba::persist::leerArchivo(salida / "hinchada" / "informe.json", s)) return json::object();
    const json todo = json::parse(s, nullptr, false);
    if (!todo.is_object()) return json::object();
    return {{"carpeta", (salida / "hinchada").string()},
            {"asamblea", todo.value("asamblea", json::object())},
            {"integrantes", todo.value("integrantes", json::array())},
            {"resumen", todo.value("resumen", json::object())},
            {"notas", todo.value("notas", json::array())}};
}

Tramo leerLineasDesde(const fs::path& archivo, std::int64_t desde, std::int64_t maxBytes) {
    Tramo t;
    t.hasta = std::max<std::int64_t>(0, desde);
    std::ifstream f(archivo, std::ios::binary);
    if (!f) return t;
    f.seekg(0, std::ios::end);
    const std::int64_t largo = static_cast<std::int64_t>(f.tellg());
    // Más corto que lo leído: lo volvieron a empezar.
    if (largo < t.hasta) t.hasta = 0;
    const std::int64_t cuanto = std::min(largo - t.hasta, std::max<std::int64_t>(0, maxBytes));
    if (cuanto <= 0) return t;
    std::string s(static_cast<std::size_t>(cuanto), '\0');
    f.seekg(t.hasta);
    f.read(s.data(), cuanto);
    s.resize(static_cast<std::size_t>(f.gcount()));
    const std::size_t fin = s.rfind('\n');
    if (fin == std::string::npos) {
        // Una línea entera más larga que maxBytes: se toma igual (si no, no avanzaría nunca).
        if (cuanto < maxBytes) return t;
        t.texto = std::move(s);
        t.hasta += static_cast<std::int64_t>(t.texto.size());
        return t;
    }
    s.resize(fin + 1);
    t.hasta += static_cast<std::int64_t>(s.size());
    t.texto = std::move(s);
    return t;
}

void borrar(const fs::path& archivo) {
    if (!archivo.empty()) ba::persist::borrar(archivo);
}

json leerJsonEncolado(const fs::path& archivo) {
    std::string s;
    if (!ba::persist::cola().leer(archivo, s)) return nullptr;
    return json::parse(s, nullptr, false);
}

void soltarProceso(QProcess* p, QObject* dueno) {
    if (!p) return;
    // Nada de lo que haga ya le llega al dueño (que se está yendo).
    if (dueno) QObject::disconnect(p, nullptr, dueno, nullptr);
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    // Fuera del árbol: ~QObject del dueño destruiría el QProcess y ~QProcess, con el hijo
    // vivo, hace kill() + waitForFinished() (la espera que no queremos en la interfaz).
    p->setParent(nullptr);
    // Se borra cuando el sistema avisa que terminó (si el bucle de eventos sigue andando).
    QObject::connect(p, &QProcess::finished, p, &QObject::deleteLater);
    // SIGKILL (TerminateProcess en Windows): no se puede ignorar, el hijo se va ya. No queda
    // HUÉRFANO (se mata antes de soltarlo, y su propia salida corta sus conexiones). No queda
    // ZOMBI: en macOS/Linux Qt lanza los hijos con forkfd, cuyo manejador de SIGCHLD recoge al
    // hijo apenas termina, haya o no bucle de eventos; y si el programa se está cerrando, al
    // irse nuestro proceso el sistema (launchd/init) recoge lo que quede. En Windows no hay
    // zombis (el handle se cierra al salir).
    p->kill();
}

} // namespace ba::app::sala::archivos
