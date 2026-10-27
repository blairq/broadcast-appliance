## Configurar (una vez)
1. `youtube_status`: dice si falta la credencial o la cuenta.
2. Credencial: la persona crea un cliente OAuth tipo **App de escritorio** en Google Cloud (la pestaña YouTube de la app tiene los pasos con enlaces: proyecto, habilitar YouTube Data API v3, pantalla de consentimiento, usuario de prueba, cliente) y se guarda con `youtube_setup {"fromDownloads": true}` (importa el JSON bajado) o con el formulario seguro `account_credentials_request {"platform": "youtube"}` (ver `help {"topic": "cuentas"}`).
3. `youtube_login`: abre el navegador de esta maquina; la PERSONA elige la cuenta y acepta. `youtube_status` confirma el canal.

## Transmitir en vivo
- Todo en uno: `youtube_live_create {"title": "...", "privacy": "private", "start": true}` crea el evento, toma servidor y clave (solo en memoria, nunca a disco) y empieza a transmitir. Con inicio automatico, el evento pasa a EN VIVO cuando YouTube recibe la señal (~10-30 s).
- A un evento existente: `youtube_live_list` -> `youtube_live_start {"id"}`.
- Verificar: `status` (streaming, fps, cuadros perdidos) y `youtube_live_health {"id"}` (active / good).
- Terminar: `youtube_live_end {"id"}` (cierra el evento y corta la app). Editar/cambiar estado/borrar: `youtube_live_update`.
- Canales nuevos: YouTube tarda hasta 24 h en habilitar el vivo la primera vez (error "liveStreamingNotEnabled").

## Videos
`youtube_videos`, `youtube_video_update` (titulo, descripcion, privacidad, etiquetas, miniatura), `youtube_video_delete`, `youtube_video_upload` (sin `path`: la ultima grabacion de la app).

## Chat
- `youtube_chat {"broadcast": id}` elige el evento; `{"send": "texto"}` escribe; devuelve los ultimos mensajes.
- En pantalla: `chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` agrega la fuente **Chat en vivo** (tipo `ba_chat_overlay`) a una escena; con `all` mezcla todas las plataformas conectadas (cada mensaje con su marca). Se ajusta con `source_update` (settings: show_youtube, show_twitch, show_facebook, show_room, max_messages, font_size, width, height, bg_opacity, show_names, fade_sec). Verificar con `foto_fuente`.

## Cuota
10.000 unidades por dia y proyecto: listar y editar cuesta poco; subir un video ~1.600; leer el chat ~5 por lectura (la app lee cada 6 s o mas, solo con un evento elegido).