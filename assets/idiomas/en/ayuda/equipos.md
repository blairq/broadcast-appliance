A single catalog of devices, with actions declared by type. It is the general path for "what is connected?" and "do X to that device".

## The path
1. `equipos_escanear {"detector"?}` (red, local, ndi, usb; without a detector, all of them): each device with a stable `id` (“red:192.168.1.90”, “local:video:<camera id>”, “local:audio:<microphone id>”, “local:salida:<id>”, “usb:046d:0881:…”), `tipo`, `nombre`, whether it is `conectado` and its `acciones`. `equipos_listar` gives what was already found without searching.
2. `equipo_ver {"id"}`: everything known and each action with its parameters.
3. `equipo_accion {"id", "accion", "args"}`.

## Useful actions
- Network camera: `pedir_clave` (secure form: never ask for the password in the chat), `ficha` (ONVIF + analysis + plan), `funciones_onvif` (which axes move and how, limits, focus, home, presets, auxiliaries, and its position), `onvif` (a raw operation: `servicio` ptz|media|imaging|device and `cuerpo` with the Body XML; {perfil}, {config_ptz} and {fuente} are replaced), `http` (the brand's own API: Axis VAPIX `/axis-cgi/com/ptz.cgi?query=limits`, Hikvision ISAPI…), `agregar`, `activar_ptz`.
- Camera on this machine: `agregar` (as a video input, with its name and its preferred format). Microphone: `agregar` (audio input). NDI: `agregar`.

## Device names (on the machine)
Cameras, capture cards, microphones, sound cards, NDI senders and network cameras have the name the operator gives them in “Devices and equipment”: `equipo_renombrar {"id", "nombre"}` (empty: goes back to the system's name). It belongs to the MACHINE, not the event, and is tied to the device's stable id. Video and audio inputs, the mixer and the outputs show it that way, and a new input is named that way. `equipo_configurar {"id", "config": {"nota", "modo"}}` (`modo`: a camera's preferred format). The ones the machine has already seen and aren't here today stay on the list as absent (`estado` “ausente”): an input that uses them waits for them and comes back on its own; `equipo_olvidar` deletes an absent one.
- USB with a UVC camera: `explorar_uvc` (standard PTZ and the brand's extension units: the first step toward a plugin).

## Drivers and quarantine
Each driver or device provider on the machine (the GStreamer ones: WASAPI, Media Foundation, Core Audio, AVFoundation, Pulse, V4L2, DeckLink, plugins; and each ASIO driver on Windows) is tested in a separate process with a time limit: if it hangs or crashes, the app keeps going. After 4 failures in a row (setting `drivers.fallasCuarentena`) it goes INTO QUARANTINE: it is not loaded again on its own, neither at startup nor when the devices change. `drivers_listar` gives the state of each one with the `motivo` (e.g. JackRouter: “not answering: is the JACK server running?”). `driver_probar {"id"}` tests it once more (after fixing the cause); `drivers_revisar` tests everything again. It is PER MACHINE: on a new machine everything is tested at startup; on a known one, only what's new (or what changed version).

## Diagnosing a PTZ that doesn't respond
`funciones_onvif` tells what the camera declares; `onvif` with `<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>` tells where it is; try a move (`RelativeMove`, `AbsoluteMove`) and look again. If the protocol accepts it and the position doesn't change, try the brand's API with `http`; if that doesn't work either, it's the camera (mechanics or a limit in its configuration).
