// ============================================================================
// src/app/emision/DocumentoAire.hpp — el documento único de estado de aire (F3-04)
//
// docs/arquitectura/04-plan-unificado.md, F3-04 (02-P3). Había tres verdades sobre
// «qué está al aire», cada una con su archivo y su momento: Runtime/al-aire.json
// (escena, transmisión, grabación y destinos de cada salida, AppRuntime),
// Runtime/placa.json (el negro y la placa, SeguridadAire) y Runtime/linea-aire.json
// (el paso de la corrida y los HOLD, EventoViewModel). Un corte entre una escritura y
// otra dejaba al aparato volviendo con piezas de dos momentos distintos, y la
// restauración estaba repartida en tres lugares y tres demoras.
//
// Ahora hay UN documento (Runtime/al-aire.json, formato en emision/SalidasConfig.hpp)
// con secciones, y un solo dueño que lo escribe:
//
//   - Quien sabe algo lo PONE (ponerSalidas: el runtime, con lo que dice el motor;
//     ponerLinea: la línea de tiempo). Poner no toca el disco: compara con lo que hay
//     y, si cambió, despierta al dueño.
//   - El dueño (el plano de control, arranque/PlanoControl; sin él, el que pone)
//     llama `volcar`: el documento entero, de una, al hilo escritor en dos
//     generaciones (persist::cola().escribirGeneracion). Cada versión escrita es una
//     foto coherente de todas las secciones.
//   - Al arrancar, `leerAnterior` lee la generación más nueva que sirve. Con un
//     documento de la versión 1 suma placa.json y linea-aire.json como secciones (una
//     versión: el primer volcado los borra).
//
// Sin Qt ni motor (se prueba entero en test_documento_aire). Seguro entre hilos: lo
// ponen el hilo del motor y el de la interfaz; lo vuelca el del plano de control; lo
// leen todos (`foto`).
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "emision/SalidasConfig.hpp"
#include "persist/Generaciones.hpp"

namespace ba::app::salidas {

class DocumentoAire {
   public:
    /// `runtime`: la carpeta Runtime/ de los datos (ahí van al-aire.json y los viejos).
    explicit DocumentoAire(std::filesystem::path runtime = {});

    /// Cambia la carpeta (AppRuntime la sabe recién en configure). Antes de leer.
    void setCarpeta(std::filesystem::path runtime);

    /// Lo que dejó la corrida anterior.
    struct Anterior {
        AlAire aire;
        /// Cómo se encontró el documento (Nada = primer arranque).
        ba::persist::Lectura::Estado estado = ba::persist::Lectura::Estado::Nada;
        int descartadas = 0;      ///< generaciones rotas que se saltearon (un corte a mitad)
        bool danado = false;      ///< no se sabe qué había al aire: se trata como un corte
        bool lineaDanada = false; ///< la sección de la línea no se pudo leer (un linea-aire.json viejo roto)
        bool deVersionVieja = false; ///< se sumaron placa.json o linea-aire.json (versión 1)
    };
    /// Lee al-aire.json (dos generaciones) y, si es de la versión 1, las secciones
    /// sueltas. Deja eso mismo como contenido del documento (lo que siga poniendo cada
    /// uno lo pisa sección por sección), sin marcarlo para escribir.
    Anterior leerAnterior();

    /// Las salidas (escena, transmisión, grabación, destinos, placa) y si la corrida se
    /// está cerrando en orden. Reemplaza la sección entera (una salida que ya no está
    /// se va). true si cambió algo.
    bool ponerSalidas(std::map<std::string, Aire> salidas, bool cerrado);
    /// La parte viva de la línea (null = no corre). true si cambió algo.
    bool ponerLinea(nlohmann::json linea);

    /// Lo que dice el documento ahora (lo último puesto, se haya escrito o no).
    AlAire foto() const;
    /// ¿Sale algo (alguna salida transmite, graba o tiene destinos)? Según lo pedido.
    bool hayAire() const;

    /// Quién escribe: lo llama el documento (en el hilo del que pone) cada vez que algo
    /// cambió, para que el dueño llame `volcar` en el suyo. Sin dueño, `volcar` en el acto.
    void setDueno(std::function<void()> despertar);
    /// Escribe el documento entero si cambió desde la última vez (al hilo escritor, en dos
    /// generaciones, primero que los ajustes). true si encoló algo. Desde cualquier hilo.
    bool volcar();
    /// Cuántas versiones se encolaron (para el test y las métricas).
    std::uint64_t escrituras() const;

    std::filesystem::path archivo() const { return carpeta_ / "al-aire.json"; }
    std::filesystem::path archivoPlacaViejo() const { return carpeta_ / "placa.json"; }
    std::filesystem::path archivoLineaViejo() const { return carpeta_ / "linea-aire.json"; }

   private:
    void cambio(std::unique_lock<std::mutex>& l);

    std::filesystem::path carpeta_;
    mutable std::mutex mu_;
    AlAire doc_;
    std::string escrito_;       ///< lo último que se encoló (para no reescribir lo mismo)
    bool sucio_ = false;
    bool borrarViejos_ = false; ///< se leyó la versión 1: el primer volcado borra placa.json y linea-aire.json
    std::uint64_t escrituras_ = 0;
    std::function<void()> despertar_;
};

} // namespace ba::app::salidas
