// ============================================================================
// src/motor/Motor.hpp — el motor: entradas, buses y el hilo que los atiende
//
// El dueño del reloj del estudio, las entradas y los buses de audio. La app (y
// más adelante la GUI por la red) le habla con comandos y lee su estado en JSON:
// nada de punteros a GStreamer afuera.
//
// EL AUDIO DEL PROGRAMA:
//   - Una entrada «global» (micrófonos, invitados, remoto, voces) suena siempre
//     en el programa, con su fader y su mute.
//   - Una entrada de escena suena solo si está en la escena al aire
//     (`escenaAlAire`), con un fundido al cambiar.
//   - El programa sale por el limitador del máster y se mide su loudness.
//
// LOS RETORNOS (lo que oye cada invitado), uno por invitado:
//   - programa:   oye el programa (el gateway usa BA.Audio.ContentForReturn).
//   - mix_minus:  oye el programa SIN su propia voz: un bus propio con todas las
//                 entradas del programa menos la suya, con los mismos faders,
//                 mutes y escena, que sale a BA.Audio.Return.<n>.
// ============================================================================
#pragma once

#include "Entrada.hpp"
#include "Mezcla.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ba::motor {

class Motor {
   public:
    struct Config {
        Lienzo lienzo;
        std::string reloj = "sistema", relojServidor;
        int relojPuerto = 0, relojDominio = 0;
        std::int64_t topeMs = -1; ///< -1 = automático (250 ms)
        /// Cómo empiezan los anillos del gateway (las pruebas usan otro, para no
        /// pisar los de una app andando). "" = sin gateway: no se escriben.
        std::string prefijoAnillos = "BA.Audio.";
    };

    explicit Motor(Config c);
    ~Motor();
    Motor(const Motor&) = delete;
    Motor& operator=(const Motor&) = delete;

    /// Arranca el bus PROGRAMA, el puente al gateway y el hilo. "" o el error.
    std::string iniciar();
    void detener();
    bool andando() const { return hilo_.joinable(); }

    // --- Entradas ---
    /// opciones: {"global": bool (suena siempre), "en_escena": bool (de fábrica: global),
    ///            "nivel": lineal, "mute": bool, "audio": bool (al programa; de fábrica true)}
    std::string agregarEntrada(const std::string& id, const nlohmann::json& ajustes,
                               const nlohmann::json& opciones = nlohmann::json::object());
    std::string actualizarEntrada(const std::string& id, const nlohmann::json& ajustes);
    std::string quitarEntrada(const std::string& id);
    std::string ponerNivel(const std::string& id, double lineal);
    std::string ponerMute(const std::string& id, bool mute);
    /// Las entradas de escena que están al aire (las demás de escena se apagan).
    void escenaAlAire(const std::vector<std::string>& ids, int fundidoMs);
    /// Controles de un medio: play, pausa, reiniciar, parar; `ir` con ms.
    std::string controlMedio(const std::string& id, const std::string& accion, std::int64_t ms = -1);

    // --- Retornos ---
    /// modo: programa | mix_minus | apagado; `propia` = la entrada del invitado.
    std::string retorno(int n, const std::string& modo, const std::string& propia);

    // --- El máster ---
    bool ponerLimitador(bool activo, double techoDb);
    void ponerTopeEstudio(std::int64_t ms);

    nlohmann::json estado() const;
    Reloj& reloj() { return *reloj_; }
    const Lienzo& lienzo() const { return cfg_.lienzo; }

   private:
    struct Canal {
        std::unique_ptr<Entrada> entrada;
        bool global = false, enEscena = false, alPrograma = true;
        double nivel = 1.0;
        bool mute = false;
    };
    struct Retorno {
        std::string modo, propia;
        std::unique_ptr<Mezcla> bus;
    };
    void bucle();
    /// Pone una entrada en un bus con el estado del canal.
    void sumar(Mezcla& m, const std::string& id, const Canal& c, int fundidoMs);
    void armarRetorno(int n, Retorno& r);
    std::string anillo(const std::string& nombre) const { return cfg_.prefijoAnillos + nombre; }

    Config cfg_;
    std::unique_ptr<Reloj> reloj_;
    mutable std::recursive_mutex mu_;
    std::map<std::string, Canal> canales_;
    std::unique_ptr<Mezcla> programa_;
    GstElement* puente_ = nullptr; ///< bus:programa → BA.Audio.ContentForReturn
    std::map<int, Retorno> retornos_;
    std::thread hilo_;
    std::atomic<bool> seguir_{false};
};

} // namespace ba::motor
