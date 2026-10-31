## Configurar (uma vez)
1. `twitch_status`: diz se falta o app ou a conta.
2. App: a pessoa ativa a verificação em duas etapas da Twitch e registra um app em https://dev.twitch.tv/console/apps/create (redirecionamento `http://localhost`, categoria **Broadcaster Suite**, tipo de cliente **Público**). É salvo com o formulário seguro `account_credentials_request {"platform": "twitch"}` (ou `twitch_setup {"clientId": "..."}`: o ID de um cliente Público não é secreto).
3. `twitch_login`: devolve um **código** e abre twitch.tv/activate; a PESSOA faz login com a conta do canal e confirma (mostre o código a ela). `twitch_status` confirma a conta.

## Transmitir
- `twitch_live_start {"title": "...", "game": "Just Chatting"}`: aplica título/categoria, pega a chave do canal (só na memória) e transmite do app para `rtmp://live.twitch.tv/app`.
- Verificar: `status` (streaming, fps, quadros perdidos) e `twitch_stream_status` (live, espectadores) em ~15 s.
- Cortar: `twitch_live_stop`. Na Twitch não há "eventos": o canal está ao vivo enquanto recebe sinal.

## Canal e VODs
- `twitch_channel` lê; `twitch_channel {"title", "game", "tags", "language"}` muda; `{"search": "..."}` procura categorias.
- `twitch_videos`, `twitch_video_delete {"ids": [...]}`. As lives são guardadas como VOD só se o canal tiver ativado "Armazenar transmissões anteriores".

## Chat
- `twitch_chat {"on": true}` se conecta ao chat do canal (IRC seguro); `{"send": "texto"}` escreve como o canal; devolve as últimas mensagens com cor e insígnias.
- Na tela: `chat_overlay_add {"channel": "twitch"}` (ou `all` para misturar plataformas).