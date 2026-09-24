# Organizar el evento entero, presencial y online

**Once de los doce puntos están hechos.** El que falta es el de subtítulos
(§7), que depende de un trabajo que se decidió dejar para más adelante.

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

## 1. Fecha y huso — **hecho**

Hoy `Evento::inicio` es `"HH:MM"` y nada más. No hay día y no hay huso.

Parece un detalle y es el cimiento: un evento híbrido tiene gente conectándose
desde otro huso, y «empieza 15:00» no significa nada para ellos. Convocar,
recordar, publicar la agenda, archivar y buscar después — todo cuelga de que el
evento sepa **cuándo** es.

El huso se guarda **junto al evento**, no se toma del reloj de la máquina: el
aparato es portátil y viaja. Un evento armado en Buenos Aires y corrido desde
otra máquina en otro lado tiene que seguir diciendo la misma hora.

Es el arreglo más barato de la lista y destraba todo lo demás. **Va primero.**

**Hecho.** `Evento` tiene `fecha` y `huso`, y de los dos sale `cuando`: el
arranque como instante absoluto con desfasaje, que es lo único que le sirve al
que está afuera. Un formato mal escrito se rechaza en vez de guardarse, y la
validación es de calendario y no de rango —un 31 de abril pasa cualquier
chequeo de «día entre 1 y 31»—. En `evento_guardar` y en la página del evento.

## 2. El padrón — **hecho**

Lo que había (`evento_invitados` / `Evento::autoAceptar`) era una lista blanca
del lobby indexada por rostro. Sirve para *dejar pasar*; no sirve para *organizar*.

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

**Hecho:** `evento/Padron.{hpp,cpp}` con `Asistente` (papel, canal, estado,
contacto, si vota, cuándo llegó y por dónde), el archivo `padron.json` al lado
del guion, y cinco herramientas MCP: `padron_ver` (con el recuento y el
quórum), `padron_anotar`, `padron_cambiar`, `padron_acreditar` y
`padron_quitar`. Se busca por ficha de rostro, por id o por nombre tipeado
—sin acentos, sin mayúsculas, con espacios de más, que es como se tipea en un
mostrador—.

Tres decisiones que quedaron escritas en el código:

- **Archivo aparte del guion**, porque tienen ritmos distintos: el guion se
  escribe antes y se guarda cuando uno quiere; el padrón cambia durante y se
  guarda solo, en el momento. En la puerta, con cola, nadie aprieta guardar.
- **Acreditar dos veces no pisa la hora.** En la puerta se vuelve a escanear
  todo el tiempo; pisar la hora convertiría «llegó 18:40» en «llegó 20:15», que
  es justo el dato que después nadie puede reconstruir.
- **`dejarPasarSolo` necesita ficha de rostro**, y si se la sacan se apaga
  solo: dejar pasar a quien *escribió* un nombre es dejar pasar a cualquiera
  que sepa escribirlo.

Se llena solo desde la puerta y desde el lobby (§3), y a mano o por el
asistente cuando hace falta.

**Unificado con `Evento::autoAceptar`:** la lista blanca vieja del lobby se
vuelca al padrón la primera vez que se abre el evento y se apaga. Con ella se
fue `evento_invitados`, cuya validación —que la ficha de rostro exista de
verdad— se mudó a donde ahora se otorga el permiso: una entrada con un id que
no existe no deja pasar a nadie, y nadie se entera hasta que esa persona se
queda esperando en el lobby.

## 3. Acreditación presencial — **hecha**

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

**Hecho:** el interruptor de la puerta (`padron_puerta`, y en la página de
Personas) mira una cámara, reconoce y acredita. Comparte la ronda con el lobby
y se turnan, así que tener las dos prendidas no cuesta el doble: cuesta la
mitad de rápido cada una. Pide la misma seguridad que para abrir la puerta de
la sala y que la cara se vea bien, porque acreditar al que no es deja a dos
personas mal anotadas y no se descubre hasta contar el quórum.

**No anota a nadie en el padrón**: al que llega sin estar en la lista lo avisa
y sigue. Quien entra sin estar invitado es una decisión del que organiza, no
del reconocimiento.

El mostrador con teclado ya funciona con `padron_acreditar` por nombre. **Falta
el camino del QR**, que depende de la invitación (§10).

## 4. El padrón único de presentes — **hecho**

Hoy hay dos mundos que no se hablan: `participant_*` (los de la sala) y
`rostro_*` (los que ve una cámara). Nadie puede contestar **«¿quiénes
están?»** incluyendo a los dos.

