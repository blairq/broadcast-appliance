Every video input has PTZ: DIGITAL (crop and zoom on the GPU, on any camera) and, if the camera is motorized, the camera's own (VISCA, ONVIF or Panasonic backend). `ptz_camaras` says what each one has and can do.

## Move
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}`: speeds -1..1 (pan + right, tilt + up, zoom + in); it stops on its own when done. It moves whatever its `modo` says (optical = the head, digital = the crop).
- Exact digital framing: `ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}` (x, y = center, 0..1; zoom 1..8). Check the result with `foto_fuente`.
- Presets: `ptz_preset {"entrada": ..., "numero": 2}` (go) or with `"accion": "guardar"`.

## Named positions
1. Frame it (ptz_mover / ptz_encuadre) and `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}`: saves the digital framing, the camera's memory and the definition.
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}` (id or name) takes it there.
3. `ptz_posiciones` lists them (with `miniatura_ruta`: the photo); `ptz_posicion_foto` shows it; `ptz_posicion_editar` (name, comment, `velocidad` 0..1, `segundos`, `regrabar`, `miniatura`) and `ptz_posicion_borrar`.
4. The chamber (the grid of seats): `recinto_ver`, `recinto_editar {filas, columnas, auto | mover | bancas}` and `recinto_apuntar {banca}` (the camera to the seat's shot, without putting it on air); `enfocar {que}` does the same by words (seat, person or position).

## Set up a motorized camera
1. ONVIF: `ptz_buscar` finds the ones on the network; `ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`.
2. VISCA (Sony, PTZOptics, BirdDog...): `"backend": "visca", "host": ip, "transporte": "sony"` (UDP 52381) or `"tcp"` (5678) or `"udp"` (1259).
3. Panasonic AW/UE: `"backend": "panasonic", "host": ip`.
4. `ptz_camaras` says whether it responds (`listo`, `estado`). With both camera and digital, `"modo": "digital"` makes the joystick move the crop.

## Improve the picture
`video_mejora {"activa": true}` (the general one: noise, color tied to brightness, sharpness; 0-100) and per camera `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`.