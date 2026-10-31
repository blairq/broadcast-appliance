## Alcance
A API do Facebook só transmite e administra vídeos de **Páginas** (não do perfil pessoal). A conta tem que administrar pelo menos uma Página.

## Configurar (uma vez)
1. `facebook_status`: diz se falta o app, a conta ou a Página, e a URI de redirecionamento.
2. App da Meta: em https://developers.facebook.com/apps/ crie um app (tipo **Empresa**), adicione **Login do Facebook** e coloque em "URIs de redirecionamento do OAuth válidos" a `redirectUri` de `facebook_status` (por padrão `http://localhost:8791/`). O modo de desenvolvimento basta (só os administradores dele o usam). ID e chave secreta em Configurações -> Básico: são salvos com o formulário seguro `account_credentials_request {"platform": "facebook"}` (a chave não deve passar pelo chat).
3. `facebook_login`: abre o navegador; a PESSOA escolhe a conta, marca as Páginas e aceita. Depois `facebook_pages` (e `{"select": "..."}` se houver várias).

## Transmitir
- `facebook_live_create {"title": "...", "test": true}` cria a live **sem publicar** (só a Página a vê no Live Producer) e transmite do app (RTMPS, servidor e chave só na memória). Revise com `foto_programa` e `facebook_live_list {"id"}`; quando estiver bom, `facebook_live_publish {"id"}` a torna pública sem cortar.
- `test: false`: fica pública na hora.
- Terminar: `facebook_live_end` (sem `id`: a live para a qual o app transmite). Fica como vídeo.

## Vídeos
`facebook_videos`, `facebook_video_update {"id", "title", "description"}` (ou `"delete": true`), `facebook_video_upload` (sem `path`: a última gravação).

## Comentários
- Ao transmitir, os da live são escolhidos sozinhos; `facebook_chat {"live": id}` escolhe outra, `{"send": "texto"}` comenta como a Página.
- Na tela: `chat_overlay_add {"channel": "facebook"}` (ou `all`).