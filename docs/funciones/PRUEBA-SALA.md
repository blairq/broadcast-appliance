# Prueba de sala (simulacro antes del evento real)

> Para saber **cuántos invitados aguanta** la máquina, otro equipo o la conexión (una rampa
> automática por escalones, sin tocar el evento), está la pestaña **Capacidad**:
> [CAPACIDAD.md](CAPACIDAD.md). La prueba de sala es el simulacro de UN evento con N.
> Para ensayar la **asamblea** (integrantes que entran por la sesión remota, esperan que los
> habiliten, piden la palabra y votan): [PRUEBA-ASAMBLEA.md](PRUEBA-ASAMBLEA.md).

Antes de un evento con mucha gente, el operador prueba si **la máquina y la red aguantan N
personas**: entran participantes «de prueba» por el **camino real** de los invitados
(`/api/join` + `/ws`, con video y voz), se mide todo y, al terminar, se van todos. Del
evento queda solo el informe.

Página **Pruebas → Prueba de sala** (en la barra lateral, grupo Sistema; la columna derecha de
la página Sala tiene un acceso) o las herramientas MCP `prueba_sala_*`: las dos usan el mismo
modelo (`src/app/ui/PruebaSalaViewModel`). La página **Pruebas** junta todo lo de probar: la
prueba de sala, la [capacidad](CAPACIDAD.md), la [prueba de asamblea](PRUEBA-ASAMBLEA.md) y el
[enlace](ENLACE.md).

## Cómo se usa

