// SPDX-License-Identifier: GPL-2.0-or-later
//
// BaWeb: el gateway lanza y vigila a ba-web (src/web), el servidor web público que va
// adelante del túnel. ba-web sirve el sitio del evento desde memoria con gzip, guarda un
// microcaché de /api/v1/evento y le pasa todo lo demás a la escucha del túnel del
// gateway. Es otro proceso a propósito: si los visitantes lo tapan o se cae, la sala y la
// emisión siguen; este supervisor lo vuelve a lanzar.
//
// Lo que le dice el gateway:
//   - su configuración (Runtime/ba-web.conf, con la sintaxis de nginx), escrita acá;
//   - la carpeta Web/ del evento en vivo (Runtime/evento-web.dir): una línea con la ruta,
//     o vacío si no hay evento. ba-web la relee cuando cambia (como mucho cada 1 s);
//   - una clave (BA_WEB_CLAVE, por el entorno) para mandar los ids de mirada que juntó
//     (X-BA-Vistos): así el «N mirando» cuenta también a los que atiende el caché.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "platform/Process.hpp"

namespace ba::gateway {

class Log;

class WebSupervisor {
   public:
    struct Cfg {
        std::string exe;           ///< bin/64bit/ba-web[.exe]
        std::string logPath;       ///< Logs/ba-web.log
        std::string dir;           ///< Runtime/ (la configuración y la carpeta del evento)
        std::string webroot;       ///< Web/guest (los invitados)
        std::string eventoWebroot; ///< Web/evento (vivo.js, marco…)
        uint16_t puerto = 8785;    ///< donde escucha (127.0.0.1)
        uint16_t upstream = 8786;  ///< la escucha del túnel del gateway
        int procesos = 0;          ///< 0 = auto
        int cacheMs = 1000;        ///< el microcaché de /api/v1/evento
        std::string clave;         ///< para X-BA-Vistos
    };
    WebSupervisor(Cfg cfg, Log& log);
    ~WebSupervisor();
    WebSupervisor(const WebSupervisor&) = delete;
    WebSupervisor& operator=(const WebSupervisor&) = delete;

    /// Escribe la configuración y lo lanza. false si falta el binario o no se pudo.
    bool start();
    void stop();
    bool vivo() const { return alive_.load(); }
    uint16_t puerto() const { return cfg_.puerto; }
    /// La carpeta Web/ del evento en vivo ("" si no hay). Escribe el archivo solo si cambió.
    void setEventoDir(const std::string& dir);
    /// La configuración que se le escribe (para la prueba y para mirarla).
    static std::string armarConf(const Cfg& c, const std::string& aliasFile, const std::string& logPath);

   private:
    bool spawn();
    void kill();
    void runLoop();

    Cfg cfg_;
    Log& log_;
    std::string confPath_, aliasPath_;
    std::mutex procLock_;
    ba::platform::Process proc_;
    std::thread thread_;
    std::atomic<bool> stop_{false}, started_{false}, alive_{false};
    std::atomic<int> restarts_{0};
    std::mutex dirLock_;
    std::string dirActual_ = "\x01"; // distinto de todo, para que la primera vez se escriba
};

} // namespace ba::gateway
