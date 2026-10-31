# Transmissão de jogos (streamer)

## Para quê
Um streamer joga ao vivo: a captura do jogo, a câmera dele num canto, o chat do pessoal na
tela e alertas. O áudio do jogo e o microfone equilibrados.

## Perguntas próprias
- Qual plataforma (Twitch, YouTube, Facebook)? A conta está conectada?
- Qual jogo e como é capturado (janela, tela, console por placa de captura)?
- Câmera do streamer? Com fundo recortado (chroma key)?
- Chat na tela? Destacar mensagens? Alertas de seguidores?
- Música de fundo (cuidado com os direitos autorais)?

## O que montar
- Cenas: “Jogo” (captura + câmera pequena + chat), “Papo” (câmera grande), “Pausa”, “Início”.
- Chat na tela (chat_overlay_add) com o da plataforma (twitch_chat / youtube_chat).
- Áudio: jogo e microfone com seu nível; o microfone com compressor e noise gate.
- Transmissão (twitch_live_start / youtube_live_create) com título e categoria.

## Verificar
Teste de transmissão; que o chat apareça; níveis (o jogo não encobre a voz).

## Entregar
O link do canal e o cartão de chat para destacar mensagens.