En un evento híbrido esa es *la* pregunta, y en una reunión formal —asamblea,
consorcio, directorio— es obligatoria: sin presentes no hay quórum, y sin
quórum lo que se decida no vale.

**Hecho**, y salió casi solo una vez que estuvieron 2 y 3: el padrón con estado
«llegó», alimentado por la acreditación de la puerta y por las admisiones del
lobby. `padron_ver` trae `presenciales` y `enLinea` por separado y el quórum
junto. Los dos mundos ya se hablan.

## 5. Preguntas del público — **hecho**

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

**Hecho:** `/publico` en el gateway —una página nueva junto a `/board`,
`/prompter` y `/rotulos`—, la clase `Preguntas` con su recorrido
(nueva → elegida → al aire → respondida, o descartada), y cuatro herramientas:
`preguntas_ver`, `pregunta_moderar`, `pregunta_sumar` y `publico_pagina`.

- **El público no entra a la sala.** La página no pide cámara ni micrófono y no
  abre WebRTC. Era el problema de fondo: con `Waiting`/`Admitted` como únicos
  estados, doscientas personas en el mismo salón entrando como participantes
  son un acople garantizado y el ancho de banda tirado.
- **`elegida` separada de `nueva`** es lo que permite moderar antes en vez de
  leer en vivo lo primero que llegó.
- **Los votos ordenan, no deciden.** Se cuentan por navegador, que alcanza para
  que no sume diez veces el que toca diez veces y no para frenar a alguien
  decidido. Queda dicho en el código para que nadie construya una urna encima.
- **El QR va con la clave adentro**: se proyecta o se imprime en el programa, y
  pedirle a doscientas personas que tipeen una clave es perder a la mitad.
- **El fondo del evento** se manda una vez a la sala y de ahí lo toman todos los
  teléfonos, con tope de 2 MB: lo baja cada aparato del salón, por el mismo wifi
  que suele llevar la transmisión.
- **El texto es de afuera** y se pinta con `textContent`, nunca como HTML: un
  `<img onerror>` en una pregunta correría en el teléfono de todos los demás.

## 6. Votación — **hecho**

Para una conferencia es enganche; para una reunión **es el punto**. Y es el
mismo problema que las preguntas con el resultado invertido: una urna, no una
por canal. El voto del presencial y el del remoto tienen que valer lo mismo y
contarse juntos, con el padrón diciendo quién tiene derecho a votar.

Se apoya en 2 (quién puede) y en 5 (la superficie del público).

**Hecho, y la decisión que lo ordena es que son DOS COSAS y no se mezclan:**

- **Encuesta** — vota cualquiera con el enlace, una vez por teléfono. Es
  enganche y sus números son *indicativos*.
- **Votación formal** — vota quien está en el padrón con derecho y ya llegó,
  una vez cada uno, con un **código** que se le da al acreditarlo. El teléfono
  no alcanza: se borra el sitio y se vota de nuevo.

Se llaman distinto a propósito. «Una votación con un interruptor de rigor»
termina en alguien leyendo al aire el resultado de una encuesta como si fuera
una decisión.

Lo demás que quedó escrito:

- **El denominador es del padrón, no de la urna.** «Votaron 12 de 40», no
  «12»: sin eso, dos a favor y cero en contra parece unánime cuando en realidad
  no vino nadie.
- **Secreta no es anónima.** Se sigue sabiendo *quién* votó —hace falta para
  probar el quórum— y se deja de guardar *qué* votó.
- **El resultado no se ve mientras está abierta**, salvo que se decida lo
  contrario: ver que va perdiendo cambia lo que vota el que todavía no votó.
- **Volver a votar cambia el voto**, no suma. Cerrar la urna es lo que lo vuelve
  definitivo.
- **El código no lleva O ni 0, ni I ni 1**: se dicta en voz alta y ahí son la
  misma letra.
- **Una urna por vez**, y abrir otra no arrastra los votos de la anterior.

## 7. Subtítulos y transcripción — **falta, y queda esperando a propósito**

Accesibilidad, gente que no habla el idioma, y de paso el insumo del acta. Sale
del trabajo de voz que quedó diseñado y guardado en `AUDIO-TTS.md`: el mismo
reconocimiento, apuntado al programa en vez de al operador.

