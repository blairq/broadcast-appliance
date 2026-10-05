# Decisiones pendientes (para el usuario)

Cosas del plan (docs/TAREAS-INFORME-EXPERTO.md) donde hay que elegir un camino antes de seguir.
Cada una dice qué opciones hay, qué cuesta cada una y qué recomiendo.

## T12 · Limitador de seguridad en el máster

libobs no tiene un filtro de máster: la mezcla de programa (pista 1) se suma y va directo a los
codificadores. El loudness (T11) ya se mide sobre esa mezcla, pero para LIMITARLA hay que meterse en el
camino. Opciones:

| Opción | Cómo | Costo | Latencia |
|---|---|---|---|
| A. Parche a libobs | `audio_output_set_mix_filter` en `media-io/audio-io.c`, antes del recorte a ±1 y de los codificadores | Mac y Linux compilan libobs desde fuente (fácil). **Windows usa el `obs.dll` precompilado de OBS**: habría que compilar libobs para Windows (MinGW o MSVC en Docker) | 0 ms (lookahead del limitador: 1,5 ms) |
| B. Bus máster por la matriz | Todas las fuentes de programa a una «entrada virtual» Máster con el limitador, y esa a la pista 1 | Sin tocar libobs; cambia el ruteo de todo el programa | +20-30 ms al audio del programa (hay que compensarlo en el video) |
| C. Limitador por fuente | Un filtro limitador en cada fuente que va al programa | Fácil | 0 ms, pero la SUMA puede pasarse igual: no es un máster |

**Recomendación:** A. Es la única que limita de verdad lo que sale sin agregar latencia. Hoy el
loudness avisa (disparador de fábrica «Loudness fuera de objetivo» y el true peak en rojo en el
Mezclador), así que no hay apuro de aire.
