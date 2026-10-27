Para "¿que camaras hay?", "agregá la camara del patio", "conectá el DVR", "tengo una Axis/Hikvision/EZVIZ…".

## El camino (siempre este)
1. `red_escanear` (unos 10 s): cada equipo con su IP, MAC y fabricante, la marca (y por que la reconocio), sus puertos, las URLs RTSP tipicas probadas (`se ve` / `pide usuario y clave` / `no existe`), `notas` de la marca y `siguiente_paso`. **Segui el `siguiente_paso`.** Contale al operador lo que encontraste en pocas lineas (cuantas camaras, de que marca, cuales andan ya y cuales piden clave), no le tires el JSON.
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
2. Si pide clave: `camara_credenciales {"ips": [...], "pedir": true}` (UN formulario para todas las que la pidan; si ya hay uno abierto, las suma a ese. Vuelve enseguida: cuando el operador lo completa te llega «[OPERADOR] guardo…» al chat; mientras, segui con lo demas). Es un formulario seguro en la barra: **nunca le pidas la clave por el chat**. Antes decile que usuario va (`usuario_de_fabrica`) y de donde sale la clave (las `notas`: EZVIZ = el codigo de verificacion de la etiqueta, y el RTSP hay que habilitarlo en su app; Hikvision = la del grabador; Axis = la de un usuario ONVIF).
3. `camara_ficha {"ip"}`: ONVIF completo (perfiles con su URL exacta, PTZ, imagen) + el analisis de GStreamer del stream + `plan`. Leele el plan al operador en dos lineas.
4. `camara_agregar {"ip", "escena", "nombre"}` con `plan.argumentos` (si ya esta conectada, `reusar: true` la suma a otra escena sin abrir otro stream; si la clave ONVIF llega despues, `solo_ptz: true` + `entrada` le configura el PTZ): la entrada queda sincronizada (colchon y reloj del estudio), normalizada solo si hace falta, y con su PTZ ONVIF si tiene. Preguntá en que escena va si no te lo dijeron (no la pongas en la que esta al aire sin preguntar). Espera a que conecte y devuelve `estado.diagnostico`: aplica lo que proponga (p. ej. `video_entrada_red {"nombre", "ntp_sync": false}` si el reloj de la camara esta corrido; `video_entrada_red` sobre una entrada existente la actualiza).
5. `video_estado {"entrada"}` a los segundos: resolucion real y diagnostico. Si algo queda mal, el diagnostico dice el arreglo.

## A saber
- Un **grabador** (DVR/NVR) tiene una camara por canal: `/Streaming/Channels/101` es el canal 1 (principal), `102` el secundario, `201` el canal 2… Cada canal se agrega aparte (`camara_agregar` con `url` del canal).
- Si la marca se reconocio solo por la MAC, la confianza es baja: si ninguna URL anda, pedi el modelo o la URL al operador.
- EZVIZ usa MACs de Hikvision: manda el nombre ONVIF (CS-…).
- `camara_urls {"ip"}` vuelve a probar las URLs (despues de guardar la clave o de habilitar el RTSP).
- Equipos sin RTSP ni ONVIF (routers, PCs) no son camaras: no los ofrezcas.
- Una camara de red tambien se puede agregar a mano con `video_entrada_red` si te dan la URL.