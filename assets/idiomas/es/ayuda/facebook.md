## Alcance
La API de Facebook solo transmite y administra videos de **Paginas** (no del perfil personal). La cuenta tiene que administrar al menos una Pagina.

## Configurar (una vez)
1. `facebook_status`: dice si falta la app, la cuenta o la Pagina, y la URI de redireccion.
2. App de Meta: en https://developers.facebook.com/apps/ crear una app (tipo **Empresa**), agregar **Inicio de sesion con Facebook** y cargar en "URI de redireccionamiento de OAuth validos" la `redirectUri` de `facebook_status` (por defecto `http://localhost:8791/`). En modo desarrollo alcanza (solo la usan sus administradores). ID y clave secreta en Configuracion -> Basica: se guardan con el formulario seguro `account_credentials_request {"platform": "facebook"}` (la clave no debe pasar por el chat).
3. `facebook_login`: abre el navegador; la PERSONA elige la cuenta, marca las Paginas y acepta. Despues `facebook_pages` (y `{"select": "..."}` si hay varias).

## Transmitir
- `facebook_live_create {"title": "...", "test": true}` crea el vivo **sin publicar** (solo lo ve la Pagina en Live Producer) y transmite desde la app (RTMPS, servidor y clave solo en memoria). Revisar con `foto_programa` y `facebook_live_list {"id"}`; cuando este bien, `facebook_live_publish {"id"}` lo hace publico sin cortar.
- `test: false`: sale publico en el acto.
- Terminar: `facebook_live_end` (sin `id`: el vivo al que transmite la app). Queda como video.

## Videos
`facebook_videos`, `facebook_video_update {"id", "title", "description"}` (o `"delete": true`), `facebook_video_upload` (sin `path`: la ultima grabacion).

## Comentarios
- Al transmitir se eligen solos los del vivo; `facebook_chat {"live": id}` elige otro, `{"send": "texto"}` comenta como la Pagina.
- En pantalla: `chat_overlay_add {"channel": "facebook"}` (o `all`).