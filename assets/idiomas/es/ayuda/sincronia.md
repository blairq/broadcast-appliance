## La idea
Las entradas **sincronizadas** (clase `sala`: cámaras, capturadoras y micrófonos del lugar) salen alineadas entre sí y con el audio: el estudio espera a la más lenta. Por eso la latencia del estudio la marca UNA entrada (la más lenta de las sincronizadas): para bajarla hay que mejorar esa. Las de clase `propia` (invitados por WebRTC, sesión remota, ventanas, pantallas compartidas) no arrastran a las demás: si llegan tarde se les corre la hora, audio y video juntos.

La latencia **no se mueve con cada corte de escena**: cambiarla en vivo congela o saltea las salidas. En vivo solo sube (si lo piden sostenido 2 s, una vez, avisado); baja sola sin nada al aire o a pedido.

## Cómo se trabaja
1. `estudio_latencia`: la latencia fijada, `la_marca` y cada entrada con su clase, lo que pide y lo que tarda.
2. Si la marca algo que no tiene que alinearse con la sala (un invitado, una ventana, una cámara que toma otra cosa): `sincronia_clase {"entrada", "clase": "propia"}` (fuera del aire: la entrada se rearma).
3. Si la marca una cámara de la sala: `tiempos_pipeline {"entrada"}` muestra si es la captura (probá otro modo: `video_devices {"device"}`) o una tarjeta.
4. Con lo nuevo andando y nada al aire, baja sola; si no, `estudio_latencia {"bajar": true}` (con algo al aire hay un salto: avisale al operador).
5. Mirá `cuadros_tarde_60s` en estudio_latencia: tiene que dar 0. Cada entrada pide su edad p90 + lo que tiembla + 5 ms, y el compositor de video suma solo 5 ms (`video.margenComposicionMs`); si aparecen cuadros tarde, esa entrada tiembla más de lo que pidió.
6. Para comprobar que dos cámaras quedaron alineadas: un reloj (el de un teléfono, con milisegundos) frente a las dos y `video_sincronia {"a", "b"}`: el desfase tiene que dar menos de un cuadro.

## Contale al operador
Con números: «la latencia del estudio es 95 ms y la marca la Cámara 2 (captura 75 ms); el invitado va con su propia demora, 40 ms más».