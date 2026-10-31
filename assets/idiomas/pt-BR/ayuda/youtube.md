## Configurar (uma vez)
1. `youtube_status`: diz se falta a credencial ou a conta.
2. Credencial: a pessoa cria um cliente OAuth do tipo **App para computador** no Google Cloud (a aba YouTube do app tem os passos com links: projeto, ativar a YouTube Data API v3, tela de consentimento, usuário de teste, cliente) e ela é salva com `youtube_setup {"fromDownloads": true}` (importa o JSON baixado) ou com o formulário seguro `account_credentials_request {"platform": "youtube"}` (ver `help {"topic": "cuentas"}`).
3. `youtube_login`: abre o navegador desta máquina; a PESSOA escolhe a conta e aceita. `youtube_status` confirma o canal.

## Transmitir ao vivo
- Tudo em um: `youtube_live_create {"title": "...", "privacy": "private", "start": true}` cria o evento, pega servidor e chave (só na memória, nunca em disco) e começa a transmitir. Com início automático, o evento passa a AO VIVO quando o YouTube recebe o sinal (~10-30 s).
- Para um evento existente: `youtube_live_list` -> `youtube_live_start {"id"}`.
- Verificar: `status` (streaming, fps, quadros perdidos) e `youtube_live_health {"id"}` (active / good).
- Terminar: `youtube_live_end {"id"}` (fecha o evento e corta o app). Editar/mudar estado/apagar: `youtube_live_update`.
- Canais novos: o YouTube demora até 24 h para habilitar a live na primeira vez (erro "liveStreamingNotEnabled").

## Vídeos
`youtube_videos`, `youtube_video_update` (título, descrição, privacidade, tags, miniatura), `youtube_video_delete`, `youtube_video_upload` (sem `path`: a última gravação do app).

## Chat
- `youtube_chat {"broadcast": id}` escolhe o evento; `{"send": "texto"}` escreve; devolve as últimas mensagens.
- Na tela: `chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` adiciona a fonte **Chat en vivo** (tipo `ba_chat_overlay`) a uma cena; com `all` mistura todas as plataformas conectadas (cada mensagem com sua marca). Ajusta-se com `source_update` (settings: show_youtube, show_twitch, show_facebook, show_room, max_messages, font_size, width, height, bg_opacity, show_names, fade_sec). Verificar com `foto_fuente`.

## Cota
10.000 unidades por dia e projeto: listar e editar custa pouco; enviar um vídeo ~1.600; ler o chat ~5 por leitura (o app lê a cada 6 s ou mais, só com um evento escolhido).