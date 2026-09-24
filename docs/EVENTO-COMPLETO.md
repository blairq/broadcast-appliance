# Organizar el evento entero, presencial y online

Documento de diseño y hoja de ruta. `EVENTOS.md` cubre la mitad que ya existe:
el **guion** —qué pasa, en qué orden, cuánto dura— y cómo se emite. Esta es la
otra mitad, la que hoy no está: **la gente**. Quién viene, cómo se lo invita,
cómo se lo recibe, cómo participa, y qué queda escrito cuando terminó.

Cada punto dice si está hecho o qué falta.

## De dónde sale esto

Del inventario de las 252 herramientas MCP en 33 categorías, mirado no como
programador sino como quien organiza el evento.

La producción no le falta nada: escenas con TAKE y preview, rack de audio con
198 plugins, streaming simultáneo a tres plataformas, grabación por entrada
separada, pantallas en la red, teleprompter, rótulos que se escriben solos,
alertas. La recepción **online** tampoco: lobby, reconocimiento de rostro,
admitir y rechazar, charla privada con el que espera, lista de auto-aceptación
por evento, fila de oradores con tiempos.

Es una sala de control completa. Y sin embargo, con esto no se puede organizar
un evento — porque un evento no es la emisión.

**La asimetría que lo resume todo:** al que entra por la *sala* lo recibís con
reconocimiento facial, ficha, charla privada y aprobación. Al que entra por la
*puerta* no lo registra nadie. El sistema tiene una recepción excelente para el
mundo remoto y ninguna para el mundo físico, que es donde está la mayoría de
la gente en un evento híbrido.

## El principio que ordena la lista

**El presencial y el remoto son el mismo evento.** No dos públicos con dos
sistemas que después alguien cruza a mano: un padrón, una acreditación, una
cola de preguntas, una urna. Cada vez que aparezca la tentación de resolver
«esto para los de la sala y esto para los de afuera», es la respuesta
equivocada; es exactamente lo que hace que los eventos híbridos se sientan como
dos eventos mal pegados, uno de ellos de segunda.

---

## 1. Fecha y huso — **falta**

Hoy `Evento::inicio` es `"HH:MM"` y nada más. No hay día y no hay huso.

Parece un detalle y es el cimiento: un evento híbrido tiene gente conectándose
desde otro huso, y «empieza 15:00» no significa nada para ellos. Convocar,
recordar, publicar la agenda, archivar y buscar después — todo cuelga de que el
evento sepa **cuándo** es.

El huso se guarda **junto al evento**, no se toma del reloj de la máquina: el
aparato es portátil y viaja. Un evento armado en Buenos Aires y corrido desde
otra máquina en otro lado tiene que seguir diciendo la misma hora.

Es el arreglo más barato de la lista y destraba todo lo demás. **Va primero.**

## 2. El padrón — **falta**

Lo que hay (`evento_invitados` / `Evento::autoAceptar`) es una lista blanca del
lobby indexada por rostro. Sirve para *dejar pasar*; no sirve para *organizar*.

Falta el artefacto central de cualquier organizador: **la lista de la gente**.
Quién está invitado, con qué papel, por dónde asiste y si confirmó.

La ficha de `Persona` (en la libreta de rostros) ya tiene id estable, nombre,
cargo, nota, foto y vectores de cara. Le falta contacto, y sobre todo falta la
relación **persona ↔ evento**, que es otra cosa: la misma persona es panelista
en uno y público en el siguiente.

```
Persona   (la libreta, permanente)   id, nombre, cargo, foto, caras…
   ↓
Asistente (por evento)               papel, canal, estado, contacto
```

- **papel**: orador, panelista, público, prensa, staff.
- **canal**: presencial, online, o sin definir todavía.
- **estado**: invitado → confirmó → llegó → se fue.

Que sea una entidad aparte y no campos sueltos en `Persona` es a propósito: los
datos del evento **caducan con el evento** y la libreta no. Si se mezclan, a la
tercera transmisión la libreta es un depósito de estados viejos y nadie sabe
cuál valía.

