# Atención a quien se conecta (Sala y Asamblea)

**Hecho (2026-10-25), sin probar a mano.** «Los atiende el asistente» y, prendido,
«Hablando», en el lobby de la Sala y en la sesión remota de la Asamblea; el alta
automática en Personas; quién entra solo si se lo reconoce. Lo mismo por MCP
(`atencion_estado`, `atencion_configurar`, `atencion_voz_probar`).

## Las decisiones

**Quién atiende.** El rol **recepción** si está prendido (un agente de un tiro
con su juego de herramientas, hecho para esto); si no, el asistente de
**automatización** (trabaja solo, en segundo plano). **Nunca el del evento**: es
la charla con el operador, y meterle encargos del lobby le ensucia la
conversación a quien está trabajando. Sin ninguno de los dos, la opción no se
prende y se dice por qué. Es una sola regla (`sala::quienAtiende`, con test) que
usan el encargo, la interfaz y el MCP.

**Cuándo termina.** Al admitirla (o al irse, o al rechazarla). La atención es del
**lobby**: adentro ya está en el programa, lo que oye es el retorno, y una voz
sintetizada hablándole ahí sonaría para todos los que oyen el retorno. Lo de
adentro es del operador y del asistente del evento. El gateway apaga solo la
atención hablada al admitirla.

**Hablando, de a uno.** Hay un solo canal de voz del que atiende hacia la sala
(`BA.Audio.Atendedor`). El primero que llega con «Hablando» prendido toma la voz;
los demás se atienden por chat. Cuando el de la voz entra (o se va), la toma el
siguiente que se estaba atendiendo por chat.

## Cómo anda (Sala)

No hay un sistema aparte: el encargo es el de siempre (`McpService`,
`setIntakeSink`) y el que atiende sigue usando `chat_send` y `chat_wait`. Con
«Hablando», además (`ui/AtencionViewModel`):

1. La app le pide a la sala `POST /api/v1/participants/<id>/atiende {on}`: su
   ruta lleva `atiende`, su voz sube a `BA.Audio.Lobby.<k>` (como cuando el
   operador le habla) y le baja la del que atiende, **no** la del operador.
2. Su voz se toma de su lugar del lobby (`OperatorFeed::escucharLobby`), se pasa a
   texto con el **mismo** reconocedor del dictado (un oído propio en
   `agent::Voz`) y entra a su hilo del chat como un mensaje suyo con
   `voz: true` (`ConferenceViewModel::agregarDichoPorVoz`). El que atiende lo lee
   con `chat_wait` como si lo hubiera escrito; el operador lo ve con 🎙.
3. Todo lo que se le escribe a esa persona (`ControlSala::chatear`, venga del que
   atiende o del operador) se dice además con **la voz del atendedor**
   (`audio::Habla`) y sale por `BA.Audio.Atendedor` (`emision/VozAlLobby`, a
   ritmo de reloj). El gateway se la manda solo a esa persona; si el operador le
   está hablando, gana el operador.
4. Al encargo se le agrega cómo hablarle: frases cortas, de a una pregunta, sin
   listas ni emojis, confirmar en voz alta los datos.

Su página dice «Te atiende el asistente: hablale, te escucha».

## Cómo anda (Asamblea)

El que espera en la sesión remota manda solo la foto de su celular (el micrófono
se abre cuando tiene la palabra): **no hay por dónde escucharlo**, así que
«Hablando» no existe ahí todavía. Atenderlo es mirarle la cara:

- se pide su foto y se compara con la ficha de Personas de su banca
  (`persona_id`);
- si es él (parecido ≥ `rostros.admitirSolo`, cara que sirve) **y** el padrón del
  evento lo deja entrar solo, se lo habilita y se avisa siempre;
- si no, se le encarga al que atiende contarle al operador lo que vio la cámara
  (`operator_ask`) y habilitarlo si dice que sí (`sesion_habilitar`). La
  recepción ve por eso la categoría `sesion`.

## Personas

- Con la atención de la sala prendida, **el lobby se mira siempre**
  (`PersonasViewModel::setLobbyPorAtencion`), esté o no el interruptor.
- **Alta automática** (`altaAutomatica`, se guarda en
  `Config/personas-opciones.json`): a quien dio su nombre (lo escribió al entrar o
  se lo corrigió quien lo atendió; `sala::nombreDado` descarta «Invitado 3»,
  «iPhone», números) y tiene una cara que sirve, se le pone ese nombre y su cargo
  a la ficha que el reconocimiento le anotó sin nombre. Una ficha nombrada a mano
  no se toca. En la asamblea, al integrante sin ficha se le arma una con el
  nombre de su credencial y se la vincula a su banca.

## Quién entra solo

Es el permiso **entra solo** del padrón del evento (`Asistente::dejarPasarSolo`),
el mismo para la sala y la asamblea: no hay otra lista. Se ve y se cambia desde
Sala, Asamblea y Personas («Entran solos si se los reconoce → Elegir…»); por MCP,
`padron_ver {solos: true}` y `padron_cambiar {id, dejarPasarSolo}`.

## La voz del atendedor

Configuración → **Voz del sistema** → pestaña **Voz del atendedor** (la de
siempre quedó en la pestaña «Voz del operador»). Se elige entre las voces bajadas
(tipo «tts»), con hablante y velocidad, en `Config/atencion.json`. **Probar no
suena**: genera la frase, muestra la duración y la forma de onda, y deja
`Logs/voz-atendedor-prueba.wav` para escucharlo con auriculares.

## Falta / a probar a mano

- Probar con una persona real en el lobby: que la voz suba con `atiende`
  (`escucharEntrada` sobre `ba-lobby-monitor-<k>`), que la transcripción llegue al
  hilo y que la voz del atendedor se oiga en su navegador (y no en el de los
  demás).
- «Hablando» en la asamblea: haría falta que la página de la sesión abra el
  micrófono hacia el gateway mientras espera (otro canal, sin pasar al aire).
