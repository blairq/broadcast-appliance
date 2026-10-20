# Plan de resolución: cada entrada pide lo que se va a ver

Charlado el 2026-10-19. Problemas que van juntos:

- **El modo del dispositivo** se elegía mirando solo el lienzo y solo entre modos crudos. Una
  webcam USB 2 da 1080p a 30 solo en MJPEG; en Windows, Media Foundation lo disfrazaba de NV12
  (lo decodificaba en la CPU sin decirlo). La lista de modos no decía el formato.
- **La superres siempre duplica**: una cámara de 1920 sale a 3840 y el compositor la achica a
  1920 (medido en la PC: además en RAM, sube 4K a la GPU y `videorate` agrega 97 ms en 4K).
- **El PTZ digital** recorta y reescala al tamaño de entrada: zoom 3 sobre 1080p = «1080p» con
  detalle de 360p.
- **Nadie sabe cuánto se ve** cada entrada: un PiP de 640 se captura y procesa a 1080 o más.

## Idea: la resolución se pide de atrás para adelante

Una función pura (como `tiempos::armar`, testeable y explicable) arma el **Plan de resolución**
de cada entrada:

1. **Demanda**: el rectángulo más grande que ocupa en **todas** las escenas del evento (no
   renegocia al cortar), más las salidas que la usan sola (NDI, grabación aislada, la vista
   entera del PTZ).
2. **Contrato por tarjeta** — «para sacar S necesito E»: superres E = S/2 (S/4 en redes x4) con
   su tope; PTZ ventana = S × zoom y **sale a S**; recorte E = S / fracción; color, croma,
   rostros: E = S.
3. **La superres es el ancla** cuando su motor fija el tamaño (ONNX compila para un tamaño, CoreML
   tiene entradas fijas, VideoToolbox se configura por tamaño): se elige su entrada de lo que
   acepta, lo de atrás se adapta a entregar justo eso (las caps no se mueven nunca con el zoom) y
   lo de adelante escala a la demanda. FSR y GLSL no anclan (cualquier factor).
4. **Dos caminos con caps fijas** para el zoom: directo (el PTZ achica la ventana a S) y con
   superres (el PTZ entrega S/2, la superres duplica); un selector cambia en un cuadro cuando la
   ventana real baja de un umbral. Sin renegociar.
5. **Con PTZ la cámara va a su máxima resolución útil**: zoom sin pérdida hasta cámara/demanda;
   recién después inventa la superres.
6. **El orden importa**: PTZ antes que la superres (la superres recibe píxeles reales, nunca un
   recorte estirado). Filtros pesados de CPU antes de la superres (sobre la imagen chica).

## Colores de alarma en las tarjetas

En el analizador de latencia **y** en el constructor de la sección Video, desde el mismo modelo:

| Color | Qué | Ejemplos |
|---|---|---|
| verde | bien | |
| amarillo | se puede mejorar | entra más de lo que se pide (escalado al pedo), superres que sobra, conversión evitable, la cámara tiene un modo que alcanza |
| naranja | mal armado | PTZ después de la superres, filtro solo-RAM partiendo la cadena de GPU |
| rojo | anda mal | latencia que pasa el colchón, cuadros perdidos, zoom muy por encima de lo que da la cámara |

Lo estructural sale del plan (siempre, gratis); la latencia, de las sondas (a pedido). Cada
tarjeta con color: el motivo y, si hay, un botón de arreglo («Reordenar», «Usar 720p»,
«Apagar superres: sobra»).

## Herramientas de medición (qué se usa y para qué)

- **Sondas por elemento** (`motor::Sondas`, `gst_pad_add_probe`): a pedido y en caliente; lo que
  tarda y lo que retiene, memoria negociada y posible, latencia declarada. Es la base.
- **Tracers de GStreamer** (`latency`, y de gst-plugins-rs `buffer-lateness`, `queue-levels`,
  `pad-push-timings`, `pipeline-snapshot`; vienen en el bundle): se prenden al iniciar
  (`GST_TRACERS`) y son globales. Son el **diagnóstico profundo**: un botón «Diagnóstico
  profundo» reinicia la app con los tracers, junta unos segundos, vuelve a la normal y los
  muestra en Latencias al lado de las sondas. Para cuando algo anda muy mal.
- **Grafos del pipeline** (`gst_debug_bin_to_dot_data`): se pueden sacar en cualquier momento
  sin reiniciar; mostrarlos en la tarjeta de la entrada.
- **De punta a punta (T4)**: `GstReferenceTimestampMeta` con la hora de captura pegada a cada
  cuadro (exacto hasta cada salida local); la marca dibujada en el cuadro solo para lo que sale
  del proceso (TV, YouTube, invitados), donde la meta no sobrevive al codificador.

## Fases

1. **Modos de dispositivo completos** — HECHO y probado en la PC con la C910 (automático: 1080p
   MJPEG a 30 con jpegdec, 4 ms; 75 ms de la captura a la salida de la entrada) — 2026-10-19 (`motor/ModosVideo`): cualquier
   dispositivo de video; crudo/MJPEG/H.264/H.265; el decodificador de mayor rango del registro
   que acepte el formato (nada por marca); los crudos que fabrica el sistema marcados
   (`convertido`); «Automático» por costo con su porqué; `dispositivos_video` (como
   gst-device-monitor) y `modo_camara` en el estado de la entrada.
2. **Demanda + Plan como función pura** — HECHO 2026-10-19 (`app/resolucion`: `planear` puro con
   test_resolucion, `ResolucionModel` recalcula cada 2 s mientras Video o Latencias se miran;
   `resolucion_plan` por MCP). Colores en las tarjetas de las dos vistas (borde, «!» con los
   motivos, panel de la tarjeta elegida). Probado en la app: en PiP la superres y la entrada
   quedan amarillas; imágenes fijas y un invitado recortado, no (se compara la escala con que
   se ajusta, no el área). Falta: el arreglo con un botón (fase 3/5) y el nivel por latencia.
3. Aplicar: modo de cámara por demanda, PTZ que sale a la demanda.
4. Superres por demanda y ancla, los dos caminos, `transform_caps` en los dos sentidos.
5. Colores de latencia, botones de arreglo, diagnóstico profundo con tracers, T4.
