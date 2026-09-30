# macOS (Apple Silicon) — cómo compilarlo y probarlo

Validado el 2026-09-30 en una Mac Apple Silicon con macOS 26.5, Xcode 26.6 y
CMake 4.4 (Homebrew): compila todo el árbol, pasan los tests (`ctest`), el
bundle portable se verifica y el sistema completo levanta (ver "Qué está
probado" al final). Cada paso deja un log; si algo sale mal, el final de ese
log dice qué fue (se indica abajo cuál).

## 1. Requisitos (una sola vez)

- macOS 13.3 o más nuevo, en una Mac con Apple Silicon (M1 en adelante).
  (13.3 lo pide el ONNX Runtime oficial de arm64.)
- **Xcode completo** (App Store; las Command Line Tools solas no alcanzan:
  libobs en macOS solo compila con el generador de Xcode). Si `xcode-select`
  sigue apuntando a las Command Line Tools, el Makefile usa
  `/Applications/Xcode.app` solo (no hace falta `sudo xcode-select -s`). La
  licencia sí hay que aceptarla una vez: `sudo xcodebuild -license accept`.
- Herramientas de build con Homebrew (solo para compilar; nada de esto viaja
  en el bundle):
  ```bash
  brew install cmake ninja meson pkg-config make
  ```
  (`make` instala `gmake`, GNU make 4: el de macOS es 3.81 y el build de los
  módulos LSP no anda con él. El Makefile del proyecto sí anda con el de macOS.)

## 2. Traer el código

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
git checkout macos-port
python3 -m venv .venv
```

## 3. Dependencias (`make deps-macos`)

```bash
make deps-macos
```

Baja (con hash) y compila en `extern/` todo lo de terceros:

| Qué | De dónde | Log si falla |
|---|---|---|
| libobs 31 + plugins (cámara, pantalla, audio, VideoToolbox, x264, RTMP...) | fuente del tag fijado + obs-deps oficiales de macOS arm64, con Xcode (`tools/deps/obs-macos.sh`) | `extern/.build-obs-macos.configure.log` / `.build.log` |
| Qt 6.6.3 (macOS) | aqtinstall en `extern/qt6-host` | la salida de make |
| libwebrtc M123 (macOS arm64) | Shiguredo, mismo tag que Linux/Windows | la salida de make |
| mbedTLS, nlohmann, libsamplerate, Argon2, lwIP, CLAP, cloudflared (darwin-arm64) | como en Linux | la salida de make |
| libmicrodns (buscar televisores por mDNS) | fuente fijado por commit (`tools/deps/ndi-src.sh`) | la salida de make |
| ONNX Runtime + modelos (reconocer caras), **opcional** | `make deps-rostros` (baja el de macOS arm64 solo) | la salida de make |
| Módulos LSP (rack de audio), **opcional** (con `gmake`) | fuente oficial + cairo/fontconfig/freetype estáticos (`tools/deps/lsp-macos.sh`) | `extern/.build-lsp-macos/lsp-build.log` |

La primera vez tarda (libobs ~10-20 min en un M1). Si LSP falla, `deps-macos`
sigue igual: el rack de audio funciona sin los módulos LSP (analizador y
generador sí).

## 4. Compilar y correr los tests (`make macos`)

```bash
make macos
```

Configura con Ninja, compila todo y corre `ctest`. Esperado: todos los tests
pasan o se saltean (los que necesitan red o SDK externos). Si algo no compila,
las primeras líneas con `error:` de la salida dicen qué.

## 5. Bundle portable (`make release-macos`)

```bash
make release-macos
```

Arma `release/` con el mismo layout que Linux (`BroadcastAppliance`,
`bin/64bit/`, `obs-plugins/64bit/`, `data/`) y lo verifica con
`tools/deps/check-portable-macos.sh`: cada binario solo puede depender de
macOS o de lo que viaja adentro, cada ejecutable tiene que resolver sus
`@rpath` con sus propios rpath, y todos tienen que estar firmados (ad-hoc).
Al final escribe `manifest.json` (la verificación de integridad del launcher).

## 6. Probarlo

Primero el motor solo, sin interfaz (arranca libobs, codifica y graba 4 s):

```bash
cd release && ./bin/64bit/ba-app --engine-selftest
```

Esperado: `autotest del motor: OK`. Después, la app completa:

```bash
cd release && ./BroadcastAppliance
```

La primera vez macOS pide permisos (cámara, micrófono y, si agregás una
captura de pantalla, **Grabación de pantalla** en Ajustes → Privacidad y
seguridad). Al conceder el de pantalla hay que reabrir la app.

Qué mirar:

1. Que abra la interfaz y el preview muestre video.
2. **Entradas**: que aparezcan las cámaras de la Mac (`macos-avcapture`).
3. **Audio**: micrófono (`coreaudio_input_capture`) con medidores; rack de
   audio (si compiló LSP, abrir el editor de un módulo: ventana Cocoa).
4. **Sala de invitados**: entrar desde otra máquina o el celular; video y audio
   del invitado (libwebrtc real en macOS).
5. Grabar 10 s y transmitir a un destino de prueba (VideoToolbox H.264 por
   hardware aparece como codificador).

Sin interfaz (por SSH, o para probar el gateway y el motor solos):

```bash
cd release && BA_GATEWAY_PIN=123456 ./BroadcastAppliance --no-recovery-ui --app-arg=--no-ui
```

Logs: `release/Logs/` (`launcher.log`, `app.log`, `gateway.log`) y la salida
de la terminal.

## Qué está probado (2026-09-30)

- `make macos`: compila todo (núcleo, launcher, gateway con libwebrtc real,
  `ba-app` con Qt y libobs, `ba-grafico`, `ba-sources`, `ba-mcp`) y pasan los
  67 tests de `ctest` (se saltea solo el de integración del gateway, que pide
  red). Incluye `test_webrtc_loopback` (libwebrtc real), los de procesos
  cruzados y los del rack de audio contra los módulos LSP reales.
- `make release-macos`: el bundle pasa `check-portable-macos.sh` y el manifest.
- `ba-app --engine-selftest` desde el bundle: libobs arranca, carga los
  plugins y `ba-sources`, graba 4 s; VideoToolbox (H.264/HEVC por hardware)
  aparece entre los codificadores.
- El launcher: integridad OK, lanza B y A, la sala (HTTPS 8789) y la consola
  (8788) responden, y el cierre ordenado pasa por los canales de control.
- La interfaz Qt/QML abre y dibuja (`ba-app` sin `--no-ui`).

## Qué falta o no está probado

- A mano, con cámara/micrófono reales: entradas, rack de audio con el editor
  Cocoa de LSP, invitados desde otra máquina, emisión RTMP.
- La primera vez que se ejecutan binarios recién firmados, macOS los analiza
  y el arranque puede tardar bastante más (se vio una vez: el launcher no
  encontró el canal de A y lo cerró con SIGTERM). No se volvió a reproducir.
- **NDI** apagado en macOS por ahora (falta FFmpeg de obs-deps-macos para
  libNoIDea; en Linux y Windows anda). La búsqueda de Chromecast por mDNS sí
  compila (libmicrodns desde el fuente), sin probar contra un televisor.
- Video entre procesos: se usa el camino de CPU (común a todas las
  plataformas). El de GPU sin copia (IOSurface) es una mejora posterior.
- **Firma**: ad-hoc, alcanza para correr en la Mac donde se compila. Para
  distribuir a otras Macs hace falta Developer ID + notarización (decisión D8
  del plan multiplataforma) y empaquetarlo como `.app` dentro de un `.dmg`.
- La app todavía no es un `.app`: se abre desde la terminal o con doble clic
  en `BroadcastAppliance` (abre una Terminal).
- CI: el workflow `macOS` (`.github/workflows/macos.yml`) corre solo a mano;
  entra al release automático cuando macOS esté validado.
