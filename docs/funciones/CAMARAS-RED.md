# Cámaras y equipos de video en la red

Encontrar las cámaras IP, grabadores y equipos de video de la red, saber qué son y conectarlos bien al estudio
(entrada sincronizada, normalización mínima, PTZ). Código: `src/app/red/`. Interfaz: Entradas → «Buscar cámaras en
la red…». MCP: `red_escanear` → `camara_credenciales` → `camara_ficha` → `camara_agregar` (ayuda: `camaras_red`).

## Escanear (`red::Red::escanear`, unos 10 s)
1. **ONVIF**: WS-Discovery por multicast (`ptz/OnvifBusqueda`). De la respuesta salen el `device_service`, el
   nombre y el modelo. Si anuncia varias direcciones (una 169.254 autoasignada), se toma la de la subred barrida.
2. **Barrido de la subred** de cada placa Ethernet o Wi-Fi (hasta /24; las virtuales —puentes, VPN— no):
   conexión TCP a 554, 8554 (RTSP), 80, 88, 8080 (web), 8000 (SDK Hikvision), 37777 (SDK Dahua), 1935 (RTMP)
   y 5960 (NDI). 256 conexiones a la vez, 600 ms cada una.
3. **MAC y fabricante**: la tabla ARP del sistema (`arp -an` / `arp -a`) y la base OUI de la IEEE
   (`datos/oui.tsv`, generada con `tools/generar/oui-generar.py` desde el `manuf` de Wireshark; se vendorea, no hace
   falta internet).
4. **La web**: el encabezado `Server` y el `<title>`.
5. **La marca** (`red::Biblioteca`, `datos/camaras-urls.json`): por ONVIF, después la web, después la MAC,
   después un puerto propio. La MAC sola engaña (EZVIZ usa MACs de Hikvision); un equipo sin RTSP ni ONVIF no
   recibe marca de cámara (es el router o una PC de esa marca).
6. **Las URLs RTSP típicas** de la marca, probadas con un `DESCRIBE` (`red::rtsp`): «se ve» (200), «pide usuario
   y clave» (401), «no existe» (404/400/454). Con la clave guardada del equipo se reintenta con Digest MD5 o Basic.
   En un grabador se prueban los canales.

Cada equipo sale con su `siguiente_paso`: lo que el asistente tiene que hacer.

## Credenciales
Por equipo, cifradas: `Credenciales/camara-<ip>` (`net/Credentials`). Por MCP, `camara_credenciales {"pedir":
true}` muestra el formulario seguro de la barra del operador (plataforma `camara:<ip>`): el asistente nunca ve la
clave. La interfaz del escáner tiene su propio formulario.

## La ficha (`red::Red::ficha`)
Con la clave: ONVIF completo —`GetSystemDateAndTime` (la hora de la cámara: la autenticación WS-Security la
usa), `GetDeviceInformation`, `GetCapabilities`, `GetProfiles` (códec, tamaño, fps, bitrate de cada perfil),
`GetStreamUri` por perfil, `GetNodes` y `GetPresets` del PTZ, `GetImagingSettings`— y el análisis con
`GstDiscoverer` del stream que conviene (lo que de verdad manda: tamaño, fps, entrelazado, píxel, colorimetría,
audio). Sin ONVIF, se analiza la URL que se vio en el escaneo.

## El plan (`red::planDe`)
- **Perfil**: el más chico que cubra el lienzo; H.264/H.265 (por hardware: VideoToolbox) antes que MJPEG (CPU).
- **Normalizar**: solo si hace falta —entrelazado, píxel no cuadrado, cuadros variables o distintos del
  lienzo—. El color (matriz, rango, YUV→RGB) lo convierte libobs en la GPU con lo que declara la fuente.
- **Latencia**: 200 ms en red local cableada.
- **PTZ**: ONVIF con el perfil que tiene PTZ.

## Agregar (`red::Red::agregar`)
Una entrada de red (`video_entrada_red`: colchón, reloj del estudio, normalización según el plan) con la URL del
perfil y la clave, y el PTZ ONVIF configurado (`ptz::ControlCamaras`, la clave cifrada).

## Agregar una marca
`datos/camaras-urls.json`: `detectar` (expresiones para ONVIF, web y MAC; puertos propios), `rtsp` (rutas en
orden de preferencia; `{canal}` y `"canales": true` para grabadores), `usuario`, `notas`, `ptz`. Si un modelo no
está, la referencia de URLs es https://www.ispyconnect.com/cameras .

## Pruebas
`test_red`: la base OUI con MACs reales, el reconocimiento de marcas (incluida la EZVIZ con MAC de Hikvision),
las URLs candidatas, RTSP (Digest calculado aparte) y el plan.
