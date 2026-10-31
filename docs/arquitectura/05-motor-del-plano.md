# El motor es del plano de control (F3-03)

Cómo se migra un área de la interfaz para que no toque el motor. Hecho para escenas,
fuentes del editor, salidas, emisión, las pastillas de la raíz y las vistas de video
(`FrameService`); falta para Mezcla/audio, Imagen/video, Control/DSK, Asamblea, Sala,
Asistente y Escritorio.

## Quién es dueño de qué

- **El dueño del motor es el hilo «control»** (`arranque/PlanoControl`). `AppRuntime`,
  `BroadcastEngine` y la configuración (`cfg_`) son suyos: corre el reloj del motor
  (`AppRuntime::ticMotor`), las órdenes que le encarga la interfaz (la agenda,
  `PlanoControl::encolarConTurno`), las herramientas MCP con `Hilo::Control` y arma la
  foto (`AppRuntime::publicarFoto`). Los conmutadores de escenas viven en ese hilo
  (`VentanillaMotor::adoptar`). Se eligió el mismo hilo del control y no uno «aire»
  aparte: el MCP de aire ya corría ahí, y con un solo dueño hay una sola cola y un
  orden. Costo: algo largo en el motor (rearmar por la GPU, aplicar video) frena también
  el canal del launcher y el transporte del MCP; si molesta, el dueño se muda a otro
  hilo cambiando solo el `encolar` de la ventanilla y la agenda.
- **La interfaz** tiene su reloj solo para su latido (`AppRuntime::latirInterfaz`): el
  supervisor sigue sabiendo si se trabó.
- Al arrancar, `start()` corre en el hilo principal (GStreamer y el contexto GL se crean
  ahí, como pide macOS); la primera foto se publica ahí y después
  `PlanoControl::enControlYEsperar` muda al dueño. Al cerrar, `devolverElMotor` (GuiMode)
  para el plano y el hilo principal vuelve a ser dueño para el cierre ordenado.

## La API para los view models: `emision/VentanillaMotor`

1. **Pedir**: `ventanilla.pedir(this, "nombre_corto", [&rt]{ … return x; }, [this](x){ … })`.
   La orden corre en el dueño, en orden y con el turno; la respuesta vuelve encolada al
   hilo de `this` (si se fue, no llega). Nunca `BlockingQueuedConnection`, nunca esperas.
   Antes de la respuesta el dueño publica la foto: lo que la orden cambió ya está en ella.
   Sin respuesta: `ventanilla.pedir("nombre", [&rt]{ … })`.
2. **Leer**: `ventanilla.foto()` (`emision/FotoMotor`), un valor que solo cambia en el
   hilo de la interfaz, al llegar `cambio(partes)`. Cada view model escucha las partes
   que muestra y emite sus `NOTIFY` (MVC: el modelo avisa; nada de sondear con
   `Q_INVOKABLE` desde QML).

Patrones que salieron al migrar:

- Lo que QML esperaba de vuelta (un `QString` de error) ahora llega como señal
  (`notify`, o una propia como `destinoGuardado`); el `Q_INVOKABLE` devuelve "".
- Lo que se pinta enseguida después de pedir (soltar una capa, un toggle) va como
  **pendiente** en el modelo hasta que vuelve la respuesta (`SourcesModel::pendientes_`):
  la foto que llega antes de la orden no lo pisa.
- Lo que lee el disco (`discoGrabacion`) no va en la foto de cada vuelta: se pide
  (`EmisionViewModel::pedirDisco`) y vuelve en una propiedad.
- Lo que se mueve siempre (loudness, salud, medios) va en la parte `Medidas`: se lee
  cuando se lo necesita (un medidor puede sondear a su ritmo).
- Los avisos del runtime (`AppRuntime::observar`) llegan en el dueño: quien escucha y
  es de la interfaz se los encarga a su bucle.

- La configuración guardada entera (app.json, gateway.json, launcher.json) está en la parte
  `Configuracion` como copias que no cambian (`FotoMotor::Guardada`, se comparan por
  puntero): un formulario la lee sin turno (`SettingsModel`, la voz, el asistente) y
  aplicar es UNA orden (`SettingsModel::aplicarEn`); lo de la interfaz (el modo en vivo, el
  cupo y la sala en caliente) se decide antes y después, y el resumen vuelve con `applied`.
- Lo que no depende del estado del motor (los codificadores, los motores de
  superresolución, con qué dibuja) es estático y se lee desde cualquier hilo; lo seguro
  entre hilos que ya existía (`haySalida`, el turno) tiene su acceso en `AppRuntime` sin
  contar como entrada.

Para sumar algo a la foto: un campo en `FotoMotor` (con su parte), lo arma
`AppRuntime::armarFoto` (o un modelo del dueño con `aportarALaFoto`), y quien lo cambia
ensucia la parte (`fotoSucia`) o avisa (`AppRuntime::avisar`).

## La red de seguridad: el turno y las llamadas directas

Lo que todavía entra directo (`rt.config()`, `rt.engine()`, los métodos de `AppRuntime`
y `BroadcastEngine` desde la interfaz) toma el turno (`emision/TurnoMotor`) **por
evento**: la primera vez que entra en un evento lo toma y lo suelta al terminar el evento
(`AplicacionInterfaz::notify`) o antes de dormir (`aboutToBlock`). Así lo leído por
referencia de `cfg_` no cambia debajo. El dueño nunca espera: la agenda reintenta y la
vuelta del motor se saltea mientras la interfaz lo tiene.

Cada entrada directa se cuenta por lugar: `ba_motor_directas_total{lugar}` en
`/metrics`, `TurnoMotor::directas()` y una línea «la interfaz entró directo al motor:
<lugar> (<archivo>:<línea>)» en `app.log` la primera vez de cada lugar. Esa lista es lo
que falta migrar.

## La línea de tiempo (asamblea, sala y línea)

La corrida de la línea (`EventoViewModel`, sus `EjecutorLinea`) se quedó en la interfaz a
propósito: casi todo lo que hace una pieza es de la interfaz (los gráficos, el carrusel,
el teleprompter, los oradores de la sala, los vínculos, el DSK, las PTZ, la bitácora), y
mudarla al dueño lo haría esperar a la interfaz para eso. Lo que toca el aire (cortar y
armar en el conmutador de cada salida, la música, el video, el audio) se pide por la
ventanilla y lo que lee del motor sale de la foto (parte `Sala`). Con la interfaz trabada
la línea no avanza: lo pendiente sale al volver, con la hora de pared (`Corrida::tick`), y
lo que ya está al aire sigue saliendo. Lo «a mano» se sabe por las escenas que pidió la
línea a cada salida (el aviso del conmutador llega encolado, después de la orden). Para el
MCP, la copia de `linea_estado`/`linea_ver` se siembra al registrarlas y la línea la pone
al día cuando cambia (`CopiaDeLaInterfaz::copiarYa`).

