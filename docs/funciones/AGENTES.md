# Agentes automáticos: roles, despachador y corridas

El asistente interactivo (pestaña ✦) es **una** conversación con quien dirige.
Todo lo que llegaba de afuera a deshora —alguien en el lobby, un canal que
satura— se le escribía en esa misma conversación y le rompía lo que estuviera
haciendo (armar escenas, por ejemplo). Este subsistema es la salida.

## La idea: el residente es el aparato, no el agente

La app ya vigila (el `AlertEngine` evalúa cada 250 ms). Cuando algo pasa y un
**rol** activo lo reclama, el **despachador** lanza un agente **de un solo
tiro** —`claude -p --output-format stream-json` u `opencode run --format
json`— con el juego de herramientas del rol y el encargo como argumento. El
proceso vive lo que dura el trabajo y se muere.

Lo que desaparece con eso: procesos ociosos, sesiones que se pisan, latidos,
inyección de texto en una TUI que capaz está en un menú. (Con OpenCode, el
encargo de un rol que no corre aparte va a la conversación del evento: ver
«Una conversación por evento».) Y el problema
original se resuelve solo: al asistente interactivo no le llega nada, porque
el despachador nunca le habla.

## Una conversación por evento

Cada evento tiene **su** conversación con el asistente, una por pestaña (la del
evento y la de automatización). Se retoma sola al reiniciar la app o al volver a
abrir ese evento, y al pasar a otro evento se usa la de ese (la primera vez se
empieza una). Así el asistente tiene el contexto del evento entero: lo que se
charló, los avisos que llegaron y lo que se hizo con ellos.

Lo que el programa le manda solo —los avisos de los disparadores, las novedades
del evento, las notas del sistema— cae en esa misma conversación, con su marca
al principio (`[AVISO AUTOMATICO de Broadcast Appliance]`, `[NOVEDADES del
evento]`, `[ENCARGO AUTOMATICO]`) para que se distinga de lo que escribe el
operador. **No se abre una sesión (una pestaña) nueva por aviso.**

Con **OpenCode** (OpenCode Go, OpenRouter):

- los avisos no se escriben en la terminal: van al servidor de OpenCode, a la
  sesión del evento, **en cola**. Si el asistente está respondiendo, esperan y
  salen cuando termina, en orden; no cortan la respuesta ni se mezclan con lo
  que el operador está escribiendo. Si llegan varios mientras tanto, salen
  **juntos en un solo mensaje**. Las imágenes que trae un encargo van adjuntas;
- los encargos de los roles (más abajo) también caen en la conversación del
  evento del asistente de automatización, salvo los roles que corren **aparte**.

Con **Claude Code**, la terminal retoma la conversación del evento
(`--resume`) y los avisos se le escriben como antes (Claude los encola si está
respondiendo). Los roles con Claude corren siempre aparte: una conversación no
puede tener dos programas escribiéndola a la vez.

**Conversación nueva** (botón de la pestaña del asistente, o `assistant_run`
con `new_conversation` por MCP) empieza otra para ese evento; la de antes queda
en el historial del motor. `assistant_status` dice cuál se está usando
(`sesionEvento`). Se guarda en `Config/asistente-sesiones.json` del evento; un
evento copiado empieza la suya (no comparte la del original).

## Un rol

`src/app/agent/Roles.{hpp,cpp}`. Cada rol tiene:

| | quién lo fija | qué es |
|---|---|---|
| categorías | el sistema | el juego de herramientas MCP que ve (más las comunes: `ayuda`, `estado`, `agentes`, `registro`) |
| disparadores | el sistema | los tipos de disparador que atiende (`lobby`, `audio_peak`…) |
| prompt | el sistema | el `CLAUDE.md`/`AGENTS.md` de su espacio de trabajo |
| agrupación | el sistema | una corrida por evento, o juntar lo que llegue en N ms |
| activo, proveedor, modelo, consola, aparte | el operador | `Config/roles.json` |

**Aparte o en la conversación del evento.** Con el asistente de
automatización andando en OpenCode, un encargo de un rol va a **la
conversación del evento** de ese asistente (`opencode run --server …
--session …`): con todo el contexto del evento y con las herramientas, el motor
y el modelo de ese asistente; las instrucciones del rol van al principio del
mensaje. Un rol **aparte** corre cada encargo en su propia sesión, con su juego
de herramientas, su prompt y su motor, como antes. Corren aparte siempre los
que tienen otro proveedor u otro modelo, y de fábrica **visión** (las imágenes
no entran a la conversación del asistente, que aguanta muy pocas),
**escenógrafo** (consola, disco y red) y **organizador** (arma el evento
entero). La recepción y el técnico, que atienden disparadores, van a la
conversación del evento. Se cambia en la página Agentes («Aparte») o con
`agente_configurar` (`aparte`).

