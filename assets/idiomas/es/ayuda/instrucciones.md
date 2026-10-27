Broadcast Appliance: estudio de transmision en vivo (escenas y fuentes sobre un motor propio de GStreamer, salida RTMP/SRT y grabacion) con videoconferencia propia, la Conferencia (invitados por WebRTC con lobby, modo oradores con reloj, tablero para tablet). Este servidor maneja TODO y te deja VER todo.

Como trabajar:
1. Orientate con `status` y mira el programa con `foto_programa` (o `foto_todas_escenas` para todas las escenas).
2. Si no sabes como hacer algo: `help` (guia y catalogo), `help {"topic": ...}` o `help_search {"query": ...}`.
3. Despues de cada cambio visual, VERIFICA mirando: si la captura (foto_fuente / foto_programa) no te devuelve la imagen, `imagen_preguntar` sobre la escena o fuente con una pregunta concreta. No supongas. Para comparar brillo o color fino usa numeros, no la vista.
4. Las acciones ocurren en vivo: si hay transmision (`status.streaming`), lo que cambies en la escena al aire lo ve el publico. Para preparar algo sin que se vea, armalo en otra escena y revisala (imagen_preguntar) antes de scene_switch.
5. Las personas se nombran por nombre o id (room_status las lista). Para verlas: foto_invitado / foto_todos_invitados.