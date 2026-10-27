## Hardware (conectadas a la maquina)
1. `video_devices` -> elegir `device`; `video_devices {"device": ...}` muestra formatos/resoluciones/fps.
2. `video_input_add {"device": ..., "scene": ..., "layout": "full", "resolution": ..., "framerate": ...}`.
3. Verificar con `foto_fuente`. Una misma camara no se abre dos veces: si ya es fuente en otra escena, agregala con `crear_fuente` usando su nombre (queda compartida).

## NDI
- `ndi_sources` busca emisores NDI en la red (mDNS) y `ndi_input_add {"ndiName": ...}` (o `host` + `port`) los agrega.
- Recepcion ABIERTA con libNoIDea (VideoLAN, experimental): NDI completo (SpeedHQ, con audio); NDI|HX (H.264/H.265) no. Si no llega imagen, `input_status` dice por que (no se encuentra, no conecta, se corto) y reintenta sola.

## Red
- Camara IP: `network_input_add {"url": "rtsp://usuario:clave@192.168.1.50:554/stream1"}` (RTSP por TCP; `rtspTransport: "udp"` si la camara lo pide).
- Otras: `rtmp://`, `srt://ip:puerto?mode=caller`, `https://.../playlist.m3u8` (HLS), `udp://@:5000`.
- Devuelve si llega imagen y de que tamaño. `input_status` muestra el estado de todas (sin claves).
- Latencia baja por defecto; `latency: "normal"` para señales inestables (mas colchon). Reconecta sola.