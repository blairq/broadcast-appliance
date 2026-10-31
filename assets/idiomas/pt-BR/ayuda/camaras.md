Cada entrada de vídeo tem PTZ: DIGITAL (recorte e zoom na GPU, em qualquer câmera) e, se a câmera for motorizada, o da câmera (backend VISCA, ONVIF ou Panasonic). `ptz_camaras` diz o que cada uma tem e pode.

## Mover
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}`: velocidades -1..1 (pan + direita, tilt + cima, zoom + aproximar); para sozinha ao terminar. Move o que o `modo` dela disser (óptico = a cabeça, digital = o recorte).
- Enquadramento digital exato: `ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}` (x, y = centro, 0..1; zoom 1..8). Veja o resultado com `foto_fuente`.
- Presets: `ptz_preset {"entrada": ..., "numero": 2}` (ir) ou com `"accion": "guardar"`.

## Posições com nome
1. Enquadrar (ptz_mover / ptz_encuadre) e `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}`: salva o enquadramento digital, a memória da câmera e a definição.
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}` (id ou nome) a leva até lá.
3. `ptz_posiciones` as lista (com `miniatura_ruta`: a foto); `ptz_posicion_foto` a mostra; `ptz_posicion_editar` (nome, comentário, `velocidad` 0..1, `segundos`, `regrabar`, `miniatura`) e `ptz_posicion_borrar`.
4. O plenário (a grade de cadeiras): `recinto_ver`, `recinto_editar {filas, columnas, auto | mover | bancas}` e `recinto_apuntar {banca}` (a câmera na tomada da cadeira, sem colocá-la no ar); `enfocar {que}` faz o mesmo por voz (cadeira, pessoa ou posição).

## Configurar uma câmera motorizada
1. ONVIF: `ptz_buscar` encontra as da rede; `ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`.
2. VISCA (Sony, PTZOptics, BirdDog...): `"backend": "visca", "host": ip, "transporte": "sony"` (UDP 52381) ou `"tcp"` (5678) ou `"udp"` (1259).
3. Panasonic AW/UE: `"backend": "panasonic", "host": ip`.
4. `ptz_camaras` diz se responde (`listo`, `estado`). Com câmera e digital, `"modo": "digital"` faz o joystick mover o recorte.

## Melhorar a imagem
`video_mejora {"activa": true}` (o geral: ruído, cor atrelada ao brilho, nitidez; 0-100) e por câmera `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`.