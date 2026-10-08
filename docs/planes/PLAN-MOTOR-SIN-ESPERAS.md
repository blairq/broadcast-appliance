# Plan: la interfaz nunca espera al motor

## El problema de diseño

La interfaz (el hilo principal de Qt, donde también corre el MCP) **llama al
motor directo y de forma sincrónica**, y el motor hace adentro de **un único
candado** (`Motor::mu_`, 47 funciones públicas) cambios de estado de GStreamer
que pueden tardar lo que quieran: una cámara RTSP que no contesta, un mezclador
de GL terminando un cuadro, una red neuronal en medio de una inferencia. El
bucle del motor (cada 20 ms) también tiene ese candado mientras rearma y pone
en reposo entradas.

Y en macOS GL necesita el hilo principal para soltar sus buffers: si el hilo
principal está esperando un cambio de estado, se esperan los dos para siempre.

Lo que pasó el 2026-10-07 probando las escenas en los televisores:

- **37 s trabada** (volvió sola): el bucle tuvo el candado rearmando y poniendo
  en reposo las entradas de la Axis (inalcanzable), y la interfaz lo esperó.
- **Colgada para siempre** (la mató el launcher): la interfaz desarmó una
  composición de GL → la traba de macOS.
- Los avisos «el bucle del motor tuvo el candado 1004 / 2458 ms» y el arranque
  que el launcher relanzó por «colgado» son lo mismo en chico.

Agravantes que encontró el inventario:

- Los vúmetros leen `estado()` cada 100 ms, que toma `mu_` y el candado de cada
  entrada: cualquier espera larga del candado congela toda la interfaz.
- `origen_red::tic()` desarma un RTSP que no contesta **con su propio
  candado**, y `Entrada::armar` (adentro de `mu_`) espera ese candado: cadena.
- `capturarVista` duerme hasta 3 s en el hilo que llama; parar una grabación
  espera el EOS hasta 5 s (dos grabaciones, 10 s); transmisiones, retornos y
  escuchas se arman y desarman en el hilo que llama.

## Las reglas del diseño nuevo

1. **El hilo de la interfaz no espera nunca al motor.** Ni el candado ni un
   cambio de estado ni un `sleep`. Lo que escribe se encola; la respuesta (el
   error, si lo hay) vuelve como evento al bucle de Qt.
2. **Las lecturas no toman el candado.** El bucle del motor publica una
   instantánea (estado, niveles, entradas) y la interfaz lee esa.
3. **El candado del motor protege datos, nunca un cambio de estado.** Armar,
   parar, rearmar y desarmar pipelines se hace en hilos de trabajo, sin
   candado; el motor actualiza su estado cuando terminan («rearmando…»).
4. **Se vigila.** `motor/Candado.hpp` deja en el registro cada vez que la
   interfaz espera el candado (> 50 ms) o alguien lo tiene (> 200 ms), con el
   lugar exacto; y cada tramo lento del bucle con su nombre. Con `--test`, una
   espera de la interfaz es un error en el registro.

## Por pasos (de más impacto y menos riesgo a menos)

| | qué | estado |
|---|---|---|
| S0 | Vigía del candado y de los tramos del bucle (`Candado`, `Tramo`). Composiciones de escena desarmadas aparte (`desarmarAparte`). | hecho |
| S1 | **Instantánea**: el bucle publica `estado()` (y lo que leen los vúmetros) en un `shared_ptr` atómico; `estadoMotor()` la lee sin candado. | |
| S2 | **El bucle sin bloqueos**: los cambios de estado del `tic` de las entradas (reposo, rearmar, reintentos, sin datos, errores) y de `actualizarUso` pasan a un trabajador de estados; `origen_red` desarma fuera de su candado. | |
| S3 | **Cola de órdenes**: `Motor::encolar` y versiones asincrónicas de las escrituras que bloquean (`agregarEntrada`, `actualizarEntrada`, `quitarEntrada`, `definirEscena`, `alAire`, `armar`, `dsk`, `verPedir`/`verSoltar`, `escucharEntrada`, `retorno`, `escuchar`), con la respuesta por evento de Qt. `BroadcastEngine` y sus llamadores pasan a esa forma (el MCP ya es por continuación: `Tool::run(args, done)`). | |
| S4 | **Salidas y capturas**: parar grabación/transmisión/limpio en el trabajador con estado «parando…»; `capturarVista` por continuación en vez de dormir. | |
| S5 | **Que no vuelva**: con `--test`, una espera de la interfaz en el candado o un `gst_element_set_state` en el hilo de la interfaz queda como error; un test que lo ejercita. | |

Cada paso se prueba con la app en `--test` (escenas a las pantallas, la Axis
apagada, grabar y parar) mirando que el vigía no avise.
