## Hardware (connected to the machine)
1. `video_devices` -> choose `device`; `video_devices {"device": ...}` shows formats/resolutions/fps.
2. `video_input_add {"device": ..., "scene": ..., "layout": "full", "resolution": ..., "framerate": ...}`.
3. Check with `foto_fuente`. The same camera isn't opened twice: if it's already a source in another scene, add it with `crear_fuente` using its name (it ends up shared).

## NDI
- `ndi_sources` looks for NDI senders on the network (mDNS) and `ndi_input_add {"ndiName": ...}` (or `host` + `port`) adds them.
- OPEN reception with libNoIDea (VideoLAN, experimental): full NDI (SpeedHQ, with audio); NDI|HX (H.264/H.265) not supported. If no picture arrives, `input_status` says why (not found, can't connect, dropped) and it retries on its own.

## Network
- IP camera: `network_input_add {"url": "rtsp://usuario:clave@192.168.1.50:554/stream1"}` (RTSP over TCP; `rtspTransport: "udp"` if the camera asks for it).
- Others: `rtmp://`, `srt://ip:puerto?mode=caller`, `https://.../playlist.m3u8` (HLS), `udp://@:5000`.
- It returns whether a picture arrives and its size. `input_status` shows the status of all of them (without keys).
- Low latency by default; `latency: "normal"` for unstable signals (more buffer). It reconnects on its own.