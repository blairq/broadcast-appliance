## Que es
Un **evento** es el guion de lo que va a pasar: **bloques** (Apertura, Palabras del intendente, Cierre) y dentro de cada uno los **cues** concretos. El evento tiene TODO lo suyo (escenas, mezclador, medios, asamblea, web): la carpeta del evento entera se puede llevar a otra máquina. Los archivos de los cues de música y video son los **medios del evento** (la página Medios, `media_list`); los discursos van en `Discursos/` del guion.

## Los tipos de cue y que necesita cada uno en `datos`
| Tipo | `datos` | ¿Se ejecuta hoy? |
|---|---|---|
| `escena` | `{escena, transicion, ms}` — entra por el PVW con TAKE; `transicion`/`ms` opcionales (si no, la de entrada de la escena) | si |
| `orador` | `{participante}` + `duracionSeg > 0` | si |
| `musica` | `{archivo, accion, repetir}` — un archivo de los medios del evento (`media_list`) | si |
| `teleprompter` | `{discurso, arrancar}` — texto en `Discursos/` | si |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | si |
| `video` | `{archivo, alTerminar}` | todavia no |
| `pantalla` | `{pantalla, contenido}` | todavia no |
| `pausa` | espera lo que diga `duracionSeg` y sigue sola | si |
| `espera` | frena hasta que el operador confirme | si |
| `nota` | no hace nada: es una indicacion | si |

Los que todavia no se ejecutan igual se pueden dejar escritos: cuentan para los tiempos y le dicen al operador que hacer. Cuando les toca el turno, el sistema lo anota en vez de fallar callado.

## Tiempos
De la `duracionSeg` de cada cue sale la del bloque y la del evento; con `inicio` (HH:MM) sale la hora prevista de cada cosa, y con `fecha` (YYYY-MM-DD) mas `huso` (nombre IANA) sale `cuando`, el arranque como instante absoluto para el que esta en otro huso. Mientras corre, lo que importa es el **desvio**: `desvioSeg` positivo = vamos tarde. Lo pausado no cuenta como atraso.

## Antes del evento
- `evento_revisar`: cues incompletos, escenas que no existen, archivos que faltan o estan vacios.
- `evento_ensayo_rapido`: recorre el guion ENTERO de una, sin tocar nada, y dice de cada cue si se podria ejecutar. Es lo que conviene correr despues de armar un guion largo.
- El interruptor de **ensayo** (`evento_correr {"ensayo": true}`) recorre el guion en tiempo real sin salir al aire.

## Mientras corre
`evento_correr` con `arrancar`, `siguiente`, `atras`, `ir`, `pausar`, `reanudar`, `hold`, `parar`. **Sale al aire** salvo en ensayo, asi que lo decide el operador.

Un cue con `automatico: true` pasa **solo** al siguiente cuando se cumple su `duracionSeg` (sin duracion, en el acto: sirve para encadenar escena + zocalo); una `pausa` con duracion, tambien. Las `espera` nunca, y el ultimo cue tampoco. Los ultimos 5 s antes de que entre solo el paso siguiente se avisan (`reloj.entraSoloEnSeg`). `hold` frena ese avance del paso al aire (la cuenta sigue y el atraso corre: no es una pausa); otro `hold` lo suelta, o `{"accion": "hold", "hold": false}`.

Por debajo, el guion es una **línea de tiempo** (ver abajo): cada cue de escena, orador, video, pausa o espera abre un **paso**, y los cues que lo siguen son **piezas** de ese paso. `siguiente` dispara la pieza siguiente del paso si todavía no salió y, si no queda ninguna, hace entrar el paso siguiente. Un gráfico que «se va al avanzar» se va cuando entra el paso siguiente.

El cue de escena que sigue queda **armado en el PVW** de antemano, asi el operador ve lo que va a entrar y el TAKE es inmediato.

Correr el guion NO es estar en vivo: en vivo es que salga algo de verdad (transmisión o grabación), y lo dice `en_vivo` en `status`. En vivo, la app le avisa al operador antes de cualquier accion de riesgo y `settings_set` no aplica lo que reinicia la conferencia o el motor (sacaria a todos los invitados o cortaria la salida en el peor momento). Con el guion corriendo para probar, sin transmitir, no se bloquea nada.

## Teleprompter
El cue `teleprompter` carga un texto de `Discursos/` y lo deja LISTO pero parado. Todo lo demas (escribir los discursos, mandarlos, manejar el texto al aire) es su propia categoria: `help {"topic": "prompter"}`.
## La cara del evento en la web (tema)
Cada evento tiene su **tema**: acento, fondo, letra, logo y nombre, en **todas** las páginas web del sistema: la del evento, la de invitados, la sesión remota (donde votan los integrantes), las consultas del público, el tótem de registro y las pantallas de la sala. Las consolas de los operadores (control, botonera, presidencia, rótulos, corrección) toman lo que diga el `alcance`: por omisión, solo el acento y el logo. Nunca cambian los colores con significado: tally, al aire y armado, vúmetros, sí y no de la votación.

