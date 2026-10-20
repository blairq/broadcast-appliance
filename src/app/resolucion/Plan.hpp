// ============================================================================
// src/app/resolucion/Plan.hpp — el plan de resolución de cada entrada (puro)
//
// docs/planes/PLAN-RESOLUCION.md, fase 2 (solo lectura: dice qué haría y por qué).
// La resolución se pide de atrás para adelante: cuánto se VE cada entrada (su
// rectángulo más grande en las escenas) y, tarjeta por tarjeta hacia la fuente,
// cuánto necesita cada una para entregar eso. Contra lo que corre de verdad (el
// tamaño que sale de cada tarjeta) salen los hallazgos, con un nivel que pinta la
// tarjeta en el analizador de latencia y en el constructor de Video:
//   mejorable (amarillo) · mal_armado (naranja) · mal (rojo).
// Lo arma ResolucionModel con lo que publican el motor, las escenas y los
// dispositivos; acá no se toca nada.
// ============================================================================
#pragma once

#include "ModosVideo.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ba::app::resolucion {

struct Tam {
    int ancho = 0, alto = 0;
    bool vacio() const { return ancho <= 0 || alto <= 0; }
    double area() const { return static_cast<double>(ancho) * alto; }
};

/// Una tarjeta de la cadena de video, en el orden en que corre.
struct Tarjeta {
    std::string id;        ///< el del nodo del grafo de video
    std::string titulo;
    std::string elemento;  ///< baptz, basuperres, quarktv…
    Tam sale;              ///< lo que negoció a su salida (vacío si no se sabe)
    double zoom = 1;       ///< PTZ: el zoom actual
    bool soloRam = false;  ///< sus pads solo aceptan memoria del sistema
    bool gpuPosible = false; ///< sabe trabajar en la GPU
};

struct Entrada {
    std::string id;
    Tam lienzo;
    double fps = 30;
    /// Dónde se ve: el rectángulo (en px del lienzo) de cada escena en que aparece.
    std::vector<std::pair<std::string, Tam>> seVe;
    Tam origen;                  ///< lo que entrega la fuente ahora
    double origenFps = 0;
    bool esCamara = false;
    bool fija = false;           ///< una imagen que no cambia (color, imagen, texto): los cps no importan
    std::vector<ba::motor::ModoVideo> modos; ///< los del dispositivo (si es cámara)
    nlohmann::json modoActual;   ///< el `modo_camara` del motor
    bool cadenaGpu = false;      ///< la cadena de la entrada corre en la GPU
    std::vector<Tarjeta> cadena;
};

/// La demanda: el mayor ancho y el mayor alto con que se ve (0×0 si no está en ninguna escena).
Tam demandaDe(const Entrada& e);

/// {entrada, demanda {ancho, alto, escenas}, origen, pasos: [{id, titulo, tipo, entra, sale,
///  real (el detalle de verdad que lleva: un zoom digital lo achica), necesita}], fuente:
///  {necesita {ancho, alto, cps}, actual, recomendado {…ModoVideo, porque}},
///  hallazgos: [{tipo, nivel, texto, tarjeta?, arreglo?: {accion, …}}],
///  niveles: {id de tarjeta | "entrada": {nivel, motivos: [texto]}}}.
nlohmann::json planear(const Entrada& e);

/// "mejorable" < "mal_armado" < "mal" (para quedarse con el peor).
int peso(const std::string& nivel);

} // namespace ba::app::resolucion
