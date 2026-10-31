Chaque entrée vidéo a un PTZ : NUMÉRIQUE (recadrage et zoom sur le GPU, sur n'importe quelle caméra) et, si la caméra est motorisée, celui de la caméra (backend VISCA, ONVIF ou Panasonic). `ptz_camaras` dit ce qu'a chacune et ce qu'elle peut faire.

## Bouger
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}` : vitesses -1..1 (pan + droite, tilt + haut, zoom + rapprocher) ; s'arrête toute seule à la fin. Fait bouger ce qu'indique son `modo` (optico = la tête, digital = le recadrage).
- Cadrage numérique exact : `ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}` (x, y = centre, 0..1 ; zoom 1..8). Regardez le résultat avec `foto_fuente`.
- Presets : `ptz_preset {"entrada": ..., "numero": 2}` (y aller) ou avec `"accion": "guardar"`.

## Positions nommées
1. Cadrer (ptz_mover / ptz_encuadre) et `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}` : enregistre le cadrage numérique, la mémoire de la caméra et la définition.
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}` (id ou nom) l'y amène.
3. `ptz_posiciones` les liste (avec `miniatura_ruta` : la photo) ; `ptz_posicion_foto` l'affiche ; `ptz_posicion_editar` (nom, commentaire, `velocidad` 0..1, `segundos`, `regrabar`, `miniatura`) et `ptz_posicion_borrar`.
4. L'hémicycle (la grille des sièges) : `recinto_ver`, `recinto_editar {filas, columnas, auto | mover | bancas}` et `recinto_apuntar {banca}` (la caméra sur le plan du siège, sans le passer à l'antenne) ; `enfocar {que}` fait la même chose à partir d'un mot (siège, personne ou position).

## Configurer une caméra motorisée
1. ONVIF : `ptz_buscar` trouve celles du réseau ; `ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`.
2. VISCA (Sony, PTZOptics, BirdDog...) : `"backend": "visca", "host": ip, "transporte": "sony"` (UDP 52381) ou `"tcp"` (5678) ou `"udp"` (1259).
3. Panasonic AW/UE : `"backend": "panasonic", "host": ip`.
4. `ptz_camaras` dit si elle répond (`listo`, `estado`). Avec caméra et numérique, `"modo": "digital"` fait que le joystick déplace le recadrage.

## Améliorer l'image
`video_mejora {"activa": true}` (le réglage général : bruit, couleur liée à la luminosité, netteté ; 0-100) et par caméra `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`.
