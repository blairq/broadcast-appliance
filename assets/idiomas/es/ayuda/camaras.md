Cada entrada de video tiene PTZ: DIGITAL (recorte y zoom en la GPU, en cualquier camara) y, si la camara es motorizada, el de la camara (backend VISCA, ONVIF o Panasonic). `ptz_camaras` dice que tiene y que puede cada una.

## Mover
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}`: velocidades -1..1 (pan + derecha, tilt + arriba, zoom + acercar); se para sola al terminar. Mueve lo que diga su `modo` (optico = la cabeza, digital = el recorte).
- Encuadre digital exacto: `ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}` (x, y = centro, 0..1; zoom 1..8). Mira el resultado con `foto_fuente`.
- Presets: `ptz_preset {"entrada": ..., "numero": 2}` (ir) o con `"accion": "guardar"`.

## Posiciones con nombre
1. Encuadrar (ptz_mover / ptz_encuadre) y `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}`: guarda el encuadre digital, la memoria de la camara y la definicion.
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}` (id o nombre) la lleva ahi.
3. `ptz_posiciones` las lista (con `miniatura_ruta`: la foto); `ptz_posicion_foto` la muestra; `ptz_posicion_editar` (nombre, comentario, `velocidad` 0..1, `segundos`, `regrabar`, `miniatura`) y `ptz_posicion_borrar`.
4. El recinto (la grilla de bancas): `recinto_ver`, `recinto_editar {filas, columnas, auto | mover | bancas}` y `recinto_apuntar {banca}` (la cámara a la toma de la banca, sin sacarla al aire); `enfocar {que}` hace lo mismo de palabra (banca, persona o posición).

## Configurar una camara motorizada
1. ONVIF: `ptz_buscar` encuentra las de la red; `ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`.
2. VISCA (Sony, PTZOptics, BirdDog...): `"backend": "visca", "host": ip, "transporte": "sony"` (UDP 52381) o `"tcp"` (5678) o `"udp"` (1259).
3. Panasonic AW/UE: `"backend": "panasonic", "host": ip`.
4. `ptz_camaras` dice si contesta (`listo`, `estado`). Con camara y digital, `"modo": "digital"` hace que el joystick mueva el recorte.

## Mejorar imagen
`video_mejora {"activa": true}` (lo general: ruido, color pegado al brillo, nitidez; 0-100) y por camara `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`.