Sin el padrón, los puntos 3 a 7 no se pueden construir: todos preguntan lo
mismo, «¿quién es esta persona en este evento?».

## 3. Acreditación presencial — **falta** (y el 80% ya está escrito)

Es el mayor retorno por trabajo de toda la lista, porque no hay que inventar
casi nada: el lobby ya hace exactamente esto, solo que apunta a la sala en vez
de a la puerta. Reconocimiento de rostro, ficha, calidad de imagen con qué
pedir cuando no alcanza, alta automática, fusión de fichas repetidas — todo
hecho y probado.

Una cámara en la entrada y el mismo flujo: llega alguien, se lo reconoce contra
la libreta, se marca «llegó» en el padrón, y al que no está se lo da de alta en
el momento.

**Y el que no quiere que le miren la cara.** El reconocimiento no puede ser la
única puerta, por dos razones que no se discuten: hay gente que no lo va a
aceptar, y un evento no puede depender de que la cámara funcione. El QR de la
invitación es el otro camino — llega, lo muestra, queda acreditado — y el
mostrador con el teclado es el tercero. Los tres escriben en el mismo padrón.

## 4. El padrón único de presentes — **falta**

Hoy hay dos mundos que no se hablan: `participant_*` (los de la sala) y
`rostro_*` (los que ve una cámara). Nadie puede contestar **«¿quiénes
están?»** incluyendo a los dos.

En un evento híbrido esa es *la* pregunta, y en una reunión formal —asamblea,
consorcio, directorio— es obligatoria: sin presentes no hay quórum, y sin
quórum lo que se decida no vale.

Sale casi solo una vez que están 2 y 3: el padrón con estado «llegó», alimentado
por la acreditación de la puerta y por las admisiones del lobby.

## 5. Preguntas del público — **falta**

Hay chat de sala y hay chat de las plataformas (`chat_history`,
`youtube_chat`, `twitch_chat`, `facebook_chat`). **Chat no es cola de
preguntas**: no se modera, no se elige, no se ordena, no hay «la que sigue», y
no sale al aire con el nombre de quien preguntó.

El presencial está peor: en la sala solo existen `Waiting` y `Admitted`, o sea
que todos entran con cámara y micrófono. Doscientas personas en el mismo salón
entrando así es un acople garantizado y un ancho de banda tirado.

Falta **una superficie liviana para el que está en la butaca**: entra por QR,
no pide cámara ni micrófono, y sirve para ver la agenda, preguntar y votar. Es
un papel nuevo en la sala —público— que no es lo mismo que participante.

Las piezas para sacarla al aire ya están: `grafico_mostrar` para la placa,
`rotulo_vincular` para el nombre de quien pregunta, el teleprompter para que el
conductor la lea.

**Una sola cola.** La pregunta del que está en la butaca y la del que mira por
YouTube entran a la misma lista y se moderan en el mismo lugar.

## 6. Votación — **falta**

Para una conferencia es enganche; para una reunión **es el punto**. Y es el
mismo problema que las preguntas con el resultado invertido: una urna, no una
por canal. El voto del presencial y el del remoto tienen que valer lo mismo y
contarse juntos, con el padrón diciendo quién tiene derecho a votar.

Se apoya en 2 (quién puede) y en 5 (la superficie del público).

## 7. Subtítulos y transcripción — **falta**

Accesibilidad, gente que no habla el idioma, y de paso el insumo del acta. Sale
del trabajo de voz que quedó diseñado y guardado en `AUDIO-TTS.md`: el mismo
reconocimiento, apuntado al programa en vez de al operador.

## 8. El acta — **falta** (hoy hay un informe, que es otra cosa)

`evento_informe` dice que el bloque 3 se fue cuatro minutos: perfecto para el
operador, inútil para el que organizó.

Un acta es **quiénes asistieron, qué se decidió, qué se votó y qué quedó
pendiente**. Con el padrón (2, 4), la votación (6) y la transcripción (7), es
sobre todo ensamblar cosas que ya están.

## 9. Capítulos y cortes de la grabación — **falta, y es casi gratis**

