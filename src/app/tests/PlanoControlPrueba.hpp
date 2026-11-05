// ============================================================================
// src/app/tests/PlanoControlPrueba.hpp — lo común de las partes de test_plano_control
//
// test_plano_control era un solo .cpp de 2300 líneas (el más lento del build: ~20 s). Sigue
// siendo UN programa y una sola corrida, en el mismo orden, pero cada tema en su archivo
// (plano_control_<tema>.cpp), que se compilan a la vez. Lo que arma el principio (el plano, el
// MCP, el vigía) va en el Banco; cada parte recibe además `resto`: lo que sigue del test, que
// llama AL FINAL con todo lo suyo todavía vivo (lo que adoptó el plano, las herramientas y
// las alarmas que dejó registradas), como cuando todo era un solo main.
// ============================================================================
#pragma once

#include "agent/AlertEngine.hpp"
#include "arranque/PlanoControl.hpp"
#include "emision/TurnoMotor.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpHttp.hpp"
#include "tests/Puntualidad.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::pruebas::plano {

using json = nlohmann::json;
namespace fs = std::filesystem;

inline int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++::ba::app::pruebas::plano::g_fallos;                                       \
        } else {                                                                         \
            std::printf("ok: %s\n", msg);                                                \
        }                                                                                \
    } while (0)
/// Un chequeo de TIEMPO: un tope en ms de reloj («en el acto») o cuántas vueltas dio un reloj
/// durante una traba. Con la máquina cargada el planificador atrasa a todos y eso no es del
/// plano de control: si solo fallan estos y la máquina estaba impuntual, SKIP con el motivo
/// (tests/Puntualidad.hpp). Lo que se hizo, dónde y con qué turno son chequeos comunes.
inline int g_fallosTiempo = 0;
#define CHECK_TIEMPO(cond, msg)                                                          \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO (de tiempo): %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++::ba::app::pruebas::plano::g_fallosTiempo;                                 \
        } else {                                                                         \
            std::printf("ok: %s\n", msg);                                                \
        }                                                                                \
    } while (0)

/// Un pedido HTTP bloqueante (desde un hilo sin bucle de eventos). Devuelve el cuerpo.
inline std::string pedir(quint16 puerto, const std::string& metodo, const std::string& ruta, const std::string& token,
                         const std::string& cuerpo = {}) {
    QTcpSocket s;
    s.connectToHost("127.0.0.1", puerto);
    if (!s.waitForConnected(3000)) return "SIN CONEXION";
    std::string req = metodo + " " + ruta + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
    s.write(req.data(), static_cast<qint64>(req.size()));
    s.waitForBytesWritten(3000);
    QByteArray todo;
    while (s.waitForReadyRead(20000)) todo += s.readAll();
    todo += s.readAll();
    const int fin = todo.indexOf("\r\n\r\n");
    return fin < 0 ? std::string() : todo.mid(fin + 4).toStdString();
}

inline std::string llamar(quint16 puerto, const std::string& token, const std::string& herramienta, int id,
                          const json& args = json::object()) {
    const json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", herramienta}, {"arguments", args}}}};
    return pedir(puerto, "POST", "/mcp", token, m.dump());
}

/// Cada archivo de la carpeta de datos con su contenido (para ver que nada cambió).
inline std::map<std::string, std::string> fotoDeArchivos(const fs::path& raiz) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(raiz, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file()) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::stringstream c;
        c << f.rdbuf();
        out[fs::relative(it->path(), raiz).generic_string()] = c.str();
    }
    return out;
}

inline double msDesde(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

/// La interfaz trabada: el hilo principal no atiende eventos durante `ms`.
inline void trabarInterfaz(int ms) {
    const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    volatile std::uint64_t x = 0;
    while (std::chrono::steady_clock::now() < hasta) x = x + 1;
}

/// La interfaz libre `ms` (atiende eventos).
inline void atender(int ms) {
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}

/// Lo que arma el principio del test y usan las partes; y lo que una parte deja para otra.
struct Banco {
    QCoreApplication& app;
    const QThread* interfaz;
    std::atomic<int>& control;
    std::atomic<int>& motor;
    std::atomic<int>& latido;
    PlanoControl& plano;
    TurnoMotor& turno;
    mcp::McpCore& core;
    QThreadPool& pool;
    const std::string& token;
    mcp::McpHttp& http;
    agent::AlertEngine& vigia;
    std::atomic<int>& disparos;
    // Lo que adoptó el plano en una parte y el final mira que vuelva a la interfaz (vive hasta
    // el final: ver `resto`).
    agent::AlertEngine* vigia9 = nullptr; ///< 9: el vigía con las entradas de la interfaz
    agent::AlertEngine* vigiaM = nullptr; ///< M: el vigía del audio
    QObject* grafoI = nullptr;            ///< I: el grafo del dueño
    agent::AlertEngine* vigiaI = nullptr; ///< I: su vigía
};

using Resto = std::function<void()>;

/// 7 y 9: las herramientas de la foto y lo que solo sabe la interfaz (plano_control_foto.cpp).
void probarFoto(Banco& b, const Resto& resto);
/// 8 y 10: las de aire y la ventanilla (plano_control_aire.cpp).
void probarAire(Banco& b, const Resto& resto);
/// S y C: asamblea, sala y línea; control y DSK (plano_control_sala.cpp).
void probarSala(Banco& b, const Resto& resto);
/// M: la mezcla (plano_control_mezcla.cpp).
void probarMezcla(Banco& b, const Resto& resto);
/// R, E y R2: la configuración, la carpeta del evento, la energía (plano_control_resto.cpp).
void probarResto(Banco& b, const Resto& resto);
/// I: la imagen (plano_control_imagen.cpp).
void probarImagen(Banco& b, const Resto& resto);
/// Lo último: el plano se detiene, T y el DSK por escena (test_plano_control.cpp). El turno anota
/// el lugar por la función (TurnoMotor::lugar): «pruebas::plano::probarFinal», «…probarAire».
void probarFinal(Banco& b);

} // namespace ba::app::pruebas::plano
