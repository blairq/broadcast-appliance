## Que es
Un estudio de transmision completo en una sola app:
- **Programa**: lo que sale al aire. Es la **escena al aire** (`scene_switch` la cambia, con transicion).
- **Escenas**: composiciones de **fuentes** (camaras, imagenes, textos, videos, invitados, colores...). Una fuente puede estar en varias escenas (es la misma).
- **Salidas**: transmision RTMP (`stream_start`) y grabacion (`record_start`).
- **Conferencia**: los invitados entran desde el navegador (WebRTC). Primero esperan en un **lobby** privado (solo te ven a vos); al admitirlos ocupan un **lugar** (slot 1..N) y su camara es la fuente `Invitado N`. Con el **modo oradores**, en vez de entrar directo hacen fila y salen al aire por turno con un reloj.

## Primer vistazo (recomendado)
1. `status` — que esta al aire, si se transmite o graba, salud.
2. `foto_programa` — ver el programa.
3. `scene_list` / `foto_todas_escenas` — todas las escenas.
4. `room_status` — quien esta en la conferencia.

## Convenciones
- Ubicaciones en **fracciones del lienzo** (0..1), o presets (`full`, `pip-br`, `grid-4-2`...). Ver tema `fuentes`.
- Volumen en **dB** (0 = sin cambio). Tiempos de oradores en segundos o `m:ss`.
- Las herramientas guardan solas la configuracion.
- Los errores explican que paso y como seguir: leelos y corregi.