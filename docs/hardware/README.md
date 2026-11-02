# Hardware: cómo se comporta el sistema en cada máquina

Una página por máquina medida: la ficha del equipo, **qué números esperar** (CPU, hilos,
memoria, GPU, red, cuadros por segundo de la grilla, edad del audio) con 10, 20, 30 y 40
invitados, hasta cuántos aguanta con margen y cómo repetir la medición. Sirven para saber si
una instalación anda «como debe»: si en el mismo hardware los números se alejan mucho de los
de la tabla, algo cambió (driver, plan de energía, otra carga, una regresión).

| Máquina | Sistema | Rol en la medición | Página |
|---|---|---|---|
| AMD Ryzen 9 5900X + Radeon RX 5700 XT, 32 GB | Windows 11 | la app (hinchada desde la Mac por la LAN) | [pc-ryzen9-5900x-rx5700xt.md](pc-ryzen9-5900x-rx5700xt.md) |
| MacBook Pro M1 Pro (8 núcleos), 32 GB | macOS 26 | la app y hinchada en la misma máquina | referencia en la página de la PC (sección «Comparación con la Mac») |

## Cómo se mide

Con la «Prueba de sala» por línea de comandos, `tools/carga/medir-sala.py` (ver su docstring):
[hinchada](../funciones/PRUEBA-SALA.md) mete N invitados reales por WebRTC (video grabado
720p/360p, voz sintética, uno hablando), se espera a que la sala se estabilice y se miden
60 s de régimen más 20 s de cuadros por celda de la grilla de personas
(`grilla_personas_medir`). Cada escalón deja `resumen.md`/`resumen.json` y la serie se
pasa a tabla con `tools/carga/tabla-sala.py`.

- **Local** (la app y hinchada en la misma máquina): la carga del generador se suma a la de
  la app; los números son pesimistas.
- **Remoto** (`--remoto HOST`): la app en otra máquina; hinchada genera desde acá y la red
  entre las dos es parte de la prueba. Los procesos, la GPU y la red de la máquina medida se
  toman por ssh (`tools/carga/muestrear-win.ps1`).

Para agregar una máquina: copiar la página de la PC como plantilla, correr la serie
10/20/30/40 con la máquina quieta (sin compilaciones ni otras pruebas) y guardar las
mediciones crudas en `mediciones-carga/<fecha>-<máquina>/` (fuera del repo).