1. Abrí el evento que vas a probar (el informe va a su lista de chequeo).
2. Elegí cuántos participantes (1 a 512, el tope de la sala), cuántos minutos, cuántos hablan a la
   vez y cuánto **decodifica el probador** de lo que vuelve (abajo). Si querés repartir la carga,
   **Repartir entre varias máquinas** ([abajo](#con-dos-o-más-pc)).
3. **Revisar la máquina** dice si la guardia deja arrancar (memoria disponible, carga).
4. El **enlace**: la tarjeta dice si la última medición del túnel alcanza para los N y
   ofrece medir ([ENLACE.md](ENLACE.md)). Con hinchada la carga no pasa por el enlace (es
   esta máquina), así que no frena: avisa. El informe final lleva el enlace.
5. **Arrancar la prueba**:
   - con **hinchada** (el probador de carga que viene en el programa, `bin/64bit/hinchada/`):
     esta máquina genera a los N. Cada uno manda un patrón con **su número** grande y una
     voz sintética (frases y respiros); nadie suena por los parlantes. La primera vez,
     hinchada graba su video de prueba (unos minutos) en `Runtime/hinchada/grabado.hgr`;
   - **sin hinchada** (o desde otras máquinas): la tarjeta muestra la URL de la sala, la
     **credencial de prueba** y la línea de hinchada lista para copiar. Sirve cualquier
     generador que entre como un invitado usando la credencial como PIN.
6. Mirá en vivo: cuántos entraron y con video, fallidos, cortes, subida/bajada, CPU de la
   app y del gateway, memoria disponible y cuánto tarda la **HD al dar la palabra** (la
   grilla los muestra en baja; al darle la palabra a uno pasa a alta con el fundido).
7. **Terminar la prueba** (o se termina sola: por tiempo, porque el probador terminó, por
   la guardia o porque la sala la cerró).

## Decodificar lo que vuelve

Cada participante de prueba recibe el **retorno** (el programa) como un invitado. Decodificarlo
es lo que más le cuesta al probador: con «todo» y 200 participantes la máquina que genera llega
al 100 % de CPU y manda sus cuadros a destiempo, y entonces lo medido en la sala es en parte culpa
del generador. Por eso hay tres opciones (`hinchada --decodificar`):

| Opción | Qué hace | Para qué |
|---|---|---|
| **Una muestra** (de fábrica, 5 %) | decodifican de verdad el 5 % (al menos 3 por proceso de hinchada: con 10 participantes, 3); los demás reciben y cuentan sin decodificar | ver que el retorno **llega entero y a su ritmo** (cuadros decodificados, congelamientos) sin cargar la máquina que genera |
| **Nada (solo envía)** | nadie decodifica: se recibe y se cuenta (bytes, cuadros, pérdida, jitter) | lo más liviano, cuando solo importa cuánto aguanta la sala |
| **Todo** | cada uno decodifica lo suyo | lo de antes; solo con pocos participantes o con varias máquinas generando |

La muestra es la de fábrica porque es la más liviana **sin perder la medición del retorno**:
«nada» ahorra un poco más pero no comprueba que el retorno se pueda ver. El informe dice qué se
usó (`probador.decodificar`: el modo, el % pedido y el efectivo) y la **CPU del probador** en
esta máquina (sus procesos y sus hijos: pico y promedio), para comparar entre corridas.

## Con dos o más PC

Para que la app medida no comparta la CPU con el generador, o para llegar a más participantes,
la prueba reparte los N entre esta máquina y otras. hinchada lo hace solo: la app lo levanta como
**coordinador** (`hinchada coordinar`), que reparte los participantes entre los **trabajadores**
que se le suman (`hinchada trabajar`) y junta un solo informe.

**En la PC que se mide (A):**

1. Pruebas → Prueba de sala: los participantes, los minutos, lo que se decodifica.
2. Tildá **Repartir entre varias máquinas**, cuántas **otras máquinas** y si **esta también
   genera** (sin tildar, A solo coordina: lo más limpio para medir).
3. **Arrancar la prueba**. La tarjeta muestra un **código** (`BAGEN1.…`, oculto: **Copiar el
   código**) y la línea de hinchada para quien no tenga el programa, y cuántas máquinas se sumaron.

**En cada PC que genera (B, C…):**

- **Con BroadcastAppliance** (Mac, Windows o Linux): Pruebas → Prueba de sala → **Generar para
  otro equipo**, pegá el código y **Empezar a generar…**. La app lanza sus hinchada y muestra su
  CPU; terminan solos cuando termina la prueba de A (o **Dejar de generar**).
- **Sin el programa**: copiá la carpeta `bin/64bit/hinchada/` de un BroadcastAppliance de la
  misma versión y corré ahí la línea que muestra A (en Windows, la de PowerShell). Si a esa
  máquina le tocan más de 150, la línea va más de una vez (la tarjeta lo dice): en macOS un
  proceso de hinchada no pasa de 150.

Lo que hace falta:

- Las PC en la misma red, y A con **HTTPS en la red** (la dirección de la sala que se ve en
  Servicios): las otras entran al gateway de A por ahí, como cualquier invitado.
- El **puerto del coordinador** (7890; si está ocupado, otro: la tarjeta lo dice) abierto en el
  firewall de A. La primera vez, macOS o Windows preguntan si hinchada puede recibir conexiones:
  aceptalo.
- Cada generadora graba su video de prueba la primera vez (unos minutos, en su carpeta de
  hinchada: `grabado.hgr`); mientras tanto, los que le tocan entran tarde.
- La **clave de coordinación** va en el código y en la línea (`HINCHADA_CLAVE`): es de esa prueba
  sola (el coordinador rechaza a quien no la trae). La credencial de prueba de la sala no está en
  el código: el coordinador se la pasa a cada trabajador por su conexión (en la red local, sin
  cifrar; vence sola a los 90 s si nadie la renueva).
- El coordinador espera a todas las máquinas pedidas hasta 10 minutos; si no llegan, la prueba
  termina con error («¿no llegaron las otras máquinas?»).

El informe de A suma `probador.maquinas` (cuántas se pidieron y cuántas se sumaron) y
`generadoras` (la CPU de cada máquina generadora, promedio y pico, de las muestras del
coordinador). Si una generadora llega al 100 %, sus números son los de la generadora, no los de
la sala.

Desde la línea de comandos, lo mismo con `tools/carga/medir-sala.py --otras-maquinas K
[--solo-coordinar] --url https://IP_DE_A:PUERTO` (ver su `--help`).

## Qué hace con los de prueba

- **Entran directo**, sin lobby (si hay lugar; si no, esperan como cualquiera y nunca sacan a
  nadie). Las personas reales siguen pasando por el lobby.
- Quedan **marcados `prueba`**: el rótulo «prueba» en el lobby, en la lista de adentro y en
  la grilla de personas («nombre · prueba»).
- **Fuera del padrón, la votación y el quórum** (son invitados de la conferencia, nunca
  integrantes), fuera del acta (tiempo de palabra), de las novedades del asistente, de la
  atención automática del lobby y de las alarmas de calidad de red (su red va al informe).
- Si el cupo de la sala es menor que N, **se sube** al empezar y **vuelve** al terminar.

## La credencial de prueba

- La emite el gateway cuando la app abre la prueba (`POST /api/v1/prueba-sala`): 32
  caracteres aleatorios. Vale **90 s** y la app la renueva cada 30 s mientras dura la
  prueba: si el programa se cae, en menos de un minuto y medio el gateway la **revoca solo**,
  echa a los de prueba y apaga la entrada directa.
- No pasa por el tope de intentos de PIN por IP (el probador entra muchas veces desde la
  misma IP); un PIN equivocado sí.
- Al terminar se revoca (`DELETE` o `POST /api/v1/prueba-sala/cerrar`) y los tokens que
  se emitieron con ella dejan de servir.

## «Entrada directa para todos»

Un interruptor **visible**, solo durante la prueba, con advertencia y confirmación: deja
entrar a **cualquiera** con el enlace y el PIN sin pasar por el lobby (para probar con
personas reales además de las generadas). No toca la configuración del lobby: se apaga
**solo** al terminar la prueba, al vencer la credencial o al cerrar el programa.

## La guardia

El 2026-11-01 una prueba de carga agotó la memoria de la Mac y tiró el kernel. Por eso:

| Momento | Regla |
|---|---|
| Para arrancar | memoria disponible ≥ 6000 MB, carga ≤ 8, y que con N (≈ 80 MB cada uno con el probador local, la mitad con uno externo) la memoria quede arriba de 4500 MB. Sin poder medir la memoria, no arranca. |
| Durante (cada 1 s) | se **corta sola** si la memoria disponible baja de 3000 MB, si ba-app pasa de 1200 hilos o si la carga pasa de 40. |

La memoria disponible es la que el sistema da sin presión (macOS: libre + inactiva +
especulativa + purgable; Linux: `MemAvailable`; Windows: `ullAvailPhys`). En Windows no hay
carga (no se usa). Las reglas son puras (`src/app/sala/PruebaSalaReglas`, `test_prueba_sala_reglas`).

## El informe

- **En vivo** (la tarjeta y `prueba_sala_estado`): pedidos, entraron, con video, fallidos,
  cortes (se cayó y la sala le guardó el lugar), tiempo hasta tener video, la HD (p50, p95,
  máx.), la última muestra y los picos.
- **Final**: además la serie (de a 5 s), cada participante y por qué terminó. Se guarda en
  `Eventos/<evento>/Pruebas/prueba-sala-<fecha>.json` y una línea en la **lista de chequeo**
  del evento (ítem `prueba_sala`: hecho si todos tuvieron video, sin cortes y sin la guardia).
  Con hinchada, su propio informe (HTML y JSON) queda en `Pruebas/hinchada-<fecha>/`.
- El ancho de banda es el del gateway (lo que suben los participantes / lo que les baja);
  la CPU, la de los procesos de la app, del gateway y del probador de esta máquina (en % de la
  máquina), con el pico y el promedio.

## Si el programa se cierra en el medio

hinchada se corta al cerrar (es un proceso hijo). El gateway revoca la credencial sola al
vencer. Lo que queda (el cupo subido) lo deshace la app al volver a arrancar
(`Runtime/prueba-sala-en-curso.json`).

## MCP

| Herramienta | |
|---|---|
| `prueba_sala_arrancar` | `{participantes, minutos?, hinchada?, hablan?, decodificar? (nada\|muestra\|todo), muestra_pct?, otras_maquinas?, esta_genera?}` (con la guardia) |
| `prueba_sala_parar` | termina y limpia |
| `prueba_sala_entrada_directa` | `{on}` (confirmalo con el operador) |
| `prueba_sala_estado` | en vivo, acceso para el generador externo, `decodificar`, `reparto` (con el código), `generando` y la última |
| `prueba_sala_revisar` | `{participantes?, hinchada?, otras_maquinas?, esta_genera?}`: ¿la guardia dejaría arrancar? |
| `prueba_sala_generar` | `{on, codigo?}`: esta máquina genera para la prueba de otro equipo (el código BAGEN1. del otro) |
| `prueba_sala_informe` | el informe completo de la última |

## API del gateway (127.0.0.1, la usa la app)

| Método | Ruta | |
|---|---|---|
| GET | `/api/v1/prueba-sala` | estado (con la credencial) |
| POST | `/api/v1/prueba-sala` | `{duracionS, directa?}` abre/renueva · `{directa}` el interruptor |
| DELETE / POST | `/api/v1/prueba-sala` · `/api/v1/prueba-sala/cerrar` `{motivo}` | termina: revoca, echa a los de prueba, apaga la entrada directa |

Eventos a la app: `prueba-sala` (estado) y `prueba-sala-hd` (`{id, ms}`: llegó la HD de uno
de prueba). El modelo es `src/gateway/PruebaSala` (`test_prueba_sala`).

## hinchada en el programa

Viaja en `bin/64bit/hinchada/` si se la indica al configurar (`-DBA_HINCHADA_DIR=` o
`make deps-hinchada`, que copia el paquete de `../hinchada/dist/hinchada-<plataforma>`).
Sin ella, la prueba funciona con carga externa. Hace falta un hinchada con `--modo grabado` y
`--hablan` (el paquete de antes no los tiene: `make -C ../hinchada paquete` y otra vez
`make deps-hinchada`; el paso lo revisa). El video grabado (`grabado.hgr`, ~100 MB) es uno
solo para todas las configuraciones: `BroadcastAppliance Datos/Runtime/hinchada/`.

**El rótulo de la capa.** Cada capa del simulcast de hinchada lleva su definición en números
grandes en el centro («720p», «360p», «180p») y abajo el tamaño, los cuadros por segundo y,
en el video grabado, la tasa. Sirve para ver en la grilla qué capa llega a cada celda y si al
agrandar al que habla sube de verdad a la alta (con 200 invitados: las miniaturas en 180p y
el orador en 720p, [captura](../hardware/img/pc-windows-noche-sala-200-rotulo.jpg)). Viene prendido;
`--rotulo-capa no` lo apaga. Un `grabado.hgr` de antes, sin rótulo, se graba de nuevo solo
la primera vez.
