// ============================================================================
// src/motor/Vistas.hpp — imágenes chicas para la interfaz (M3.1)
//
// Lo que la GUI muestra (programa, preview, multiview, una entrada suelta) sale
// de acá, sin libobs: por clave, un pipeline
//
//   intersrc <productor> ! queue (tira lo viejo) ! videoconvert ! videoscale
//                        ! BGRA del tamaño pedido ! appsink (el último cuadro)
//
// (o NV12, si se pide: el canal del emisor aparte, F3-01)
//
// con el reloj del estudio y la latencia de lo que mira (así se ve junto con
// lo que suena). Se arma cuando alguien la pide (`pedir`) y se desarma cuando
// la sueltan todos (`soltar`). `ultimo` da el último cuadro si es más nuevo
// que el que ya tiene quien pregunta (número de serie).
//
// Un cuadro NO se copia (F2-13): lleva el buffer de GStreamer mismo, mapeado y
// con su referencia (Pixeles); la vista guarda el último y quien recibe el aviso
// o pregunta por él comparte esos bytes hasta que lo suelta. Lo convierte y lo
// escala la GPU (aSistemaEscalado): a la CPU baja una vez, ya chico y en BGRA.
//
// Claves: "programa", "preview", "limpio" (las composiciones de la salida por
// omisión), "programa:<salida>", "preview:<salida>", "limpio:<salida>" (las de
// otra salida) o "entrada:<id>"; las resuelve el motor. Una clave
// puede llevar «@<nombre>» para tener otra vista de lo mismo (otro tamaño): lo
// que va después de la @ no se mira para resolver.
// `avisar` registra una función que recibe CADA cuadro de esa clave (en el hilo
// de GStreamer, fuera del candado de las vistas): para mandar el programa al
// gateway o medir algo. Cada cuadro trae además la hora de lo que muestra en el
// reloj de los sellos entre procesos (contenidoQpc: el gateway alinea con eso el
// video del programa con su audio, F2-11).
//
// LAS QUE NADIE MIRA (F4-05, la política térmica «priorizar programa»): con
// `soltarSinMirar(true)`, una vista sin aviso que nadie leyó (`ultimo`) en un
// rato (kSinMirarUs de fábrica) se desarma («dormida»: sigue pedida, sin pipeline) y se vuelve a armar
// sola en el tic siguiente a que alguien la lea. Las que tienen aviso (el retorno, las
// alarmas, el emisor) no se tocan.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

/// Los bytes de un cuadro: el buffer de GStreamer mismo, mapeado para leer y con su
/// referencia (copiar un Pixeles es contar una referencia más, no copiar bytes). El buffer
/// se desmapea y se suelta con el último que lo tiene.
class Pixeles {
   public:
    Pixeles() = default;
    /// Una referencia a `b`, mapeado; `desde`/`bytes`: la parte que es el cuadro (0 = todo).
    Pixeles(GstBuffer* b, std::size_t desde, std::size_t bytes);
    const std::uint8_t* data() const { return datos_; }
    std::size_t size() const { return bytes_; }
    bool empty() const { return bytes_ == 0; }

   private:
    struct Mapa;
    std::shared_ptr<const Mapa> mapa_;
    const std::uint8_t* datos_ = nullptr;
    std::size_t bytes_ = 0;
};

struct Cuadro {
    Pixeles bgra; ///< BGRA, filas de `stride` bytes (en NV12: desde el plano Y hasta el final del UV)
    int ancho = 0, alto = 0, stride = 0;
    /// Una vista pedida en NV12: el plano UV empieza `desdeUv` bytes después del Y, con
    /// filas de `strideUv` bytes (alto/2 filas).
    bool nv12 = false;
    std::size_t desdeUv = 0;
    int strideUv = 0;
    std::uint64_t serie = 0;
    /// La hora de lo que muestra, en el reloj de los sellos (HealthBlock::nowQpc); 0 = no se sabe.
    std::uint64_t contenidoQpc = 0;
};

class Vistas {
   public:
    /// `resolver(clave, latencia)` da el productor de la clave ("" si no existe) y su latencia (ns).
    using Resolver = std::function<std::string(const std::string&, std::int64_t*)>;
    Vistas(Reloj& reloj, Resolver resolver);
    /// Las entradas que alguien está mirando («entrada:<id>@…»; las dormidas no).
    std::set<std::string> entradasEnUso() const;
    /// Lo que leen las vistas (sus productores; las dormidas no).
    std::set<std::string> productoresEnUso() const;
    /// Sin leer este rato, una vista se duerme (con soltarSinMirar).
    static constexpr std::int64_t kSinMirarUs = 10'000'000;
    /// Dormir (o no) las vistas que nadie mira en `sinMirarUs` (ver arriba). Al apagarlo,
    /// las dormidas vuelven en el tic siguiente.
    void soltarSinMirar(bool on, std::int64_t sinMirarUs = kSinMirarUs);
    ~Vistas();
    Vistas(const Vistas&) = delete;
    Vistas& operator=(const Vistas&) = delete;

    /// Alguien quiere ver `clave` a ese tamaño (cuenta pedidos) en `formato` («BGRA» o
    /// «NV12»). "" o el error.
    std::string pedir(const std::string& clave, int ancho, int alto, const std::string& formato = "BGRA");
    void soltar(const std::string& clave);
    /// El último cuadro de `clave` si su serie es mayor que `serie` (y la actualiza). Comparte
    /// los bytes con la vista (no los copia).
    bool ultimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const;
    /// Cada cuadro de `clave` a `fn` (en el hilo de datos). fn vacía = sacar.
    void avisar(const std::string& clave, std::function<void(const Cuadro&)> fn);
    /// true si alguna se durmió o se despertó (cambia lo que se usa).
    bool tic();
    nlohmann::json estado() const;
    /// Quién desarma los pipelines (Motor::desarmarAparte): pasarlos a NULL puede esperar
    /// al contexto de GL (en macOS, el hilo principal) y lo pedía el motor con su candado
    /// (200-500 ms al cerrar una vista previa). Sin él, acá mismo.
    void ponerDesarmador(std::function<void(std::function<void()>)> d);

   private:
    struct Una {
        GstElement* pipeline = nullptr;
        int pedidos = 0, ancho = 0, alto = 0;
        std::string formato = "BGRA";
        std::int64_t latencia = 0;
        std::string productor;
        Cuadro cuadro; ///< bajo mu_
        std::function<void(const Cuadro&)> aviso;
        std::int64_t leidoUs = 0, armadaUs = 0; ///< la última lectura (ultimo) y cuándo se armó
        bool dormida = false;                   ///< nadie la miraba: sin pipeline hasta que la lean
        // BA_MEDIR_VISTAS=1: cuadros por segundo y el hueco más largo, al registro cada 2 s.
        std::int64_t medirDesde = 0, ultimoUs = 0, huecoMaxUs = 0;
        int medidos = 0;
    };
    static GstFlowReturn alCuadro(GstElement* sink, gpointer d);
    void armar(const std::string& clave, Una& u);
    void desarmar(Una& u);

    Reloj& reloj_;
    Resolver resolver_;
    std::function<void(std::function<void()>)> desarmador_; ///< bajo mu_
    mutable std::mutex mu_;
    std::map<std::string, Una*> vistas_;
    bool sinMirar_ = false; ///< bajo mu_
    std::int64_t sinMirarUs_ = kSinMirarUs;
    std::uint64_t serie_ = 0;
};

} // namespace ba::motor
