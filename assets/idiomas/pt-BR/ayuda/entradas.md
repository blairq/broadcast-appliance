## Hardware (conectadas à máquina)
1. `video_devices` -> escolher `device`; `video_devices {"device": ...}` mostra formatos/resoluções/fps.
2. `video_input_add {"device": ..., "scene": ..., "layout": "full", "resolution": ..., "framerate": ...}`.
3. Verificar com `foto_fuente`. Uma mesma câmera não é aberta duas vezes: se já é fonte em outra cena, adicione-a com `crear_fuente` usando o nome dela (fica compartilhada).

## NDI
- `ndi_sources` procura emissores NDI na rede (mDNS) e `ndi_input_add {"ndiName": ...}` (ou `host` + `port`) os adiciona.
- Recepção ABERTA com libNoIDea (VideoLAN, experimental): NDI completo (SpeedHQ, com áudio); NDI|HX (H.264/H.265) não. Se não chegar imagem, `input_status` diz por quê (não encontrado, não conecta, caiu) e tenta de novo sozinho.

## Rede
- Câmera IP: `network_input_add {"url": "rtsp://usuario:clave@192.168.1.50:554/stream1"}` (RTSP por TCP; `rtspTransport: "udp"` se a câmera pedir).
- Outras: `rtmp://`, `srt://ip:puerto?mode=caller`, `https://.../playlist.m3u8` (HLS), `udp://@:5000`.
- Devolve se chega imagem e de que tamanho. `input_status` mostra o estado de todas (sem senhas).
- Latência baixa por padrão; `latency: "normal"` para sinais instáveis (mais buffer). Reconecta sozinha.