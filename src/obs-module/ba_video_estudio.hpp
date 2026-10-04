// ============================================================================
// src/obs-module/ba_video_estudio.hpp — Lo que cruza entradas en el grafo de video
//
// El filtro `ba_video_grafo` de cada entrada corre SU sub-grafo. Lo que cruza
// entradas se arma con dos piezas:
//
//   TOMAS: un punto del grafo de una entrada publicado con nombre (el id del
//   nodo en la app, p. ej. «sony.gst»). Lo escribe el filtro de esa entrada
//   (paso `toma`) y lo lee quien lo necesite. Si en este cuadro nadie dibujó
//   esa entrada, quien lee la DESPIERTA (la dibuja aparte, y su filtro llena
//   la toma), como hacen las salidas.
//
//   COMPOSITORES: junta tomas en un lienzo (capas con posición, tamaño y
//   opacidad, en fracciones del lienzo). Se calcula UNA vez por cuadro, a
//   pedido de quien lo muestra: la fuente `ba_video_compositor` (en una
//   escena) o el paso `traer` de una entrada cuya escena recibe el compositor.
//   Su configuración viaja en esos ajustes y se registra por id.
//
// Todo esto corre en el hilo gráfico. Una entrada que ya se está dibujando
// no se despierta de nuevo (usa la toma del cuadro anterior): así no hay
// recursión aunque una entrada alimente un compositor que vuelve a ella.
// ============================================================================
#pragma once

#include <obs-module.h>

#include <cstdint>
#include <string>

namespace ba::video {

/// Copia `tex` (cx x cy) a la toma `nombre`, de la entrada `fuente`.
void publicarToma(const std::string& nombre, const std::string& fuente, gs_texture_t* tex, std::uint32_t cx,
                  std::uint32_t cy);
/// La toma `nombre` de este cuadro (despierta a `fuente` si hace falta), o
/// nullptr. `cx`/`cy`: su tamaño.
gs_texture_t* leerToma(const std::string& nombre, const std::string& fuente, std::uint32_t* cx,
                       std::uint32_t* cy);

/// La configuración del compositor `id` (JSON: {ancho, alto, fondo,
/// capas:[{toma, fuente, x, y, ancho, alto, opacidad}]}, en orden de abajo
/// hacia arriba; posiciones y tamaños en fracciones del lienzo).
void configurarCompositor(const std::string& id, const char* config);
/// El lienzo del compositor `id` en este cuadro, o nullptr.
gs_texture_t* componer(const std::string& id, std::uint32_t* cx, std::uint32_t* cy);

/// Dibuja la entrada `fuente` aparte, en el hilo gráfico (su filtro corre y
/// llena sus tomas y sus tarjetas de análisis). No hace nada si ya se está
/// dibujando.
void despertar(const std::string& fuente);
/// Mientras una entrada se dibuja (su filtro), para no despertarla de nuevo.
void dibujando(const std::string& fuente, bool si);

} // namespace ba::video

extern "C" void ba_register_video_estudio(void);
