# Hardware: cómo se comporta el sistema en cada máquina

Una página por máquina medida: la ficha del equipo, **qué números esperar** (CPU, hilos,
memoria, GPU, red, cuadros por segundo de la grilla, edad del audio) según la cantidad de
invitados, hasta cuántos aguanta con margen y cómo repetir la medición. Sirven para saber si
una instalación anda «como debe»: si en el mismo hardware los números se alejan mucho de los
de la tabla, algo cambió (driver, plan de energía, otra carga, una regresión).

El resumen en inglés, con el modelo de costos y las condiciones de la prueba, está en
[docs/en/capacity.md](../en/capacity.md).

| Máquina | Sistema | Invitados (medido) | Rol en la medición | Página |
|---|---|---|---|---|
| AMD Ryzen 9 5900X (12 núcleos / 24 hilos) + Radeon RX 5700 XT, 32 GB | Windows 11 | **~160 con margen; 200 entran y andan** | la app (hinchada desde otra máquina por la LAN); campaña por camino (directo, TURN, pinggy), códecs, núcleos con ETW, escalera hasta 200 | [pc-ryzen9-5900x-rx5700xt.md](pc-ryzen9-5900x-rx5700xt.md) |
| MacBook Pro M1 Pro (8 núcleos: 6 de rendimiento + 2 de eficiencia), 32 GB | macOS 26 | **40 con margen** (no se midió más) | la app y hinchada en la misma máquina, 10-40 directo | [mac-m1-pro.md](mac-m1-pro.md) |

## Cómo se mide

Para el operador, la pestaña **Configuración → Sistema → Capacidad** hace una rampa
automática y da un número ([CAPACIDAD.md](../funciones/CAPACIDAD.md)). Lo de abajo es la
medición de laboratorio, con todo el detalle interno.

Con la «Prueba de sala» por línea de comandos, `tools/carga/medir-sala.py` (ver su docstring):
[hinchada](../funciones/PRUEBA-SALA.md) mete N invitados reales por WebRTC (video grabado
720p/360p/180p en simulcast, voz sintética, uno hablando), se espera a que la sala se
estabilice y se miden 45-60 s de régimen más 15-20 s de cuadros por celda de la grilla de
personas (`grilla_personas_medir`); cada corrida, 2 minutos como mucho. Cada escalón deja
`resumen.md`/`resumen.json` y la serie se pasa a tabla con `tools/carga/tabla-sala.py`.

- **Local** (la app y hinchada en la misma máquina): la carga del generador se suma a la de
  la app; los números son pesimistas.
- **Remoto** (`--remoto <PC>`): la app en otra máquina; hinchada genera desde acá y la red
  entre las dos es parte de la prueba. Los procesos, la GPU y la red de la máquina medida se
  toman por ssh (`tools/carga/muestrear-win.ps1`). Con más de ~150 invitados el generador
  también se carga: para 256-512 hace falta generar desde dos máquinas.

Para agregar una máquina: copiar una de las páginas como plantilla, correr la serie
10/20/30/40 (y la escalera si hace falta) con la máquina quieta (sin compilaciones ni otras
pruebas) y guardar las mediciones crudas fuera del repo. En las páginas, nada de direcciones,
nombres de máquina ni rutas personales.
