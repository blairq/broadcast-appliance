# MacBook Pro M1 Pro (macOS 26)

La Mac de desarrollo. Medida el **2026-11-02** con la app y el generador de invitados
(hinchada) **en la misma máquina**, de 10 a 40 invitados. Desde esa noche la Mac se usó como
**generadora** de la carga para la [PC con Windows](pc-ryzen9-5900x-rx5700xt.md) (hasta 200
invitados) y no se volvió a medir como anfitriona: **no tiene escalera por encima de 40 ni
medición después del gateway en varios núcleos** (ver «Lo que no se midió»).

## Ficha

| | |
|---|---|
| Equipo | MacBook Pro 14" (2021, `MacBookPro18,3`) |
| Chip | Apple M1 Pro: CPU de **8 núcleos** (**6 de rendimiento + 2 de eficiencia**), GPU de 14 núcleos, Neural Engine de 16 núcleos |
| Motor de medios | decodificación y codificación H.264/HEVC por hardware (VideoToolbox) |
| Memoria | 32 GB unificada (CPU y GPU comparten la misma) |
| Sistema | macOS 26 (26.5.1 al escribir esto) |
| Red de la prueba | ninguna: app y generador en la misma máquina (127.0.0.1) |

### Qué usa la app de esta máquina

- **Motor de video (ba-app)**: GStreamer con la composición en la GPU del chip (GL).
- **Video de los invitados (ba-gateway)**: desde la campaña de la tarde del 2026-11-02, el
  H.264 que suben los invitados se **decodifica con VideoToolbox** y el retorno se codifica
  también con VideoToolbox (`room_status`: «sube H264 (placa: VideoToolbox)»; `gateway_estado`
  → `codecsVideo`: 65 decodificadores activos por hardware con 40 invitados).

## Números esperados (la app y hinchada en la misma Mac)

Sala de prueba con la grilla de personas al aire (`alta_hasta` 10: hasta 10 personas todas en
alta 720p30; con más, miniaturas en baja y el orador en alta), hinchada `--modo grabado
--hablan 1` (video grabado, voz sintética, uno hablando), camino directo, 60 s de régimen
cada corrida. CPU en **% de un núcleo** (100 % = un núcleo entero); «máquina» en % de los 8
núcleos, **con hinchada adentro** (su parte figura aparte): los números son pesimistas.

**Con H.264 por VideoToolbox** (campaña de la tarde, camino directo):

| Invitados | 10 | 20 | 30 | 40 |
|---|---|---|---|---|
| ba-app CPU p50 | 50 % | 64 % | 75 % | 101 % |
| ba-gateway CPU p50 | 87 % | 62 % | 85 % | 104 % |
| ba-gateway hilos (máx.) | 73 | 86 | 97 | 135 |
| hinchada (el generador) CPU p50 | 30 % | 46 % | 66 % | 92 % |
| Máquina entera p50 (con hinchada) | 53 % | 54 % | 65 % | 75 % |
| Memoria disponible (mínimo) | 12,6 GB | 11,6 GB | 11,4 GB | 11,0 GB |
| Grilla en el programa: mosaico / celda p50 | 30 / 29,5 cps (alta) | 30 / 15,0 (baja) | 30 / 15,0 | 30 / 15,0 |
| Cuadros perdidos del mosaico | 0 | 0 | 0 | 0 |
| Edad del audio de los invitados (p50) | 10,9 ms | 14,4 ms | 11,9 ms | 13,4 ms |
| Salida del gateway (retornos) | 15,6 Mbps | 30,8 Mbps | 46,7 Mbps | 64,4 Mbps |
| Conectar (desde el lanzamiento, p95) | 1,5 s | — | — | 4,4 s |

**Antes, con VP8 por software** (la serie de la mañana, después de los arreglos de la grilla):

