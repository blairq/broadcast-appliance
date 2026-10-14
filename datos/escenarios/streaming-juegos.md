# Transmisión de juegos (streamer)

## Para qué
Un streamer juega en vivo: la captura del juego, su cámara en una esquina, el chat de la gente en
pantalla y alertas. El audio del juego y el micrófono balanceados.

## Preguntas propias
- ¿Qué plataforma (Twitch, YouTube, Facebook)? ¿Está la cuenta conectada?
- ¿Qué juego y cómo se captura (ventana, pantalla, consola por capturadora)?
- ¿Cámara del streamer? ¿Con fondo recortado (croma)?
- ¿Chat en pantalla? ¿Destacar mensajes? ¿Alertas de seguidores?
- ¿Música de fondo (cuidado con los derechos)?

## Qué armar
- Escenas: «Juego» (captura + cámara chica + chat), «Charla» (cámara grande), «Pausa», «Inicio».
- Chat en pantalla (chat_overlay_add) con el de la plataforma (twitch_chat / youtube_chat).
- Audio: juego y micrófono con su nivel; el micrófono con compresor y puerta de ruido.
- Transmisión (twitch_live_start / youtube_live_create) con título y categoría.

## Verificar
Prueba de transmisión; que el chat aparezca; niveles (el juego no tapa la voz).

## Entregar
El enlace del canal y la tarjeta de chat para destacar mensajes.
