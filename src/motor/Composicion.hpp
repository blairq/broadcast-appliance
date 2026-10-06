// ============================================================================
// src/motor/Composicion.hpp — un bus de video del motor (PROGRAMA, PREVIEW…)
//
// Un pipeline propio por bus:
//
//   intersrc <id>:video ! queue ! videocrop ─► compositor (pad: x, y, ancho, alto, alfa, orden)
//   intersrc <id>:video ! queue ! videocrop ─►     │
//   …                                              ▼
//                         caps del lienzo ! intersink video:<nombre>
//
// - Reloj y base_time del estudio; en vivo aunque no haya capas (force-live),
//   arranca con la primera que llega (start-time-selection=first) y espera la
//   latencia del estudio + margen (min-upstream-latency): una entrada lenta que
//   se enchufa andando no llega tarde.
// - ESCENA = un conjunto de capas (qué entrada, dónde, de qué tamaño, recorte,
//   alfa, orden). `mostrar` pone una escena con una TRANSICIÓN:
//     corte   la nueva aparece entera; la vieja se saca recién cuando la nueva
//             ya dio su primer cuadro (nada de un cuadro negro en el medio).
//     mezcla  la nueva entra encima con su alfa de 0 a 1 en `ms`; después se saca la vieja.
//   Mostrar la MISMA escena otra vez (mismo nombre) actualiza las capas en el
//   lugar: posición, tamaño, recorte y alfa cambian andando, sin rearmar.
// - Una capa cuya entrada no da video no se ve (el pad no tiene datos): la placa
//   «sin señal» la pone la entrada (M5).
// ============================================================================
#pragma once

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

struct Capa {
    std::string entrada;           ///< la entrada del motor (lee <entrada>:video); también el nombre de la capa
    std::string productor;         ///< si no está vacío, lee este productor (p. ej. «video:programa» en el multiview)
    int x = 0, y = 0;
    int ancho = 0, alto = 0;       ///< 0 = el del lienzo
    double alfa = 1.0;
    int orden = 0;                 ///< más alto = más arriba
    int recIzq = 0, recArr = 0, recDer = 0, recAba = 0; ///< recorte en píxeles de la entrada
    bool visible = true;
};

struct Escena {
    std::string nombre;
    std::vector<Capa> capas;
    static Escena desdeJson(const nlohmann::json& j);
    nlohmann::json aJson() const;
};

class Composicion {
   public:
    struct Opciones {
        bool gpu = false; ///< glvideomixer (en la placa) en vez de compositor (CPU)
    };
    Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre, Opciones op);
    Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre)
        : Composicion(reloj, lienzo, std::move(nombre), Opciones{}) {}
    ~Composicion();
    Composicion(const Composicion&) = delete;
    Composicion& operator=(const Composicion&) = delete;

    const std::string& nombre() const { return nombre_; }
    std::string productor() const { return "video:" + nombre_; }
    bool anda() const { return pipeline_ != nullptr; }
    const std::string& error() const { return error_; }

    /// Pone una escena: transición «corte» o «mezcla» con `ms`. Misma escena: actualiza en el lugar.
    void mostrar(const Escena& e, const std::string& transicion = "corte", int ms = 0);
    const std::string& escena() const { return escena_.nombre; }
    /// ¿Hay una transición en curso?
    bool enTransicion() const;

    void tic();
    nlohmann::json estado() const;
    /// Cuánto después de su hora sale lo que entrega (ns).
    std::int64_t latenciaSalidaNs() const;

   private:
    struct Rama {
        std::string entrada, productor;
        GstElement* src = nullptr;
        GstElement* cola = nullptr;
        GstElement* recorte = nullptr;
        GstElement* subir = nullptr; ///< glupload (gpu)
        GstPad* pad = nullptr;
        int generacion = 0;
        std::atomic<bool> listo{false}; ///< ya pasó su primer cuadro (lo pone el hilo de datos)
        double alfaHacia = 1.0;
        int orden = 0;
    };
    void aplicarLatencia();
    void ponerCapa(Rama& r, const Capa& c, double alfa);
    Rama* crearRama(const Capa& c, int gen);
    void soltar(Rama& r);
    void sacarGeneracion(int gen);
    static GstPadProbeReturn alPrimerCuadro(GstPad* pad, GstPadProbeInfo* info, gpointer d);

    Reloj& reloj_;
    Lienzo lienzo_;
    std::string nombre_, error_;
    bool gpu_ = false;
    GstElement* pipeline_ = nullptr;
    GstElement* mezclador_ = nullptr;
    mutable std::mutex mu_;
    std::map<std::string, Rama*> ramas_; ///< clave: <generación>/<entrada>
    int generacion_ = 0;
    Escena escena_;
    // La transición en curso
    int genVieja_ = -1;
    std::string transicion_;
    std::int64_t transDesdeNs_ = 0, transNs_ = 0;
    std::int64_t latenciaNs_ = -1, salidaNs_ = 0, consultaNs_ = 0;
};

} // namespace ba::motor