| Invitados | 10 | 20 | 30 | 40 |
|---|---|---|---|---|
| ba-app CPU p50 | 53 % | 60 % | 73 % | 86 % |
| ba-gateway CPU p50 | 93 % | 71 % | 88 % | 108 % |
| ba-gateway hilos (máx.) | 66 | 71 | 82 | 93 |
| Máquina entera p50 (con hinchada) | 40 % | 37 % | 43 % | 50 % |
| Edad del audio (p50) | 10,9 ms | 10,4 ms | 12,9 ms | 14,1 ms |
| Faltantes del retorno de audio (máx. en 10 s) | 0 | 10 | 8 | 25 |
| Salida del gateway | 16,5 Mbps | 32,5 Mbps | 50,4 Mbps | 67,9 Mbps |

Cómo leerlas:

- **40 invitados andan sin degradarse**: grilla clavada a 30 cps sin perdidos, celdas en baja a
  15 cps, audio en 11-15 ms y todos conectados en menos de 5 s. Con 40 la máquina entera
  queda al 50-75 % **contando el generador**, que con 40 ya usa casi un núcleo.
- **10 es el caso caro para el gateway** (los diez en alta 720p30), como en la PC.
- La salida crece ~1,6 Mbps por invitado (un retorno por invitado).
- Comparada con la PC de Windows en las mismas condiciones (antes del gateway en varios
  núcleos), la Mac gasta menos CPU por proceso (el gateway y el motor usan el motor de medios
  del chip) pero tiene 8 núcleos contra 24 hilos: queda menos holgada como máquina. Ver
  «Comparación con la Mac» en la [página de la PC](pc-ryzen9-5900x-rx5700xt.md).

## Lo que no se midió

- **Más de 40 invitados.** La rampa en la Mac se cortó en 40; con la app y el generador en la
  misma máquina, el generador ya come casi un núcleo con 40, así que para pasar de ahí hace
  falta generar desde otra máquina (`medir-sala.py --remoto`, como se hizo con la PC).
- **El gateway en varios núcleos** (capas de hilos de red y de trabajo, TURN en memoria,
  playout repartido, video según demanda): llegó después de estas mediciones y en la Mac no
  tiene serie propia. De esa tanda solo se midieron en la Mac las **colas compartidas** de
  libwebrtc, con 10 y 40 (directo): con 40, gateway 103 → 99 %, hilos 108 → 100, cambios de
  contexto 25,9 → 24,3 mil/s, grilla igual. En macOS la ganancia es chica porque las colas
  ya eran de GCD (hilos compartidos); la grande era para Windows, donde cada cola era un hilo.
- **TURN y pinggy** en la Mac (la campaña de la tarde quedó con solo directo).

No hay un techo medido para esta Mac: **40 con margen** es lo único que se puede afirmar.
Para saber cuántos aguanta hoy, la pestaña **Configuración → Sistema → Capacidad** hace la
rampa sola ([CAPACIDAD.md](../funciones/CAPACIDAD.md)).

## La Mac como generadora

Para las mediciones de la PC, hinchada corrió en esta Mac por Wi-Fi 5 decodificando todos los
retornos (`--decodificar todo`): ~30 % de la máquina con 40, 52 % con 80, 75 % con 120 y
**83-87 % p50 (100 % de pico) con 160-200**. Desde 160 es la Mac la que limita la prueba: los
congelamientos del lado del invitado con 120-200 son en buena parte de ella.

## Cómo repetir la medición

Con la app de la Mac abierta en un evento de prueba y la máquina quieta (sin compilaciones ni
otras pruebas):

```sh
for n in 10 20 30 40; do
  tools/carga/medir-sala.py --participantes $n --cupo --regimen 60 --medir-grilla-s 20 \
    --salida <carpeta-de-mediciones>/mac-$n -- --modo grabado --hablan 1
done
```

Cada corrida, 2 minutos como mucho; al terminar, cerrar la app (`app_quit`). Las mediciones
crudas quedan fuera del repo.
