## Configurar (una vez)
1. `twitch_status`: dice si falta la app o la cuenta.
2. App: la persona activa la verificacion en dos pasos de Twitch y registra una app en https://dev.twitch.tv/console/apps/create (redireccion `http://localhost`, categoria **Broadcaster Suite**, tipo de cliente **Publico**). Se guarda con el formulario seguro `account_credentials_request {"platform": "twitch"}` (o `twitch_setup {"clientId": "..."}`: el ID de un cliente Publico no es secreto).
3. `twitch_login`: devuelve un **codigo** y abre twitch.tv/activate; la PERSONA inicia sesion con la cuenta del canal y lo confirma (mostrale el codigo). `twitch_status` confirma la cuenta.

## Transmitir
- `twitch_live_start {"title": "...", "game": "Just Chatting"}`: aplica titulo/categoria, toma la clave del canal (solo en memoria) y transmite desde la app a `rtmp://live.twitch.tv/app`.
- Verificar: `status` (streaming, fps, cuadros perdidos) y `twitch_stream_status` (live, espectadores) en ~15 s.
- Cortar: `twitch_live_stop`. En Twitch no hay "eventos": el canal esta en vivo mientras recibe señal.

## Canal y VODs
- `twitch_channel` lee; `twitch_channel {"title", "game", "tags", "language"}` cambia; `{"search": "..."}` busca categorias.
- `twitch_videos`, `twitch_video_delete {"ids": [...]}`. Los vivos se guardan como VOD solo si el canal tiene activado "Almacenar transmisiones anteriores".

## Chat
- `twitch_chat {"on": true}` se conecta al chat del canal (IRC seguro); `{"send": "texto"}` escribe como el canal; devuelve los ultimos mensajes con color e insignias.
- En pantalla: `chat_overlay_add {"channel": "twitch"}` (o `all` para mezclar plataformas).