`Guion::recorrido()` ya guarda, por cada cue ejecutado, desde cuándo hasta
cuándo — reloj de pared real, no lo previsto. Con eso, cortar la grabación por
bloque o por orador y subirla con capítulos es ensamblar, no construir.

Es lo primero que te piden al día siguiente, y hoy se hace a mano mirando el
video.

## 10. Convocatoria y recordatorios — **a decidir, no a construir**

No hay correo ni mensajería, y puede que esté bien así: esto corre portable
desde un pendrive, sin depender de servicios de nadie. Los tipos de QR que ya
existen (`correo`, `sms`, `contacto`, `wifi`) sugieren el camino elegido:
**generar, no enviar**. El sistema arma la invitación con su enlace y su QR, y
el que convoca la manda por donde ya manda todo.

Vale la pena que sea una decisión escrita y no una omisión que se descubre
tarde.

## 11. Prueba técnica previa con los remotos — **falta**

`evento_ensayo_rapido` ensaya el guion, no a la gente. La causa número uno de
que un evento híbrido se caiga es el orador que prueba el micrófono cuando le
dan la palabra.

Con el lobby, `rostro_calidad` y `audio_measure` ya está casi todo para un
autochequeo: entra cuando quiere, y el sistema le dice «la cámara está bien, el
micrófono está bajo, acercate» sin que haya nadie del otro lado.

---

## 12. El asistente de armado — **falta**, y va al final a propósito

Cuando todo lo anterior exista, **una vista de asistente que arme el evento
entero conversando**. No un formulario con más campos: el organizador cuenta lo
que va a pasar y el sistema propone el evento armado.

> «Asamblea del consorcio, el jueves 8 a las 19, dos horas, presencial en el
> salón y por videollamada. Hay que votar tres puntos. Son 40 propietarios.»

Y de ahí sale la fecha con su huso, los bloques con tiempos, los tres puntos a
votar como cues, el padrón con los 40, la invitación con su QR, la sala con
lobby y la acreditación en la puerta prendida.

**Por qué al final y no ahora.** Un asistente no puede armar lo que el sistema
no sabe representar: hoy propondría bloques y tiempos —eso ya está diseñado en
`EVENTOS.md` §10— y nada más, porque padrón, acreditación, preguntas y urna no
existen como cosas. Construirlo antes obliga a inventarle un modelo propio que
después hay que tirar. Se hace cuando hay qué llenar.

**Lo que sí hay que respetar desde ahora**, para que exista después:

- Cada paso de estos deja su **herramienta MCP con ayuda de qué es, cómo se usa
  y para qué sirve**, como todo lo demás del sistema. El asistente de armado no
  es un motor aparte: es el asistente que ya existe, usando estas herramientas.
  Si cada pieza queda bien expuesta y bien descripta, el asistente sale casi
  solo; si no, no hay asistente que lo salve.
- **Propone, no ejecuta.** Armar un evento entero de una orden es justo el caso
  donde una equivocación cuesta caro y se descubre en vivo. Deja el evento
  escrito para revisar, no lo pone al aire.

---

## Orden

Por dependencia, no por tamaño:

1. **Fecha y huso** (§1) — chico, destraba el resto.
2. **Padrón: persona ↔ papel ↔ canal ↔ estado** (§2) — la pieza estructural.
   Sin esto lo demás son parches sueltos.
3. **Acreditación presencial reusando el lobby** (§3) — el mayor retorno por
   trabajo. Recién acá el padrón se llena solo y se puede contestar «¿quiénes
   están?» (§4).
4. **Superficie del público** (§5) — un papel nuevo en la sala, por QR, sin
   cámara ni micrófono. Abre preguntas y votación.
5. **Preguntas** (§5) y **votación** (§6), en esa superficie.
6. **Capítulos de la grabación** (§9) — se puede colar en cualquier momento:
   es chico y se disfruta enseguida.
7. **Acta** (§8), cuando haya padrón y urna.
8. **Subtítulos** (§7), con lo de `AUDIO-TTS.md`.
9. **Asistente de armado** (§12), cuando haya qué armar.