- `evento_tema {"accion": "ver"}`: el tema, los colores que van a salir (`resuelto`) y los `avisos` de contraste. Si hay avisos, decíselo al operador y proponé otro color.
- `evento_tema {"accion": "cambiar", "colores": {"acento": "#00a86b"}, "marca": {"nombre": "Foro 2026"}}`: lo que no se pone se calcula (el texto encima del acento sale del contraste).
- `evento_tema {"accion": "logo", "archivo": "<ruta>", "cual": "logo"}` (SVG, PNG o WebP, hasta 2 MB; `logoClaro` para fondos oscuros, `favicon` para la pestaña) y `{"accion": "fuente", "archivo": "<ruta>", "familia": "…"}` (WOFF2, WOFF, TTF u OTF, hasta 4 MB). Sin internet: nada de Google Fonts, la letra viaja con el evento.
- `alcance`: `{"publico": "todo", "control": "acento"}` (cada uno nada, acento o todo).
- Un sitio generado antes del tema: `evento_tema {"accion": "migrar_sitio"}` (respeta lo cambiado a mano en `estilo.css`).

Se guarda en la carpeta del evento (`tema.json` y `Tema/`). Sin tema propio, el acento y el fondo salen de los del sitio (`evento_web sitio {color, fondo}`). En la app: Servicios → Aspecto de las páginas web → Tema del evento.

Fuera de la web también es el valor por omisión: los **gráficos** con un color de acento (zócalos, placas, tickers) salen con el del evento si no se les pasa otro, y los **QR** nuevos toman sus colores (siempre oscuro sobre claro, para que se lean) y su logo al centro. Con un tema claro, las páginas del público se dan vuelta enteras (resaltes, campos y sombras incluidos).

## La línea de tiempo (`linea_*`)
El plan del evento es su **línea de tiempo**; el guion de cues es una manera de leerla y editarla (lo que no tocás en los cues queda exacto en la línea). La línea sabe más que los cues: **pasos** que entran con GO o solos (`tras` otro, `con` otro, a una `hora` fija con política avisar/esperar/cortar, `ancla`dos a otra salida), **piezas** con su tiempo dentro del paso (`{ms}`, `{antesDelFin}`, `{manual:true}`) y su `vida` (paso, bloque, hasta que se saque, evento), un **estante** de adlibs y pasos para insertar, y el pronóstico con back time y over/under.

