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

## Por pasos

El orden cambió al hacerlo: S3 va antes que S2, porque es la garantía (con la
interfaz encolando, ningún bloqueo del motor la alcanza); S2 hace que el motor
no se atrase.

| | qué | estado |
|---|---|---|
| S0 | Vigía del candado y de los tramos del bucle (`Candado`, `Tramo`). Composiciones de escena desarmadas aparte (`desarmarAparte`). | hecho |
| S1 | **Foto**: el bucle publica `estado()` en `Motor::instantanea()`; `estadoMotor()`, la Matriz, `medio` y `tbarHacia` la leen sin candado. | hecho |
| S3 | **Cola de órdenes**: `Motor::orden` encola lo que escribe (entradas, escenas, aire, T-bar, DSK, vistas, retornos, escucha, tomas) y vuelve; los errores llegan por `alFallar` → aviso en la interfaz. Las tomas de audio nacen «en camino» (`tomaViva`). `verAvisar(nullptr)` es inmediato. Los cuadros de la app van a un **buzón** y los entrega el hilo de cuadros. `propiedadesEntrada` por un índice propio y sin esperar a una entrada ocupada. `esperarCola()` para los tests. | hecho (falta `iniciar`/`detener`, que corren al arrancar y al cerrar) |
| S2 | **El bucle sin bloqueos**: cada `Entrada` tiene su fila de trabajos en un trabajador (`trabajar`, en orden y con su candado): armarla al crearla, rearmarla (sin datos, error, EOS, reloj perdido, dispositivo que se fue o volvió, entrelazada, el rack), la placa en reposo, ajustes nuevos, controles, propiedades y los medios (pausa, salto). Su `tic` toma el candado solo si está libre y la saltea si está ocupada; su `estado` y los del medio, ocupada, dan lo último que se supo. `origen_red` desarma en un hilo suelto y, si el anterior no terminó, posterga el rearmado en vez de esperar. Escenas y entradas que se sacan se desarman aparte. | hecho |
| S4 | **Salidas y capturas**: un **hilo de salidas** en `BroadcastEngine` es dueño de transmisión, extras, grabación (y copia), repetición, grabaciones por entrada y señal limpia: las arranca, las para (esperar el cierre del archivo) y las atiende (`tic`: una transmisión caída se rearma con espera creciente, eligiendo de la misma lista de codificadores: el que falló se vuelve a elegir). La interfaz encola (`enSalidas`), contesta «ya está» con lo pedido y lee la foto que publica ese hilo cada 100 ms. Las capturas van por `capturarAparte` (el cuadro se espera en otro hilo y la respuesta vuelve al de la interfaz): herramientas MCP de imágenes, la foto del integrante, los scopes, la miniatura de las posiciones PTZ y los cuadros para entrenar la superresolución. | hecho |
| S5 | **Que no vuelva**: `Candado` cuenta cada vez que la interfaz toma el candado del motor fuera de `iniciar`/`detener` (`tomasDeLaInterfaz`) y, con `--test`, lo deja como ERROR en el registro con la función (`vigiaEstricto`). `test_motor_sin_esperas` marca su hilo como el de la interfaz, llama a toda la API del motor —con una cámara de red que no contesta— y exige cero tomas y ninguna llamada de más de 50 ms. | hecho |
| — | **El latido del arranque**: el primer arranque después de compilar tarda más de 20 s (GStreamer relee sus plugins) y el launcher lo mataba por «colgado». Ahora late desde su hilo mientras arranca (tope: 2 minutos). Arrancar y apagar el motor siguen en el hilo de la interfaz, detrás del cartel de carga. | hecho |

Cada paso se prueba con la app en `--test` (escenas a las pantallas, la Axis
apagada, grabar y parar) mirando que el vigía no avise.
