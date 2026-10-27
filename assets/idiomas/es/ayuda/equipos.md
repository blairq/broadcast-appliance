Un catalogo unico de equipos, con acciones declaradas por tipo. Es el camino general para "¿que hay conectado?" y "hacele X a tal equipo".

## El camino
1. `equipos_escanear {"detector"?}` (red, local, ndi, usb; sin detector, todos): cada equipo con `id` estable («red:192.168.1.90», «local:video:<id de la camara>», «local:audio:<id del microfono>», «local:salida:<id>», «usb:046d:0881:…»), `tipo`, `nombre`, si esta `conectado` y sus `acciones`. `equipos_listar` da lo ya encontrado sin buscar.
2. `equipo_ver {"id"}`: todo lo que se sabe y cada accion con sus parametros.
3. `equipo_accion {"id", "accion", "args"}`.

## Acciones utiles
- Camara de la red: `pedir_clave` (formulario seguro: nunca pidas la clave por el chat), `ficha` (ONVIF + analisis + plan), `funciones_onvif` (que ejes se mueven y como, limites, foco, home, presets, auxiliares, y su posicion), `onvif` (una operacion cruda: `servicio` ptz|media|imaging|device y `cuerpo` con el XML del Body; {perfil}, {config_ptz} y {fuente} se reemplazan), `http` (la API propia de la marca: Axis VAPIX `/axis-cgi/com/ptz.cgi?query=limits`, Hikvision ISAPI…), `agregar`, `activar_ptz`.
- Camara de esta maquina: `agregar` (como entrada de video, con su nombre y su formato preferido). Microfono: `agregar` (entrada de audio). NDI: `agregar`.

## Nombres de los dispositivos (de la maquina)
Las camaras, capturadoras, microfonos, placas de sonido, emisores NDI y camaras de red tienen el nombre que les pone el operador en «Dispositivos y equipos»: `equipo_renombrar {"id", "nombre"}` (vacio: vuelve al del sistema). Es de la MAQUINA, no del evento, y va pegado al id estable del equipo. Las entradas de video y de audio, el mezclador y las salidas lo muestran asi, y una entrada nueva se llama asi. `equipo_configurar {"id", "config": {"nota", "modo"}}` (`modo`: el formato preferido de una camara). Los que la maquina ya vio y hoy no estan siguen en la lista como ausentes (`estado` «ausente»): una entrada que los usa los espera y vuelve sola; `equipo_olvidar` borra uno ausente.
- USB con camara UVC: `explorar_uvc` (PTZ estandar y unidades de extension de la marca: el primer paso para un plugin).

## Drivers y cuarentena
Cada driver o proveedor de dispositivos de la maquina (los de GStreamer: WASAPI, Media Foundation, Core Audio, AVFoundation, Pulse, V4L2, DeckLink, plugins; y cada driver ASIO en Windows) se prueba en un proceso aparte con tiempo limite: si se cuelga o se cae, la app sigue. A las 4 fallas seguidas (ajuste `drivers.fallasCuarentena`) queda EN CUARENTENA: no se vuelve a cargar solo, ni al arrancar ni cuando cambian los dispositivos. `drivers_listar` da el estado de cada uno con el `motivo` (p. ej. JackRouter: «no contesta: ¿está corriendo el servidor de JACK?»). `driver_probar {"id"}` lo prueba una vez mas (despues de arreglar la causa); `drivers_revisar` vuelve a probar todo. Es POR MAQUINA: en una maquina nueva se prueba todo al arrancar; en una conocida, solo lo nuevo (o lo que cambio de version).

## Diagnosticar un PTZ que no responde
`funciones_onvif` dice que declara la camara; `onvif` con `<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>` dice donde esta; probar un movimiento (`RelativeMove`, `AbsoluteMove`) y volver a mirar. Si el protocolo acepta y la posicion no cambia, probar la API de la marca con `http`; si tampoco, es la camara (mecanica o un limite en su configuracion).