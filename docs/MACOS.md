# macOS (Apple Silicon) — cómo compilarlo y probarlo

Rama `macos`. Todo esto está **escrito pero sin compilar todavía en una Mac**:
lo que falle, se corrige con lo que muestre tu M1. Cada paso deja un log; si
algo sale mal, mandame el final de ese log (se indica abajo cuál).

## 1. Requisitos (una sola vez)

- macOS 13 o más nuevo, en una Mac con Apple Silicon (M1 en adelante).
- **Xcode completo** (App Store; las Command Line Tools solas no alcanzan:
  libobs en macOS solo compila con el generador de Xcode). Después:
  ```bash
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
  sudo xcodebuild -license accept
  ```
- Herramientas de build con Homebrew (solo para compilar; nada de esto viaja
  en el bundle):
  ```bash
  brew install cmake ninja meson pkg-config
  ```

## 2. Traer el código

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
git checkout macos
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
| Módulos LSP (rack de audio), **opcional** | fuente oficial + cairo/fontconfig/freetype estáticos (`tools/deps/lsp-macos.sh`) | `extern/.build-lsp-macos/lsp-build.log` |

La primera vez tarda (libobs ~10-20 min en un M1). Si LSP falla, `deps-macos`
sigue igual: el rack de audio funciona sin los módulos LSP (analizador y
generador sí).

## 4. Compilar y correr los tests (`make macos`)

```bash
make macos
```

Configura con Ninja, compila todo y corre `ctest`. Esperado: todos los tests
pasan o se saltean (los que necesitan red o SDK externos). Si algo no compila,
mandame las primeras líneas con `error:` de la salida.

## 5. Bundle portable (`make release-macos`)

```bash
make release-macos
```

Arma `release/` con el mismo layout que Linux (`BroadcastAppliance`,
`bin/64bit/`, `obs-plugins/64bit/`, `data/`) y lo verifica con
`tools/deps/check-portable-macos.sh`: cada binario solo puede depender de
macOS o de lo que viaja adentro, y todos tienen que estar firmados (ad-hoc).
Si marca `FALTA` o `RUTA DE LA MAQUINA DE BUILD`, mandame esas líneas.

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

Logs: `release/Logs/ba.log` (y la salida de la terminal).

## Qué falta o no está probado

- **Nada de esto corrió todavía en una Mac.** Lo específico de macOS se
  verificó compilando esas ramas en Linux (sintaxis) y con los headers reales
  de OBS/Qt/CLAP; el enlace y la ejecución se validan en tu M1.
- **NDI** apagado en macOS por ahora (falta armar libmicrodns + FFmpeg de
  obs-deps-macos; en Linux y Windows anda).
- Video entre procesos: se usa el camino de CPU (común a todas las
  plataformas). El de GPU sin copia (IOSurface) es una mejora posterior.
- **Firma**: ad-hoc, alcanza para correr en la Mac donde se compila. Para
  distribuir a otras Macs hace falta Developer ID + notarización (decisión D8
  del plan multiplataforma) y empaquetarlo como `.app` dentro de un `.dmg`.
- La app todavía no es un `.app`: se abre desde la terminal o con doble clic
  en `BroadcastAppliance` (abre una Terminal).
- CI: el workflow `macOS` (`.github/workflows/macos.yml`) corre solo a mano;
  entra al release automático cuando macOS esté validado.
