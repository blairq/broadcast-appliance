# Plan: control del sistema (Windows, macOS y Linux con las mismas herramientas)

Pedido (2026-10-13): «Control de Windows» tiene que ser un MCP para Windows, Linux y
Mac: **los mismos endpoints en todos los sistemas**, y el servidor decide qué hacer y
cómo, según dónde corre.

## Hoy

`ba-control` (src/app/escritorio): servidor MCP propio (stdio, sin Qt) que los
asistentes lanzan como segundo servidor (`mcp__control`). 24 herramientas `windows_*`:
pantalla, monitores, mouse, teclado, cursor, ejecutar (y como administrador), buscar,
ventanas/ventana, portapapeles, procesos/proceso_cerrar, comando, sistema, esperar,
archivos, registro, aviso, ui_leer/clic/escribir/texto/esperar (UI Automation) y
chrome (CDP). Permisos en `<datos>/Config/control-windows.json`, de fábrica apagado.
Fuera de Windows compila y cada herramienta contesta «solo Windows».

## Cómo queda

- **Herramientas con nombre neutro**, `system_*` (decisión del usuario: system_pantalla,
  system_mouse, system_teclado, system_ventanas, system_ui_leer…): el mismo esquema y la misma
  respuesta en los tres sistemas. Lo que solo existe en uno (el registro de Windows)
  contesta qué hay en su lugar en los otros (por ejemplo, en macOS las preferencias).
- **Un servidor, tres motores**: `ba-control` con una interfaz por capacidad y una
  implementación por sistema (control_win.cpp, control_mac.mm, control_linux.cpp). Lo
  común (MCP, permisos, archivos, comandos, Chrome por CDP) se comparte.
- **Configuración › Control del sistema** (y `control-sistema.json`, que lee el de antes).
  Dice qué permiso del sistema falta y lleva a prenderlo.

## Por sistema

| Capacidad | Windows | macOS | Linux |
|---|---|---|---|
| pantalla, monitores | GDI (como hoy) | CoreGraphics / ScreenCaptureKit (permiso «Grabación de pantalla») | X11 (XGetImage); Wayland: portal de captura |
| mouse, teclado, cursor | SendInput | CGEvent (permiso «Accesibilidad») | XTest; Wayland: portal RemoteDesktop / uinput |
| ejecutar, admin | ShellExecute / runas | open, posix_spawn / «con privilegios de administrador» | xdg-open, posix_spawn / pkexec |
| ventanas | Win32 | CGWindowList + AX | EWMH (X11); Wayland: lo que deje el compositor |
| UI de otros programas | UI Automation | AX (Accesibilidad) | AT-SPI |
| portapapeles | Win32 | NSPasteboard | X11/Wayland (selección) |
| procesos, comando, sistema, archivos, papelera, avisos, esperar | Win32 | POSIX + NSFileManager + notificaciones | POSIX + gio/D-Bus |
| Chrome / Edge (CDP) | igual en los tres (protocolo de red) |||
| registro | Windows | (las preferencias: `defaults`) | (no hay: lo dice) |

## Fases

- **Q1** — Nombres neutros y la interfaz por sistema; Windows igual que hoy detrás de
  ella; Configuración y el MCP interno con el nombre nuevo.
- **Q2** — macOS: pantalla, mouse, teclado, ventanas, ejecutar, portapapeles, procesos,
  sistema, avisos, archivos; los permisos del sistema (detectar y pedir).
- **Q3** — macOS: la UI de otros programas (AX).
- **Q4** — Linux X11; después Wayland (portales).
