# Capacidad: ¿cuántos invitados aguanta?

Página **Pruebas → Capacidad** (antes en Configuración → Sistema, que ahora lleva ahí) o las herramientas MCP `capacidad_*`: las
dos usan el mismo modelo (`src/app/ui/CapacidadViewModel`). Es para el operador: dice
cuántos invitados aguanta **esta máquina**, **otro BroadcastAppliance** y **la conexión a
internet**, con un número y lo que limita. No muestra hilos ni métricas internas (eso es
`tools/carga/medir-sala.py`, la herramienta de laboratorio).

Reúsa lo que ya había: [hinchada](PRUEBA-SALA.md#hinchada-en-el-programa) (los invitados de
prueba reales por WebRTC), la credencial de prueba de la [prueba de sala](PRUEBA-SALA.md) y
la medición del [enlace](ENLACE.md) con su cuenta (`sala/EnlaceNecesita`).

## Este equipo

«¿Cuántos invitados aguanta esta máquina?» **Probar este equipo…** (con confirmación) mete
invitados de prueba con hinchada **en esta misma máquina** contra el propio gateway, por
escalones: **10, 20, 40, 60, 80, 100, 120, 160, 200…** hasta el tope elegido (de fábrica 80)
o hasta que se degrade.

**Decodificar lo que vuelve** (vale para este equipo y para probar otro): «una muestra» (de
fábrica, 5 % y al menos 3 por escalón), «nada (solo envía)» o «todo»; por qué la muestra, en
[PRUEBA-SALA.md](PRUEBA-SALA.md#decodificar-lo-que-vuelve). El resultado lo guarda
(`decodificar`).

Cada escalón dura **2 minutos como mucho**: entran los nuevos (los de antes siguen adentro;
de a 300 ms, menos si son muchos, para que entren a tiempo), 15 s para que se acomode la
grilla y 30 s de **régimen**, que es lo que se juzga. Un escalón está **degradado** si en
su régimen:

| Qué | Criterio |
|---|---|
| CPU | la mediana pasa del **85 %** de la máquina (sin la parte de hinchada) |
| Memoria | la disponible baja de **4500 MB** |
| Grilla | el mosaico de la grilla de personas baja de **25 cuadros por segundo** (se mide 20 s en cada escalón, como `grilla_personas_medir`) |
| Audio | la edad del audio de los invitados (mediana) pasa de **50 ms** (`estudio_latencia`) |
| Entrada | menos del **90 %** de los pedidos está adentro con video |
| Interfaz | el latido de la interfaz se atrasa más de **500 ms** (p90) o una vez más de 2 s |

**hinchada en la misma máquina** también usa CPU: sus procesos se miden aparte (el
muestreador de recursos) y su parte **se descuenta** de la CPU de la máquina para el juicio.
El resultado lo dice («usó ~X % de la CPU: ya está descontado»).

**La guardia** mira cada segundo y corta YA (antes de que la máquina se ahogue): memoria
disponible menor a 3000 MB, la CPU entera al 97 % o más durante 10 s seguidos, o la interfaz
parada 5 s. Además, para arrancar vale la guardia de la prueba de sala (memoria y carga).

**El resultado**: «Aguanta **N** invitados con margen (**M** como máximo antes de
degradarse). Lo que limita es la CPU / la memoria / la grilla / el audio / la entrada / la
interfaz.» M es el último escalón sano. N es M si ese escalón anduvo holgado (CPU ≤ 68 % y
memoria bien); si no, el 80 % de M (redondeado a 5). Si la rampa llegó al tope sin
degradarse dice «al menos» y, con la tendencia de la CPU (una recta por los escalones),
hasta dónde llegaría. Un gráfico por medida (CPU, memoria, grilla, audio) muestra cada
escalón con su umbral.

### No ensuciar el evento

- Los invitados de prueba entran con la **credencial de prueba** (vence sola en 90 s y se
  renueva mientras dura): directo, sin lobby, marcados «prueba», **fuera del padrón, la
  votación, el quórum y el acta** (lo mismo que la prueba de sala).
- **No se escribe nada en el evento abierto**: el resultado va al historial de la máquina.
  Lo único que se toca es el cupo de la sala si no alcanza: se sube al empezar y vuelve al
  terminar (si el programa se cae en el medio, al volver se devuelve:
  `Runtime/capacidad-en-curso.json`).
- **No arranca** con algo al aire (transmisión, grabación, señal limpia), con invitados de
  verdad conectados, con la prueba de sala corriendo o midiendo el enlace (el aviso de la
  pestaña dice por qué). Y la prueba de sala no arranca mientras corre esta.

## Un sistema remoto

Probar **otro** BroadcastAppliance desde esta máquina (por ejemplo, la PC del evento desde una
notebook): los invitados salen de acá y el otro informa su CPU, memoria, grilla y audio.

1. En el otro: **Que otro equipo pruebe este → Recibir una prueba…** (hasta cuántos y
   cuántos minutos, como mucho 60). Abre la credencial de prueba, sube el cupo y muestra
   un **código** (oculto; **Copiar el código**).
2. En este: pegar el código en **Un sistema remoto**, elegir **Rampa** o **Prueba de sala** (N
   invitados de una vez durante 1 a 15 minutos, con el mismo juicio) y **Probar el otro
   equipo…**. La rampa es la misma, con el tope recortado al cupo que abrió el otro.

Los dos ven en vivo los **gráficos de CPU y red de los dos lados** (la máquina medida: la
máquina, la app, el gateway y su red; la que genera: la máquina, hinchada y su red). La que
genera manda lo suyo (CPU, red, lo que decodifica y su **ficha**) con cada pedido de la medida
(cabecera `X-BA-Generador`, con la credencial); el otro lo muestra («Genera mac-b: CPU…») y,
al dejar de recibir, guarda un informe **recibida** en su historial. Varias PC pueden generar
contra la misma: cada una pega el mismo código. Pasos para el usuario: [PRUEBAS.md](PRUEBAS.md#probar-entre-dos-pc).

El código (`BACAP1.` + base64url) lleva la dirección del otro gateway, la credencial y la
**huella del certificado** de su HTTPS (es autofirmado: la conexión se fija a ESE
certificado, `HttpsClient` con `huellaSha256`). Si el otro tiene pinggy listo hay un
segundo código por internet (certificado de verdad, sin huella).

Seguridad:

- **Nada de tokens en la URL ni en la línea de comandos**: la medida se pide con
  `GET /api/prueba-sala/medida` y la credencial en la cabecera `X-BA-Prueba`; a hinchada
  le llega por el entorno (`HINCHADA_PIN`). La API de control y el MCP del otro **no** se
  exponen.
- La credencial vence sola (90 s sin renovar) y el otro la revoca al terminar o al
  cancelar; los intentos fallidos cuentan en el tope de intentos por IP.
- La IP que entró con la credencial no queda frenada por `room.maxConexionesPorIp` mientras
  dura la prueba (todos los de prueba salen de una sola máquina).
- La guardia del otro corta si se ahoga (echa a los de prueba). Si el otro deja de
  contestar 15 s, la prueba de acá se corta.
- Acá, la máquina que genera: si su CPU pasa del 95 % el resultado se marca como un mínimo
  («lo que limita es la máquina que genera los invitados»).

## Conexión a internet

**Medir la conexión** mide los caminos de los invitados y lo traduce a «con esta conexión
entran **N** invitados con margen» con la cuenta del [enlace](ENLACE.md#cuánto-necesita-el-evento)
(como mucho 4 en alta, el resto en baja; el cupo de la sala como perfil):

- **pinggy** (el camino real de los invitados: pinggy → el TURN de la conferencia): **ida y
  vuelta**. Esta máquina le pide relleno a su propia dirección pública
  (`GET /api/capacidad/relleno` con un secreto de un minuto en la cabecera `X-BA-Medir`),
  así cada byte cruza la subida y la bajada: lo medido es **lo menos** de las dos (casi
  siempre la subida, que es lo que manda el retorno a cada invitado) y se toma como subida
  y bajada para la cuenta. La demora es la de `/healthz` en reposo y con carga; la pérdida,
  lo que no contestó. Son 8 s con 4 conexiones.
- **WireGuard** (si hay túnel): la medición del enlace de siempre (`EnlaceMedidor`, con su
  eco en el VPS). Con invitados conectados o algo al aire, no se mide (lo dice la nota).

Si pinggy está **apagado**, «Medir la conexión» lo dice y no lo prende. **Prender pinggy y
medir…** (con confirmación) lo prende, mide y **lo apaga al terminar**; prenderlo reinicia
la conferencia, así que pide lo mismo que la rampa (nada al aire, nadie conectado). Si el
programa se cierra en el medio, al volver se apaga.

## El historial

Cada prueba queda en el **historial de pruebas** de la máquina, `Pruebas/<fecha>-<máquina>-<tipo>.json`
en la carpeta de datos (`local`, `remoto`, `recibida` o `internet`; las de antes siguen en
`Capacidad/`), con los escalones, lo medido, los criterios, la frase, lo que se decodificó, la
serie (de a 5 s) y la ficha de cada máquina ([PRUEBAS.md](PRUEBAS.md#el-historial)). Los
informes de hinchada de cada escalón quedan en `Pruebas/hinchada-<fecha>/`.

- La pestaña muestra la lista (fecha, tipo, máquina, resultado) y al abrir carga el último.
- **Exportar el último…** (JSON: la capacidad y la conexión) y **Exportar el historial
  (CSV)…**.
- La **revisión previa del evento** (la del guion) avisa si la última capacidad de esta
  máquina o la última conexión medida dan menos invitados que el cupo del evento, y si la
  máquina nunca se midió.

## MCP

| Herramienta | |
|---|---|
| `capacidad_arrancar` | `{prueba: este_equipo\|remoto\|internet, tope?, codigo?, minutos? (remoto: prueba de sala), prender_pinggy?, decodificar?, muestra_pct?, confirmar}` |
| `capacidad_recibir` | `{on, cupo?, minutos?}`: dejarse probar desde otro equipo |
| `capacidad_estado` | estado, riesgo, progreso, en vivo, escalones, recibir (con el código), `generadores` (recibiendo), `serie` (los últimos 2 min), pinggy y los últimos |
| `capacidad_cancelar` | corta y limpia |
| `capacidad_ultimo` | `{que: capacidad\|remoto\|internet\|historial}` |

## API del gateway

| Método | Ruta | |
|---|---|---|
| POST | `/api/v1/prueba-sala` | `{medida}`: la de esta máquina para el probador remoto (127.0.0.1, la app) → `{ok, generadores}` (lo que mandan las que generan) |
| POST / DELETE | `/api/v1/capacidad/red` | `{duracionS}` → `{secreto}` para medir por el túnel (≤ 3 min) · lo olvida (127.0.0.1) |
| GET | `/api/prueba-sala/medida` | pública, cabecera `X-BA-Prueba: <credencial>` (y, de la que genera, `X-BA-Generador: <JSON>`) → `{ok, medida, edadMs, participantes}` |
| GET | `/api/capacidad/relleno?kb=N` | pública, cabecera `X-BA-Medir: <secreto>` → N KB (≤ 4096) que no se comprimen |

## Pruebas

`test_capacidad_reglas` (escalones y tiempos, el juicio con datos sintéticos, la guardia, el
resultado y la tendencia, el código, lo que se lee de la latencia y la grilla, el
historial), `test_capacidad_vm` (el modelo con una sala, un generador y una red de mentira:
lo que impide arrancar, la rampa hasta 40, la degradación, la guardia, cancelar, remoto,
recibir, internet) y `test_prueba_sala` (la medida y las IP de prueba del gateway).