Roles de esta versión: **recepción** (lobby: ficha, verificación de identidad,
dejar listo para pasar) y **técnico** (audio y entradas: medir antes de tocar,
cambiar lo mínimo). Ambos vienen apagados: nada cambia hasta que alguien los
prende en la página Agentes.

**Motores mezclados a propósito.** Cada rol elige su motor y su modelo. Es la
estrategia de costo, no una comodidad: la recepción en OpenCode Go, el
asistente en la suscripción de Claude y el técnico en un modelo por API no
comparten techo de uso. Las credenciales son por motor (un solo login sirve
para todos los roles que lo usen); el espacio de trabajo es por rol
(`Runtime/agentes/<rol>/`).

## Identidad por conexión

`ba-mcp --rol <rol>` manda la cabecera `X-BA-Rol` en cada pedido. El servidor
(`McpCore::setRoles`) le muestra en `tools/list` **solo** las herramientas de
sus categorías y las comunes; llamar a otra se niega con un mensaje que dice
de quién es. El mapa de categorías del `initialize` también es el del rol.

Dos cosas que da y una que no:

- **Barandilla contra el accidente**: un agente no puede tocar por accidente lo
  que no es suyo (la recepción no tiene `scene_delete`: no es disciplina, es
  que la herramienta no está).
- **Contexto chico**: cada herramienta que no ve son tokens que no paga en
  cada pedido (~4.000 en vez de ~32.000).
- **No es una frontera de seguridad**: el token ya da acceso a todo, y un
  proceso que lo tiene puede presentarse con el rol que quiera.

## El despachador

`src/app/agent/Despachador.{hpp,cpp}`. Reglas que hacen que no se descontrole:

- **Una corrida por rol a la vez**; lo demás espera en cola.
- **Agrupar**: el técnico junta hasta cinco eventos en 3 s en una corrida (dos
  picos seguidos son un problema, no dos).
- **Profundidad 1**: un agente puede encargarle algo a otro rol
  (`agente_encargar`), pero lo que ese encargo lance no puede encargar a
  nadie. Sin esto, dos roles se mandan pedidos en círculo.
- **Memoria como dato, no como contexto**: cada corrida recibe el contexto que
  le arma el sistema (para la recepción: quién está adentro y quién espera) y
  los resúmenes de las últimas cinco corridas de su rol. No arrastra una
  conversación que crece hasta que el motor la compacta y pierde justo lo de
  hace dos horas. Y la verificación de identidad **no depende de lo que un
  modelo recuerda**.
- **Tope de 20 minutos** por corrida; el mismo origen de lobby no se duplica
  (el disparador y el click del operador pueden traer a la misma persona).
- Cada agente termina con `RESUMEN:`; eso es lo que ve el operador y lo que
  recibe la próxima corrida.

Una **corrida** guarda sus pasos leídos de la salida JSON del motor: qué dijo,
qué herramienta llamó y con qué, qué le contestó. Es la vista de «qué está
haciendo», pareja para Claude Code y opencode, sin raspar ninguna terminal.
Todo va también al registro con componente `agente.<rol>`.

## Por MCP y en la interfaz

Categoría `agentes`: `agente_roles`, `agente_configurar`, `agente_encargar`,
`agente_corridas`, `agente_corrida`, `agente_detener`. La página **Agentes**
(riel, grupo Sistema) muestra lo mismo: los roles con su interruptor, motor y
modelo; las corridas, que se despliegan paso a paso; y un campo para
encargarle algo a mano. Una sola fuente (`Studio.despachador`), así lo que se
toca por un lado se ve por el otro.

Arriba de los roles, **Oído (voz a texto)**: qué reconocedor usa cada uno que
escucha —el dictado, la transcripción continua y la traducción en tiempo real, la
recepción, el tótem—, Parakeet (de fábrica) o Nemotron (streaming), cuáles están
en memoria y la alerta si están los dos a la vez. Por MCP, `voz_reconocedores`.
Detalle en AUDIO-TTS.md («Dos reconocedores»).

`chat_wait` (categoría `sala`) existe para esto: un agente de un tiro que
atiende a alguien hace una pregunta con `chat_send` y espera la respuesta con
`chat_wait`, en vez de consultar `invitados_chat_leer` cada tanto.

## Lo que sigue

- Cortar las categorías de plataforma (`youtube` mezcla «leer el chat en vivo»
  con «administrar los videos»): hasta entonces no hay rol **comunidad**,
  porque le daría permiso para borrar videos.
- Política de aprobación **por rol** y una bandeja única, con «aprobar =
  ejecutar la acción grabada» sin volver a correr el modelo.
- Escalera regla → agente en los disparadores: primero la acción determinista;
  si no alcanza, recién ahí un rol.
- Enlaces de invitación por persona (hoy es un PIN por sala); reconocimiento
  de rostros como herramienta determinista (detector + embedding, ONNX
  vendoreado), con su administración de rostros; agentes en el diario de
  arranque.
