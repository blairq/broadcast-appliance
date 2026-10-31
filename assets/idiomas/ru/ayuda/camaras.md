У каждого видеовхода есть PTZ: ЦИФРОВОЙ (кадрирование и зум на GPU, на любой камере) и, если камера моторизованная, собственный PTZ камеры (протокол VISCA, ONVIF или Panasonic). `ptz_camaras` говорит, что есть у каждой и что она умеет.

## Двигать
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}`: скорости -1..1 (pan + вправо, tilt + вверх, zoom + приблизить); останавливается сама по окончании. Двигает то, что указано в её `modo` (оптический = головка, цифровой = кадрирование).
- Точное цифровое кадрирование: `ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}` (x, y = центр, 0..1; zoom 1..8). Посмотрите результат через `foto_fuente`.
- Пресеты: `ptz_preset {"entrada": ..., "numero": 2}` (перейти) или с `"accion": "guardar"`.

## Именованные позиции
1. Выставить кадр (ptz_mover / ptz_encuadre) и `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}`: сохраняет цифровое кадрирование, память камеры и определение.
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}` (id или имя) переводит её туда.
3. `ptz_posiciones` перечисляет их (с `miniatura_ruta`: фото); `ptz_posicion_foto` показывает её; `ptz_posicion_editar` (имя, комментарий, `velocidad` 0..1, `segundos`, `regrabar`, `miniatura`) и `ptz_posicion_borrar`.
4. Зал заседаний (сетка мест): `recinto_ver`, `recinto_editar {filas, columnas, auto | mover | bancas}` и `recinto_apuntar {banca}` (камера на план места, не выводя его в эфир); `enfocar {que}` делает то же самое словами (место, человек или позиция).

## Настроить моторизованную камеру
1. ONVIF: `ptz_buscar` находит камеры в сети; `ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`.
2. VISCA (Sony, PTZOptics, BirdDog...): `"backend": "visca", "host": ip, "transporte": "sony"` (UDP 52381) или `"tcp"` (5678) или `"udp"` (1259).
3. Panasonic AW/UE: `"backend": "panasonic", "host": ip`.
4. `ptz_camaras` говорит, отвечает ли она (`listo`, `estado`). При наличии и камеры, и цифрового PTZ `"modo": "digital"` заставляет джойстик двигать кадрирование.

## Улучшить изображение
`video_mejora {"activa": true}` (общее: шум, цвет с учётом яркости, резкость; 0-100) и для каждой камеры `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`.
