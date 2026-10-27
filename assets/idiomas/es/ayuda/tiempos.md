## De afuera hacia adentro
1. `tiempos_mapa`: de cada entrada a cada salida, cuánto agrega cada nodo (entrada, tarjeta, módulo de audio, escena, mezcla, salida) y lo acumulado. `salidas[].camino` es el camino más lento: por ahí se empieza.
2. `tiempos_pipeline {"entrada"}`: la entrada sospechosa por dentro, elemento por elemento (unos segundos de sondas; no toca el aire). Leé primero `hallazgos`: ya vienen razonados, por gravedad, con la tarjeta culpable.
3. `tiempos_plugin {"entrada", "tarjeta"}`: la tarjeta culpable, cómo trabaja (sus memorias, propiedades, documentación) y lo que hace en esta cadena. `tiempos_plugin {"elemento"}` de las alternativas para comparar (`interior.gpu`).
4. Si cambiaste algo, `tiempos_pipeline` otra vez y compará. Para ver en el tiempo: `tiempos_evento_agregar` (una nota) y `tiempos_evento_impacto`.

## Cómo leer lo medido
- `agrega_ms` = edad del cuadro a la salida del elemento menos a la entrada. Cuenta lo que TARDA y lo que RETIENE (un filtro temporal que guarda cuadros agrega aunque procese rápido). El `ms_por_cuadro` que informa un plugin es solo lo que tarda.
- `memoria_*` es lo que negoció; `puede_*` lo que sabe usar. Un cruce (`baja` de la GPU a RAM, `sube` de RAM a la GPU) copia el cuadro entero: en 1080p son ~8 MB por cuadro, más CPU y ancho de memoria. Una **ida y vuelta** (baja y vuelve a subir) casi siempre es un plugin que solo trabaja en RAM entre dos que trabajan en la GPU.
- Si un plugin solo acepta «sistema»: no tiene versión GPU; buscá otro (video_modulos, las variantes gl/d3d12) o ponelo al principio o al final de la cadena, donde el video ya está en RAM, para no partirla.
- Si `puede` GPU pero negoció RAM: lo que tiene al lado lo obligó (otro plugin solo-RAM pegado, o un formato que la GPU no maneja).
- `declara_ms` es lo que el elemento le dice al pipeline (con eso compensa el estudio). `declara_menos`: tarda más de lo que declara; si la diferencia se acerca al colchón de la entrada, los cuadros llegan tarde a la mezcla → subir el colchón (`latencia_ms` de la entrada) o cambiar el plugin.
- `conversion_cpu`: videoconvert/videoscale en RAM. Muchos ms en 1080p = conviene que lo anterior entregue el formato que pide lo que sigue.
- Una cola con `nivel_cola_ms` alto está guardando video: algo de después va lento o espera.

## Resolución: que cada entrada entregue lo que se ve
`resolucion_plan {"entrada"}` cruza dónde se ve la entrada (su tamaño más grande en las escenas) con cada tarjeta y la cámara: la superres que agranda algo que ya cubre lo que se ve, el PTZ después de la superres, el zoom digital que estira pocos píxeles, la cámara que entrega de más o de menos (con el modo que conviene), los pocos cps. Los mismos hallazgos pintan las tarjetas (amarillo / naranja / rojo) en Video y en Latencias. Los modos reales de cada cámara (crudo, MJPEG, H.264 y quién decodifica): `video_devices {"device"}`; todo lo que anuncia: `dispositivos_video`.

## Contale al operador
En castellano y con la consecuencia: «la disolución por movimiento retiene 800 ms (24 cuadros) y trabaja en RAM: obliga a bajar y subir el video de la GPU». Proponé el cambio y esperá el OK antes de tocar una tarjeta al aire.