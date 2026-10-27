For "what cameras are there?", "add the camera in the courtyard", "connect the DVR", "I have an Axis/Hikvision/EZVIZ…".

## The path (always this one)
1. `red_escanear` (about 10 s): each device with its IP, MAC and manufacturer, the brand (and why it recognized it), its ports, the typical RTSP URLs tested (`se ve` / `pide usuario y clave` / `no existe`), the brand's `notas` and `siguiente_paso`. **Follow the `siguiente_paso`.** Tell the operator what you found in a few lines (how many cameras, which brand, which already work and which ask for a password); don't dump the JSON on them.
<!-- “[OPERADOR]”: not translated (it is a fixed marker the program writes or reads). -->
2. If it asks for a password: `camara_credenciales {"ips": [...], "pedir": true}` (ONE form for all the ones that ask for it; if there's already one open, it adds them to it. It returns right away: when the operator fills it in you get “[OPERADOR] guardo…” in the chat; meanwhile, keep going with the rest). It is a secure form in the bar: **never ask for the password in the chat**. First tell them which user goes there (`usuario_de_fabrica`) and where the password comes from (the `notas`: EZVIZ = the verification code on the label, and RTSP has to be enabled in its app; Hikvision = the recorder's; Axis = an ONVIF user's).
3. `camara_ficha {"ip"}`: full ONVIF (profiles with their exact URL, PTZ, image) + GStreamer's analysis of the stream + `plan`. Read the plan to the operator in two lines.
4. `camara_agregar {"ip", "escena", "nombre"}` with `plan.argumentos` (if it is already connected, `reusar: true` adds it to another scene without opening another stream; if the ONVIF password arrives later, `solo_ptz: true` + `entrada` sets up its PTZ): the input ends up synchronized (buffer and studio clock), normalized only if needed, and with its ONVIF PTZ if it has one. Ask which scene it goes in if they didn't tell you (don't put it in the one on air without asking). It waits for it to connect and returns `estado.diagnostico`: apply what it proposes (e.g. `video_entrada_red {"nombre", "ntp_sync": false}` if the camera's clock is off; `video_entrada_red` on an existing input updates it).
5. `video_estado {"entrada"}` a few seconds later: real resolution and diagnosis. If something is still wrong, the diagnosis gives the fix.

## Good to know
- A **recorder** (DVR/NVR) has one camera per channel: `/Streaming/Channels/101` is channel 1 (main), `102` the secondary, `201` channel 2… Each channel is added separately (`camara_agregar` with the channel's `url`).
- If the brand was recognized only by the MAC, confidence is low: if no URL works, ask the operator for the model or the URL.
- EZVIZ uses Hikvision MACs: the ONVIF name (CS-…) wins.
- `camara_urls {"ip"}` tests the URLs again (after saving the password or enabling RTSP).
- Devices with neither RTSP nor ONVIF (routers, PCs) are not cameras: don't offer them.
- A network camera can also be added by hand with `video_entrada_red` if they give you the URL.