**Es el único punto de esta lista que no se hizo, y no por olvido:** todo su
trabajo es el de `AUDIO-TTS.md`, que se decidió dejar para más adelante. Sin
reconocimiento de voz no hay subtítulos, y hacer «medio» reconocimiento de voz
para tener subtítulos sería construir dos veces lo mismo. Cuando se retome
aquello, esto sale casi de arriba: el texto ya tiene dónde ir —el acta (§8) lo
espera— y las pantallas ya saben mostrar cosas.

## 8. El acta — **hecho**

`evento_informe` dice que el bloque 3 se fue cuatro minutos: perfecto para el
operador, inútil para el que organizó.

Un acta es **quiénes asistieron, qué se decidió, qué se votó y qué quedó
pendiente**. Con el padrón (2, 4), la votación (6) y la transcripción (7), es
sobre todo ensamblar cosas que ya están.

**Hecho:** `evento_acta` la arma y la deja en la carpeta del evento como
`acta-<fecha>.md`. Texto plano a propósito: se abre en cualquier lado y se pega
en un correo sin romperse.

- **El quórum va arriba y con sus dos números.** Un acta que dice «se aprobó por
  8 votos a favor» sin decir cuántos podían votar no prueba nada.
- **Lleva todas las votaciones del evento, no la última.** Para eso la urna
  ahora guarda un historial, y una votación que se reemplaza sin cerrar igual
  queda registrada: para el acta importa que se votó, no que alguien se haya
  acordado de apretar cerrar.
- **Dice de cada una si fue formal o encuesta.** Una encuesta leída como
  decisión es el error que toda esta separación existe para evitar.
- Si el gateway no contesta, el acta sale igual **pero lo avisa**: un acta a la
  que le faltan las votaciones y no lo aclara es peor que ninguna.

## 9. Capítulos de la grabación — **hecho**

`Guion::recorrido()` ya guarda, por cada cue ejecutado, desde cuándo hasta
cuándo — reloj de pared real, no lo previsto. Con eso, cortar la grabación por
bloque o por orador y subirla con capítulos es ensamblar, no construir.

Es lo primero que te piden al día siguiente, y hoy se hace a mano mirando el
video.

**Hecho:** `evento_capitulos` da el texto listo para pegar en la descripción.

- **Se miden desde que arrancó la grabación, no desde el evento.** Casi siempre
  se aprieta grabar antes de empezar, y contar desde el evento dejaría todas las
  marcas corridas. Para eso hizo falta guardar el arranque en reloj de pared: el
  monótono que ya había sirve para contar cuánto lleva grabando, no para
  compararlo con el recorrido del guion.
- **Uno por bloque, no por cue.** Cuarenta capítulos de quince segundos no le
  sirven a nadie.
- Las reglas de YouTube están contempladas (el primero en 0:00, ninguno de menos
  de diez segundos, mínimo tres) y cuando no se cumplen lo dice, en vez de dar
  una lista que YouTube va a ignorar en silencio.
- Los bloques que pasaron **antes** de apretar grabar se cuentan aparte y no se
  les inventa una marca: no están en el video.

## 10. Convocatoria — **decidido y hecho**

No hay correo ni mensajería, y puede que esté bien así: esto corre portable
desde un pendrive, sin depender de servicios de nadie. Los tipos de QR que ya
existen (`correo`, `sms`, `contacto`, `wifi`) sugieren el camino elegido:
**generar, no enviar**. El sistema arma la invitación con su enlace y su QR, y
el que convoca la manda por donde ya manda todo.

**Decidido así, y escrito para que no se discuta de nuevo: el sistema genera,
no manda.** No hay correo ni mensajería y no los va a haber: esto corre portable
desde un pendrive y no puede depender de los servicios de nadie ni guardar las
credenciales de un correo. Se arma la invitación y la manda el que convoca, por
donde ya manda todo.

**Hecho:** `invitacion` da el texto y el QR personal de cada persona del padrón.

**Y ese QR cierra el camino que faltaba en §3.** Hace tres cosas con un solo
escaneo: **lo acredita** (llega, lo mira con el teléfono y queda anotado),
le abre la página donde pregunta y vota, y le deja puesto su código de votante
para que no tenga que tipearlo.

Por eso el QR de cada persona es distinto y no se comparte; el que se proyecta
para todos es otro y no acredita a nadie.

Del lado de adentro es un **buzón**, como el de los rótulos: el gateway no tiene
el padrón —lo tiene la app— así que anota quién llegó y la app lo viene a buscar
cada dos segundos. Encolar es lo que hace que acreditarse funcione aunque la app
esté ocupada o reiniciando; una llamada directa a un proceso que no está se
pierde, y el que se acreditó ya guardó el teléfono.

