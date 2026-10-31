## Tipos
`source_types` lista os desta instalação. Os mais usados:
| type | settings típicos |
|---|---|
| `image_source` | `file` (caminho) |
| `ffmpeg_source` | `local_file`, `looping`, `restart_on_activate` |
| `color_source_v3` | `color` (inteiro ABGR: 0xAABBGGRR, p. ex. vermelho opaco = 4278190335), `width`, `height` |
| `text_ft2_source_v2` | `text`, `font` {face, size}, `color1`, `color2`, `outline`, `drop_shadow` |
| `v4l2_input` (Linux) | câmera: `device_id`, resolução (ver source_properties) |
| `macos-avcapture` (macOS) | câmera: `device` (ver source_properties) |
| `coreaudio_output_capture` (macOS) | áudio do sistema: `device_id` |
| microfones (todos os sistemas) | entram pelo GStreamer: `audio_input_add` (não `crear_fuente`); `ba_gst_fuente` com `pipeline_audio` e `microfono`. Filtros: `entrada_filtros_audio`; mesa-redonda: `automixer` |
| `screen_capture` | uma tela: `monitor` (seu nome: as opções saem em source_properties; vazio = a principal), `show_cursor` |
| `ba_qr` | um código QR na cena: `qr` = \"aparato:publico\" (a página do público, com sua senha), \"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\" ou o id de um salvo (qr_listar); vazio = `texto`. `fondo` (margem branca), `tamano`. Mantém-se atualizado sozinho |
| `window_capture` | a janela de um aplicativo, mesmo que esteja coberta: `ventana` = "aplicacion\ntitulo" (as abertas saem em source_properties), `show_cursor`. O aplicativo é lembrado: se fechar e abrir, continua sozinho. No Mac pede permissão de gravação de tela |
| `dshow_input` (Windows) | câmera |
| `ba_guest_video` | convidado: `slot` (1..N), `channel` guest (câmera) ou share (tela compartilhada) |
| `ba_grilla` | grade de pessoas: um mosaico que cresce sozinho com os admitidos da Conferência e os membros habilitados da Assembleia. `origen` (sala/asamblea/ambas), `orden` (llegada/habla/prioridad), `tope` (com o “+N”), `grupo` (várias grades repartem as pessoas sem repetir; `reparto` en_orden/parejo). Quem já aparece em outra grade no ar não se repete. Ver grilla_personas_estado e grilla_personas_ajustar; manualmente: grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`, `slide_time` |
| `ba_chat_overlay` | chat ao vivo (YouTube, Twitch, Facebook e/ou conferência) sobre o vídeo: `show_youtube`, `show_twitch`, `show_facebook`, `show_room` (ou `channel` all/youtube/twitch/facebook/room), `max_messages`, `font_size`, `width`, `height`, `bg_opacity`, `show_names`, `fade_sec` |

Para TODAS as opções de uma fonte: `source_properties {"name"}`; para mudar uma: `source_set_property`. `source_update {"settings": {...}}` mescla chaves (não apaga as demais).

## Posição (`layout`)
Frações do canvas: `{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` ou presets:
`full`, `left`, `right`, `top`, `bottom`, `center`, `top-left`, `top-right`, `bottom-left`, `bottom-right`, `pip-tl`, `pip-tr`, `pip-bl`, `pip-br` (janelinha nesse canto), `lower-third` (GC), `third-1..3`, `grid-<n>-<i>` (célula i de uma grade para n: `grid-4-1` .. `grid-4-4`).
A fonte se ajusta dentro do retângulo conservando sua proporção.

## Ordem e visibilidade
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`, `{"visible": false}` (oculta sem apagar: ideal para preparar e mostrar no momento certo).

## Convidados
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` coloca o convidado do lugar 1 (quem quer que esteja) na cena.