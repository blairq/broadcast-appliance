// ============================================================================
// src/app/dispositivos/SondaDrivers.hpp — lo que corre ADENTRO del proceso
// hijo que prueba los drivers
//
// Cargar un driver de terceros (un ASIO, un proveedor de dispositivos de
// GStreamer) puede colgarse o tumbar el proceso. Por eso no se prueba en la
// app: la app se lanza a sí misma con `--sonda-drivers` (ba-app ya está en el
// bundle de los tres sistemas, con su GStreamer y sus DLL al lado: no hay otro
// ejecutable que empaquetar) y este proceso hijo hace una cosa por vez y la
// cuenta por stdout, una línea JSON por evento:
//
//   ba-app --sonda-drivers listar
//     {"driver":{"id","nombre","tipo","firma","rango"}}   (uno por driver, SIN cargarlo)
//     {"fin":true}
//   ba-app --sonda-drivers probar <id> <id>…
//     {"empieza":"<id>"}
//     {"resultado":"<id>","ok":true,"detalle":"…","dispositivos":[…]}
//     {"resultado":"<id>","ok":false,"motivo":"…"}        (o "ausente":true)
//     {"fin":true}
//
// Si un driver se cuelga, la app (dispositivos/Sondeo) ve un «empieza» sin su
// «resultado» y lo mata al vencer el tiempo límite; si se cae, ve el proceso
// terminar sin «fin». En los dos casos sabe CUÁL fue y sigue con los demás en
// otro proceso.
//
// Las FUENTES dicen qué drivers hay y cómo se prueba cada uno: GStreamer y ASIO
// en la app (SondaReal.hpp); la de PRUEBA (drivers de mentira que andan, fallan,
// se cuelgan o se caen) para los tests de los tres sistemas, sin hardware.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ba::app::dispositivos::sonda {

/// El primer argumento que pone a ba-app (o a un test) en modo sonda.
constexpr const char* kBandera = "--sonda-drivers";

struct Fuente {
    /// Lo que hace falta antes de listar o probar (gst_init…). Una vez, si hace falta.
    std::function<void()> preparar;
    /// Los drivers instalados, sin cargarlos: [{id, nombre, tipo, firma, rango}].
    std::function<std::vector<nlohmann::json>()> listar;
    /// ¿Este id es de esta fuente?
    std::function<bool(const std::string& id)> esMia;
    /// Lo carga y lo prueba: {ok, detalle?, dispositivos?} o {ok:false, motivo} o {ausente:true}.
    std::function<nlohmann::json(const std::string& id)> probar;
};

/// Los drivers de mentira. Se listan solo si BA_SONDA_FALSOS tiene una lista
/// («anda,error,cuelga,cae,jack»); se prueban siempre que el id sea «falso:<cómo>».
/// La firma es BA_SONDA_FALSOS_FIRMA (o «1»): cambiarla simula otra versión.
/// Con BA_SONDA_FALSOS_DIARIO, cada prueba anota su id en ese archivo.
Fuente fuenteDePrueba();

/// El proceso hijo: `argv[1]` es kBandera. Devuelve el código de salida.
int principal(int argc, char** argv, const std::vector<Fuente>& fuentes);

} // namespace ba::app::dispositivos::sonda