- **Mirar**: `linea_ver` (compacto; `nivel`: bloques, pasos o piezas; `desde`/`hasta` "HH:MM") y `linea_estado` (al aire, próximo, HOLD, over/under, próximo hard start). `linea_revisar` dice lo que está mal armado.
- **Cambiar**: `linea_editar {ops, probar}`: un lote que entra entero o nada. **Probá primero** (`probar: true`), mostrá el diff y el pronóstico, y aplicalo cuando la persona confirme. Cada cambio aplicado deja la línea anterior en `Respaldos/linea-<n>.json`; `linea_deshacer` vuelve. En vivo solo se toca lo que viene.
- **Llegar a horario**: `linea_ajustar {hasta: "21:00"}` (sin `hasta`, el fin planeado) es el solucionador del back-timing: trae **opciones ya probadas** (cada una con su `resultado`: cómo queda el fin) contra el fin y contra cada hard start que viene desfasado: **recortar** (los pasos `recortable` —hasta la mitad— o `recortable:N` —hasta el N %—, de la `prioridad:1` a la `prioridad:9`; sin etiqueta, 5), **absorber** (un solo paso se hace cargo de toda la diferencia), **saltear** los `opcional`, **mover la hora** de un hard start, y si sobra, **relleno** o **estirar**. Un paso `fijo` no se toca; a uno abierto (un orador) se le acorta lo previsto: hay que avisarle. Nunca aplica: mostrá las opciones y aplicá la que la persona elija (`linea_editar` con sus `ops`).
- **El productor**: mientras la línea corre, si una salida se pasa de un minuto (y después cada minuto más) la conducción avisa con la mejor opción («Vamos +3:20; si se hace esto llegamos: …») y la deja a la vista para que una persona la aplique o la descarte. No ejecuta nada solo.
- **Armar desde plantillas**: `linea_desde_plantilla` sin nada lista las plantillas (las del evento y las de fábrica: `orador`, `punto`, `votacion`) con sus campos. Con `plantilla` y `datos` (una fila por orador: `nombre`, `cargo`, `escena`, `participante`, `tiempoSeg`) arma un paso por fila; con `origen: "orden-del-dia"`, un bloque con un paso por punto que queda atado al orden del día (si cambia, se re-expande sin tocar lo emitido); con `escenario` (el id de `evento_escenarios`), la línea entera de la plantilla de esa guía. Siempre prueba primero; con `probar: false` aplica.
- **En vivo**: `linea_go`, `linea_hold`, `linea_siguiente`/`linea_anterior` (mueven el próximo sin tocar el aire), `linea_saltar`, `linea_ir {paso}` (entra con el estado que corresponde: repone música y gráficos de vida larga), `linea_repetir`, `linea_insertar {item}` (un paso del estante), `linea_adlib {pieza}`. Las que cambian el aire las confirma la persona.
- **Lo que pasó**: `linea_recorrido` (también en `Eventos/<evento>/recorrido.jsonl`).
- **Cámaras**: `linea_camaras` dice, para cada cámara, si está al aire y qué planos vienen con ella y cuándo (de todas las salidas), y qué PTZ le toca.
- **Lo que pasa afuera**: `linea_pulso {clave}` manda un pulso («tecla:F5», «botonera:<id>», «web:<botón>») para los disparadores y los pasos «cuando» que lo esperan (`linea_ver` trae `pulsos`; las teclas, la botonera y la web los mandan solas). Si alguien cambió la escena a mano (`aMano` en `linea_estado`), `linea_reenganchar` vuelve a la del paso. Una PTZ que espera porque su cámara está al aire (`ptzPendientes`) se decide con `linea_ptz {pieza, decision: mover|esperar|saltar}`.
- **Disparadores**: se agregan con `linea_editar` (`disparador_agregar`); `activo` + `en` dicen cuándo valen (todo el evento; mientras un paso o un bloque está al aire; desde que entró). Las plantillas del evento, con `plantilla_agregar`.
- **Escaletas de afuera**: `linea_exportar {formato: csv|mos}` y `linea_importar {texto|archivo}` (CSV de una planilla o de Rundown Studio, o MOS de un sistema de redacción): lo importado entra como bloques al final, siempre probando primero.
- **Grabar los tiempos**: `linea_grabar {accion: grabar}` deja el próximo ensayo con todo a GO; al terminar, `linea_grabar {accion: ver}` dice cuánto duró cada paso y, si la persona confirma, `aplicar` lo deja como duración prevista.

## La línea en otros aparatos
Además de la conducción en la app, la línea se maneja y se mira desde la red local (aparatos del control web: se emparejan con el QR de **Servicios → Control** y una persona les pone un perfil):
- **`/control/escaleta?salida=<id>`**: la escaleta para una tablet: al aire y próximo con su miniatura y su cuenta, GO, HOLD, mover el próximo, saltar, el estante y la lista de pasos (tocar uno que viene es ir a él).
- **`/control/presentador`**: cuánto le queda al paso al aire, en grande, y qué viene después.
- **`/control/camara?camara=<nombre o número>`**: la pantalla del camarógrafo: si su cámara está al aire y los planos que vienen con ella, con su cuenta regresiva.
- El perfil **`linea:ver`** («solo mirar») deja ver todo eso sin poder tocar nada: para el productor, la producción, el presentador y las cámaras. Con `linea`, también se maneja.
- En la **Botonera** (y por ella en Companion y el Stream Deck), la plantilla **«Línea de tiempo»** trae GO, HOLD, mover el próximo, saltar y lo que está al aire, de la salida que maneja cada aparato. El bus publica `linea/<salida>/estado`, `…/proximo`, `…/overunder` y `…/propuesta` para los disparadores y los plugins.

## Si se corta la luz
La línea vuelve con el aire como estaba pero **en HOLD**, y la conducción pregunta **«¿Qué está pasando?»**. Cuando la persona te lo cuente (escrito o dictado): `linea_estado` (mirá `retoma.resumen`: cuánto duró el corte, qué estaba al aire, qué iría según el plan, qué se repuso y qué se perdió), pasá la descripción con `linea_retomar {descripcion}`, y **proponé** cómo seguir (dónde reenganchar, qué saltear, qué recortar) con `linea_editar` o `linea_ajustar` con `probar`. Cuando confirme, aplicalo y soltá con `linea_soltar {decision}`. Si dice que siga como estaba, `linea_soltar` sin cambiar nada. Sin respuesta, queda en HOLD. Para probar el flujo sin cortar nada: `linea_retomar {"simular": true}` con la línea corriendo.