## 11. Prueba técnica previa con los remotos — **hecho**

`evento_ensayo_rapido` ensaya el guion, no a la gente. La causa número uno de
que un evento híbrido se caiga es el orador que prueba el micrófono cuando le
dan la palabra.

Con el lobby, `rostro_calidad` y `audio_measure` ya está casi todo para un
autochequeo: entra cuando quiere, y el sistema le dice «la cámara está bien, el
micrófono está bajo, acercate» sin que haya nadie del otro lado.

**Hecho:** `invitado_chequeo` junta en una respuesta lo que hoy había que ir a
buscar a tres lados —calidad de la cara, nivel de audio y si está silenciado— y
devuelve `listo` como respuesta corta y `quedecirle` como la frase para
mandarle, escrita para que la lea alguien que no sabe de audio: «acercate un
poco» y no «estás a -42 dBFS».

**Informa, no arregla ni le habla a nadie.** Mandar el mensaje es decisión del
que opera: el invitado puede estar hablando con otro o todavía no saber que lo
están mirando.

---

## 12. El asistente de armado — **hecho**, y fue lo último a propósito

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

**Hecho, en tres piezas:**

1. **`evento_armar`** crea el evento entero —fecha con huso, bloques con
   tiempos, cues y padrón— en una llamada.
2. **El rol `organizador`**, con su prompt propio: es donde vive lo que no está
   escrito en ningún otro lado —que un evento híbrido necesita fecha *con
   huso*, que sin `vota` no hay quórum, que cada punto a votar es un bloque,
   que la prueba técnica se hace antes y no cuando le dan la palabra— y lo que
   hay que ofrecer sin que lo pidan.
3. **La vista** en la página de Evento: se cuenta el evento, se ve correr al
   agente y queda a la vista lo que armó, con «Verlo» o «Descartarlo».

**Las categorías del rol están acotadas y eso es la mitad del diseño:** lleva
`evento`, `padron`, `preguntas`, `medios`, `prompter`, `graficos`, `qr`,
`operador` y `rostros`. **No lleva audio, ni plataformas, ni salidas.** Un rol
que arma un guion no tiene por qué poder sacar algo al aire, y *no poder
hacerlo* es la contención — el mismo principio que en `vision`.

Viene prendido de fábrica, como `vision` y por la misma razón: no tiene
disparadores, así que no hace nada hasta que alguien le encarga algo. Apagado,
el botón de la página no tendría a quién encargarle, y quien lo aprieta no
tiene por qué saber que antes hay que ir a prender un rol.

Se le pide al asistente que **muestre antes de armar** y que corra
`evento_revisar` después. Nunca pisa un guion existente: si ya hay uno con ese
nombre, crea otro.

---

## Orden

Por dependencia, no por tamaño:

1. ~~**Fecha y huso** (§1)~~ — **hecho**.
2. ~~**Padrón: persona ↔ papel ↔ canal ↔ estado** (§2)~~ — **hecho**, con
   `autoAceptar` ya volcado adentro.
3. **Acreditación presencial reusando el lobby** (§3) — el mayor retorno por
   trabajo. Recién acá el padrón se llena solo y se puede contestar «¿quiénes
   están?» (§4).
4. ~~**Superficie del público** (§5)~~ — **hecha**: `/publico`, por QR, sin
   cámara ni micrófono.
5. ~~**Preguntas** (§5) y **votación** (§6)~~ — **hechas**, sobre la misma
   superficie y el mismo padrón.
6. ~~**Capítulos de la grabación** (§9)~~ — **hecho**.
7. ~~**Acta** (§8)~~ — **hecha**.
8. ~~**Convocatoria** (§10) y **prueba técnica previa** (§11)~~ — **hechas**.
9. ~~**Asistente de armado** (§12)~~ — **hecho**.
10. **Subtítulos** (§7) — lo único pendiente, y espera a que se retome
    `AUDIO-TTS.md`.

---

## Dónde quedó

Once de los doce puntos, hechos. El único que falta es el de subtítulos, que
depende de un trabajo que se decidió dejar para más adelante.

El sistema ya no sabe solamente emitir: sabe **cuándo** es el evento, **quién**
viene, **quién llegó** —por la puerta, por la sala o escaneando su invitación—,
**qué pregunta** el público de los dos lados, **qué se votó** y con qué quórum,
y deja el **acta** y los **capítulos** cuando terminó. Y se arma contándolo.
