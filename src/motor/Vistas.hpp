// ============================================================================
// src/motor/Vistas.hpp — imágenes chicas para la interfaz (M3.1)
//
// Lo que la GUI muestra (programa, preview, multiview, una entrada suelta) sale
// de acá, sin libobs: por clave, un pipeline
//
//   intersrc <productor> ! queue (tira lo viejo) ! videoconvert ! videoscale
//                        ! BGRA del tamaño pedido ! appsink (el último cuadro)
//
// con el reloj del estudio y la latencia de lo que mira (así se ve junto con
// lo que suena). Se arma cuando alguien la pide (`pedir`) y se desarma cuando
// la sueltan todos (`soltar`). `ultimo` copia el último cuadro si es más nuevo
// que el que ya tiene quien pregunta (número de serie).
//
// Claves: "programa", "preview" (las composiciones) o "entrada:<id>".
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

struct Cuadro {
    std::vector<std::uint8_t> bgra;
    int ancho = 0, alto = 0, stride = 0;
    std::uint64_t serie = 0;
};

class Vistas {
   public:
    /// `resolver(clave, latencia)` da el productor de la clave ("" si no existe) y su latencia (ns).
    using Resolver = std::function<std::string(const std::string&, std::int64_t*)>;
    Vistas(Reloj& reloj, Resolver resolver);
    ~Vistas();
    Vistas(const Vistas&) = delete;
    Vistas& operator=(const Vistas&) = delete;

    /// Alguien quiere ver `clave` a ese tamaño (cuenta pedidos). "" o el error.
    std::string pedir(const std::string& clave, int ancho, int alto);
    void soltar(const std::string& clave);
    /// El último cuadro de `clave` si su serie es mayor que `serie` (y la actualiza).
    bool ultimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const;
    void tic();
    nlohmann::json estado() const;

   private:
    struct Una {
        GstElement* pipeline = nullptr;
        int pedidos = 0, ancho = 0, alto = 0;
        std::int64_t latencia = 0;
        std::string productor;
        Cuadro cuadro; ///< bajo mu_
    };
    static GstFlowReturn alCuadro(GstElement* sink, gpointer d);
    void armar(const std::string& clave, Una& u);
    void desarmar(Una& u);

    Reloj& reloj_;
    Resolver resolver_;
    mutable std::mutex mu_;
    std::map<std::string, Una*> vistas_;
    std::uint64_t serie_ = 0;
};

} // namespace ba::motor
