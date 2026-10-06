// ============================================================================
// src/motor/Motor.hpp — el motor: entradas, buses y el hilo que los atiende
//
// El dueño del reloj del estudio, las entradas y los buses de audio. La app (y
// más adelante la GUI por la red) le habla con comandos y lee su estado en JSON:
// nada de punteros a GStreamer afuera.
//
// LOS BUSES (cada uno un audiomixer en su pipeline):
//   programa    pista 1: lo que sale. Limitador del máster y loudness.
//   pista2…6    la grabación multipista: cada entrada va a las pistas que diga
//               su ruteo (la Matriz). Se arman cuando alguien las usa.
//   retornoN    lo que oye el invitado N en mix_minus (todo menos su voz).
//   directo     la sala sin demora, para la escucha «directo».
//
// QUÉ SUENA:
//   - Una entrada «global» (micrófonos, invitados, remoto, voces) suena siempre,
//     con su fader y su mute.
//   - Una entrada de escena suena solo si está en la escena al aire
//     (`escenaAlAire`), con un fundido al cambiar. Vale para el programa, las
//     pistas y los retornos.
//
// LOS RETORNOS (lo que oye cada invitado), uno por invitado:
//   - programa:   oye el programa (el gateway usa BA.Audio.ContentForReturn).
//   - mix_minus:  oye el programa SIN su propia voz → BA.Audio.Return.<n>.
//
// LA ESCUCHA (auriculares del operador): programa (lo que sale, junto con su
// video), directo (la sala sin demora: solo las globales), pfl (una entrada
// sola) o apagado.
// ============================================================================
#pragma once

#include "Entrada.hpp"
#include "Escucha.hpp"
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
        /// Un sink en vez de la salida de audio del sistema para la escucha (las
        /// pruebas: «fakesink sync=true», para no sonar por los parlantes).
        std::string sinkEscucha;
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
    ///            "nivel": lineal, "mute": bool, "pistas": [1…6] (de fábrica [1]: el programa)}
    std::string agregarEntrada(const std::string& id, const nlohmann::json& ajustes,
                               const nlohmann::json& opciones = nlohmann::json::object());
    std::string actualizarEntrada(const std::string& id, const nlohmann::json& ajustes);
    std::string quitarEntrada(const std::string& id);
    std::string ponerNivel(const std::string& id, double lineal);
    std::string ponerMute(const std::string& id, bool mute);
    /// A qué pistas va (1 = el programa; 2…6 = la grabación multipista).
    std::string ponerPistas(const std::string& id, const std::vector<int>& pistas);
    /// Las entradas de escena que están al aire (las demás de escena se apagan).
    void escenaAlAire(const std::vector<std::string>& ids, int fundidoMs);
    /// Controles de un medio: play, pausa, reiniciar, parar; `ir` con ms.
    std::string controlMedio(const std::string& id, const std::string& accion, std::int64_t ms = -1);

    // --- Retornos ---
    /// modo: programa | mix_minus | apagado; `propia` = la entrada del invitado.
    std::string retorno(int n, const std::string& modo, const std::string& propia);

    // --- La escucha ---
    /// modo: programa | directo | pfl | apagado; `id` = la entrada (pfl).
    std::string escuchar(const std::string& modo, const std::string& id = "");
    void escuchaDispositivo(const std::string& id);
    void escuchaNivel(double lineal);

    // --- El máster ---
    bool ponerLimitador(bool activo, double techoDb);
    void ponerTopeEstudio(std::int64_t ms);

    nlohmann::json estado() const;
    Reloj& reloj() { return *reloj_; }
    const Lienzo& lienzo() const { return cfg_.lienzo; }
    /// Lo que entrega un bus (para la grabación, más adelante): "bus:programa", "bus:pista2"…
    /// y cuánto tarda en salir (ns). "" si no existe.
    std::string productorDeBus(const std::string& bus, std::int64_t* latenciaNs = nullptr);

   private:
    struct Canal {
        std::unique_ptr<Entrada> entrada;
        bool global = false, enEscena = false;
        double nivel = 1.0;
        bool mute = false;
        unsigned pistas = 1; ///< bit 0 = programa, bit n-1 = pista n
    };
    struct Retorno {
        std::string modo, propia;
        std::unique_ptr<Mezcla> bus;
    };
    void bucle();
    /// Pone (o saca) una entrada en un bus con el estado del canal.
    void enBus(Mezcla& m, const std::string& id, const Canal& c, bool incluir, int fundidoMs);
    /// Lleva un canal a todos los buses donde corresponde.
    void aplicar(const std::string& id, int fundidoMs);
    Mezcla* pista(int n, bool crear);
    void armarRetorno(int n, Retorno& r);
    std::string anillo(const std::string& nombre) const { return cfg_.prefijoAnillos + nombre; }

    Config cfg_;
    std::unique_ptr<Reloj> reloj_;
    mutable std::recursive_mutex mu_;
    std::map<std::string, Canal> canales_;
    std::unique_ptr<Mezcla> programa_, directo_;
    std::map<int, std::unique_ptr<Mezcla>> pistas_;
    GstElement* puente_ = nullptr; ///< bus:programa → BA.Audio.ContentForReturn
    std::int64_t puenteLatNs_ = -1;
    std::map<int, Retorno> retornos_;
    std::unique_ptr<Escucha> escucha_;
    std::string escuchaModo_ = "apagado", escuchaId_;
    std::thread hilo_;
    std::atomic<bool> seguir_{false};
};

} // namespace ba::motor
