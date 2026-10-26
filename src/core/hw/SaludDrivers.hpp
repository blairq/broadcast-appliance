// ============================================================================
// src/core/hw/SaludDrivers.hpp — qué drivers anduvieron en esta máquina y
// cuáles quedaron en cuarentena
//
// UN registro para todos los drivers y proveedores de dispositivos: los de
// GStreamer (WASAPI, Media Foundation, Core Audio, AVFoundation, Pulse, V4L2,
// DeckLink, lo que agregue un plugin), cada driver ASIO instalado, el runtime
// de NDI. De cada uno: cuántas veces seguidas falló, cómo (error, tiempo
// agotado, se cayó el proceso que lo probaba), el último motivo en castellano y
// cuándo.
//
// POR QUÉ. Un driver de terceros que se cuelga o tumba el proceso no se arregla
// desde acá; lo que sí se puede es DEJAR DE CARGARLO. En una notebook con
// JackRouter y SAR (sobre JACK, sin el servidor corriendo) el proveedor de ASIO
// los cargaba en cada enumeración: 416 veces en 12 minutos, con la interfaz
// trabada de a 3-12 s. A las `umbral` fallas seguidas (4 de fábrica) el driver
// queda EN CUARENTENA: no se vuelve a cargar solo (ni al arrancar, ni en cada
// cambio de dispositivos). Un éxito pone el contador en cero. «Probar de nuevo»
// lo deja probar UNA vez más: si anda sale de la cuarentena; si no, se queda.
//
// POR MÁQUINA. El disco es portable: el archivo (<datos>/Config/drivers.json)
// guarda una sección por huella de máquina (platform/Maquina.hpp). En una
// máquina que no se conoce se prueba todo; en una conocida, solo lo NUEVO (un
// driver que no estaba, o que cambió de versión o de archivo) y lo que venía
// fallando sin llegar a la cuarentena. Lo que anduvo se usa directo, sin volver
// a probarlo; lo que ya no está se marca ausente (no es una falla).
//
// Sin Qt a propósito (std y nlohmann): lo lee también platform/GstEntorno antes
// de gst_init (los proveedores en cuarentena no se cargan ni una vez) y se
// prueba solo (test_salud_drivers).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ba::hw {

/// Cómo falló una prueba.
enum class Falla {
    Error,  ///< contestó que no anda (no arrancó, no se pudo cargar)
    Tiempo, ///< no contestó en el tiempo límite (se colgó)
    Caida,  ///< se cayó el proceso que lo estaba probando
};
const char* nombreFalla(Falla f); ///< "error" | "tiempo" | "caida"

/// Un driver tal como lo lista la sonda (sin cargarlo).
struct DriverListado {
    std::string id;     ///< «gst:wasapi2deviceprovider», «asio:{CLSID}», «ndi:runtime»
    std::string nombre; ///< para mostrar
    std::string tipo;   ///< gstreamer | asio | ndi | prueba
    std::string firma;  ///< versión/archivo: si cambia, se lo vuelve a probar
    int rango = 0;      ///< el de GStreamer (para devolvérselo al salir de la cuarentena)
};

struct EstadoDriver {
    std::string id, nombre, tipo, firma;
    int rango = 0;
    /// sin_probar | anda | falla (falló menos de `umbral` veces seguidas) | cuarentena
    std::string estado = "sin_probar";
    int fallas = 0;      ///< seguidas (un éxito las pone en cero)
    int fallasTotal = 0; ///< todas las que tuvo en esta máquina
    std::string ultimaFalla; ///< error | tiempo | caida
    std::string motivo;      ///< el de la última falla, legible
    std::string detalle;     ///< lo del último éxito («2 entradas, 2 salidas»)
    std::string cuando;      ///< el último resultado (ISO)
    std::string ultimoOk;    ///< el último éxito (ISO)
    bool presente = true;    ///< está instalado ahora (false: ausente, no es una falla)
    bool unaVez = false;     ///< «Probar de nuevo» pendiente (no se guarda)

    nlohmann::json comoJson() const;
    static EstadoDriver deJson(const std::string& id, const nlohmann::json& j);
};

class SaludDrivers {
   public:
    /// `archivo` vacío: en memoria. `etiqueta`: cómo se muestra la máquina (modelo).
    SaludDrivers(std::filesystem::path archivo, std::string huella, std::string etiqueta = {});

    void leer();
    const std::string& huella() const { return huella_; }

    /// ¿Ya se probó todo en esta máquina alguna vez (un sondeo completo terminado)?
    bool maquinaConocida() const { return !sondeoCompleto_.empty(); }
    /// Al terminar el primer sondeo completo: la máquina pasa a conocida.
    void sondeoCompleto(const std::string& cuando);

    void setUmbral(int n) { umbral_ = n < 1 ? 1 : n; }
    int umbral() const { return umbral_; }

    /// Lo que hay instalado ahora → los ids que hay que probar. Anota los nuevos,
    /// actualiza nombre y rango, marca ausentes los que no están. Se prueba: lo
    /// nuevo, lo que cambió de firma (sale de la cuarentena: es otra versión) y lo
    /// que venía fallando sin llegar a la cuarentena. NO: lo que anduvo con la
    /// misma firma, ni lo que está en cuarentena.
    std::vector<std::string> novedades(const std::vector<DriverListado>& hay);

    void exito(const std::string& id, const std::string& detalle, const std::string& cuando);
    /// true si con esta falla QUEDÓ en cuarentena (no estaba).
    bool falla(const std::string& id, Falla tipo, const std::string& motivo, const std::string& cuando);
    /// No está instalado (la sonda no lo encontró): ausente, sin contar falla.
    void ausente(const std::string& id);

    /// «Probar de nuevo»: lo habilita para UNA prueba. "" o el error.
    std::string reintentar(const std::string& id);
    bool enCuarentena(const std::string& id) const;
    /// ¿Se lo puede cargar? Todo lo que no está en cuarentena, y el que tiene
    /// un «Probar de nuevo» pendiente.
    bool sePuedeCargar(const std::string& id) const;

    std::vector<EstadoDriver> todos() const;
    std::optional<EstadoDriver> uno(const std::string& id) const;
    /// Los que andan, presentes (para el resumen «N drivers listos»).
    int cuantosAndan() const;
    int cuantosEnCuarentena() const;

    /// Los ids en cuarentena de `huella` que empiezan con `prefijo`, leídos del
    /// archivo sin armar nada (para GstEntorno, antes de gst_init).
    static std::vector<std::string> cuarentenaDe(const std::filesystem::path& archivo, const std::string& huella,
                                                 const std::string& prefijo);

   private:
    void guardar() const;
    std::filesystem::path archivo_;
    std::string huella_, etiqueta_;
    std::string primera_, sondeoCompleto_;
    int umbral_ = 4;
    std::map<std::string, EstadoDriver> drivers_;
    nlohmann::json otras_ = nlohmann::json::object(); ///< las otras máquinas, tal cual
};

/// El motivo de una falla en castellano, para el operador. `mensaje`: lo que
/// contestó la sonda; `salida`: lo que el driver escribió por stderr mientras
/// se lo probaba (JackRouter dice ahí «is JACK server running?»).
std::string motivoLegible(Falla tipo, const std::string& nombre, const std::string& mensaje,
                          const std::string& salida, int limiteMs);

} // namespace ba::hw
