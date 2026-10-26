# Sesión remota: integrantes que sesionan desde afuera

Un integrante que no está en el recinto entra por el navegador (WebRTC) y **ocupa su banca**: lo
ven y lo escuchan en la sala y en la transmisión cuando tiene la palabra, pide la palabra en la misma
fila que los presenciales, vota, cuenta para el quórum, ve la pantalla de la sala y escucha la sala.

Es un sistema **aparte de la sala de invitados**. Reusa sus piezas técnicas (señalización, WebRTC,
túnel, el camino de los medios a la app), pero no su modelo: acá cada participante es un **integrante
del Cuerpo** con credencial propia, y lo que hace (estar presente, votar) va al acta.

## 0. Transparente: en la sala o afuera

No es solo para los de afuera: es **una banca virtual** que anda en cualquier lado. Para el sistema
da lo mismo si el integrante está en el recinto o en su casa: **un integrante, una presencia, un
voto**. Así también es el **respaldo de la central**: si se quema la CCU (o se corta el audio), los
integrantes abren su link en el celular, votan y lo usan de micrófono, y la sesión sigue.

- **Dónde está:** el gateway sabe si entró por la red local o desde internet (por el túnel). En la
  sala el celular es **solo micrófono** (apretar para hablar) y **no** reproduce la sala (si no,
  acopla con los parlantes); afuera recibe la vuelta de la sala. El integrante lo puede cambiar.
- **Voz a la sala:** la salida «A la sala» de la matriz va a la entrada de la CCU cuando anda, o
  directo a la potencia de sala cuando no (se elige en el panel de audio, sin tocar nada más).
- **Fila y voto:** con la central, su fila y la de los celulares se unen; sin la central, la de los
  celulares es la única. **Un voto por integrante:** si votó por la unidad y por el celular, vale el
  último, y queda en el registro por dónde votó cada vez.
- **Presencia:** cuenta igual estar presente por la central (asistencia de la CCU), por el celular o
  las dos; nunca se cuenta dos veces.

## 1. Decisiones (2026-10-05)

| Tema | Decisión |
|---|---|
| Identidad | **Link personal + PIN propio** por integrante. Antes de contar para el quórum y poder votar, el operador o la presidencia lo **habilita** viéndolo en cámara. |
| Escala | Límite técnico **4096** remotos. El audio va siempre (casi no pesa). El **video se limita solo** según CPU, jitter, ping y ancho de banda. Hay 1 Gb/s simétrico disponible. |
| Audio y video de cada remoto | Son **entradas** de las secciones Audio y Video de siempre, **agrupadas** («Sesión remota») para que la interfaz no explote. Se les aplican filtros (LADSPA, CLAP) como a cualquier entrada. |
| Voz del remoto en la sala | Por una **entrada de la CCU**: se cablea en el panel de audio (una salida de la matriz → placa → entrada de línea de la central). Los presenciales lo oyen en los parlantes de sus bancas. |
| Quién da la palabra | **Los dos**: el operador (app, botonera, MCP) y la presidencia (página propia en una tablet). Queda registrado quién la dio. |

## 2. El audio de la sala

- La CCU tiene **una sola salida**: la mezcla de sala (todos los micrófonos abiertos). Entra a la app
  como **una** entrada, «Sala (central)».
- **Qué banca presencial habla** no sale del audio: sale de la CCU (aviso de micrófono). Con eso se
  mueve la cámara, el zócalo y «Seguir al micrófono». La app nunca abre ni cierra el audio de una banca
  presencial: lo hace la central.
- **Los remotos sí tienen una entrada cada uno** (su micrófono por WebRTC). La app abre la del que
  tiene la palabra.
- **La voz de los remotos a la sala:** una salida de la matriz, «A la central», con la suma de los
  remotos que tienen la palabra, va a la entrada de línea de la CCU.
- **Lo que escucha un remoto:** la mezcla de la sala (que ya trae la voz de los remotos por la CCU) sin
  su propia voz. Mientras no tiene la palabra es la misma mezcla para todos (se codifica una vez); el
  que habla recibe la suya, sin él (mix-minus, como en la sala de invitados).

> **A confirmar en la sala:** que la CCU tenga una entrada de línea libre y si la central devuelve esa
> entrada en su salida de sala (si sí, al remoto que habla hay que restarle el retardo de la vuelta).

## 3. Participantes y credenciales

- Cada **integrante del Cuerpo** puede tener una credencial de sesión remota: un link personal
  (`…/sesion/#i=<id>&t=<token>`) y un **PIN** de 6 dígitos. Se guarda solo el hash con sal
  (`CredentialStore`, como los aparatos de la botonera). Se puede revocar y regenerar.
- Entrar: link + PIN → la sesión del participante (token de un uso para el WebSocket, como hoy).
- **Estados:**
  - `conectado`: entró con link y PIN; se lo ve en el lobby de la sesión;
  - `habilitado`: el operador o la presidencia lo vio en cámara y lo habilitó → cuenta como **presente
    en línea** y puede votar;
  - `con_palabra`: tiene la palabra (su audio está abierto);
  - `desconectado`: se cayó; si vuelve dentro de un plazo, recupera `habilitado` sin volver a validar
    (configurable).
- Un integrante, **una** conexión: si entra de otro lado, la anterior se corta.
- Todo cambio de estado queda en el **registro de la sesión** (quién, cuándo, desde qué IP), que va
  anexado al acta.

## 4. La palabra: una sola fila

- Fila única de pedidos: los de la CCU (presenciales, cola RTS) y los de los remotos (botón «Pedir la
  palabra»), en orden de llegada. La fila presencial de la central sigue siendo la de la central; la
  app arma la vista unificada.
- **Dar la palabra:**
  - a un presencial → `MM_C_SET_MICRO_ON_OFF` en la CCU (como hoy);
  - a un remoto → se abre su entrada de audio (y su video pasa a «en el aire» para las escenas).
- La pueden dar el operador (app, botonera, MCP con guardas de aire) y la presidencia (página
  `…/presidencia/`, con su propia credencial). Cada acción registra quién la dio.
- El tiempo de uso de la palabra cuenta igual para remotos y presenciales (Orden del día).

## 5. Votación y asistencia

- **Asistencia:** los remotos `habilitado` cuentan como presentes en línea; el padrón ya separa
  `presenciales` y `enLinea`. Quórum suma las dos fuentes.
- **Lo que le llega al integrante:** el **tema a votar** (y los que vienen, del orden del día); al
  abrirse la votación, un **cartel rojo de votación** a pantalla completa con el tema, las opciones y
  la **cuenta regresiva** del tiempo para votar (el de la central, `VT_C_GET_TIMER_VALUES`, o el que
  fije la app); la confirmación de su voto; al cerrar, el resultado.
- **Votación:** cuando se abre una votación (en la central o en la app), a los remotos habilitados les
  aparece la boleta con las mismas opciones. Pueden cambiar el voto mientras esté abierta (como en la
  unidad). Al cerrar:
  - resultado = voto nominal de la CCU (presenciales) + voto nominal de los remotos;
  - va a Quórum como resultado oficial y al acta con su hash, con el origen de cada voto
    (`central` / `remoto`).
- Si un integrante figura en las dos (presente en la sala y conectado), vale el de la sala y el remoto
  queda anotado como observación.

## 6. Entradas agrupadas en Audio y Video

- **Audio:** cada remoto es una entrada del panel de audio (`Sesión remota / Banca 12 · Nombre`), con
  su cadena de filtros (los de GStreamer, incluido `ladspa`, y los CLAP). Los filtros se pueden poner
  **al grupo** (todos los remotos) o a uno. El grupo se pliega y muestra solo los que hablan o piden la
  palabra.
- **Video:** cada remoto es una entrada del grafo de video (sub-grafo propio), agrupada igual. Las
  escenas usan «el remoto que tiene la palabra» como una entrada virtual, sin tener que elegir a mano.
- **No se crea todo de golpe:** con 4096 remotos, las entradas existen en el modelo pero solo se
  **instancian** (decodificador, filtros) las que hacen falta: los que tienen la palabra, los que la
  pidieron y las miniaturas que alguien está mirando.

## 7. Escala (hasta 4096)

Lo que cambia respecto de la sala de invitados (hoy hasta 64 lugares, `ipc/LugaresSala.hpp`, con los canales de memoria compartida creados al subir el cupo):

- **Lugares dinámicos.** Las colas de memoria compartida se crean y liberan a pedido (audio y video
  por separado), no un número fijo al arrancar.
- **Audio:**
  - el cliente manda Opus siempre, con el micrófono cortado en el cliente mientras no tiene la palabra
    (y DTX: el silencio casi no pesa);
  - el gateway **solo decodifica** a los que tienen la palabra o la pidieron;
  - la vuelta (la mezcla de la sala) se codifica **una vez** y se manda igual a todos; solo los que
    hablan reciben la suya.
- **Video:**
  - la pantalla de sala / programa se codifica **una vez** en varias calidades y se reparte (como un
    SFU), no un codificador por participante;
  - de cada remoto llega simulcast (alta, media, baja); el gateway pide la capa que necesita: alta para
    el que habla, baja para miniaturas, nada para el resto.
- **El regulador** mide cada 2 s CPU, jitter, pérdida, RTT y ancho de banda (de `GetStats`, que ya
  existe para los invitados) y baja escalones: menos miniaturas, menos cuadros, menos resolución, solo
  audio para los que no se miran. El que tiene la palabra es lo último que se toca. Lo que hace se ve
  en Salud y se puede fijar a mano.
- **Red:**
  - el túnel de Cloudflare lleva la señalización; los medios van por UDP directo (puerto abierto o el
    WireGuard propio);
  - hace falta **TURN** (hoy no hay) para los que están detrás de redes que no dejan pasar UDP: un TURN
    propio en el gateway o uno externo configurable.
- **Límites de una máquina:** 1 Gb/s simétrico alcanza de sobra para el audio de 4096 (unos 30 kb/s
  cada uno) y la vuelta en baja; el techo real es la CPU del cifrado (DTLS/SRTP) y del reparto de video.
  El regulador lo protege.

## 8. Páginas web

- `…/sesion/` — la del integrante: su imagen, la pantalla de la sala, «Pedir la palabra», la boleta
  cuando hay votación, quién habla, la fila, el punto del orden del día, aviso de conexión.
- `…/presidencia/` — la de la presidencia: fila unificada, dar y quitar la palabra, habilitar a los
  que entran (con su imagen), abrir y cerrar votación si así se decide.
- Todo en castellano rioplatense, para tablet y celular.

## 9. Fases

| Fase | Qué | Depende de |
|---|---|---|
| **R1** | Credenciales por integrante (link + PIN), borde propio en el gateway (`Sesion.hpp`), página `…/sesion/` con su cámara (foto cada 1 s mientras espera, para habilitarlo), estados conectado/habilitado, habilitar desde la app y por MCP, presentes en línea → padrón y Quórum, registro | — |
| **R2** | Fila unificada (CCU + remotos), dar la palabra desde la app/botonera/MCP y desde `…/presidencia/`, abrir el audio del remoto, tiempo de palabra | R1 |
| **R3** | Audio y video por WebRTC hacia entradas agrupadas en Audio y Video (con filtros), «el remoto con la palabra» como entrada virtual, salida «A la central», la vuelta de la sala | R1 |
| **R4** | Temas a votar, cartel rojo con cuenta regresiva, boleta remota, resultado combinado CCU + remotos → Quórum y acta | R1, R2 |
| **R5** | Escala: lugares dinámicos, decodificar solo lo necesario, vuelta codificada una vez, simulcast, regulador, TURN | R1–R3 |
| **R6** | Pruebas de carga (clientes simulados) y prueba real en la sala | todo |

Cada fase con su GUI, sus herramientas MCP y su prueba sin equipo (clientes simulados).

## 10. Estado

- **R1 hecha (2026-10-05):**
  - credenciales por integrante (link + PIN, o usuario y contraseña);
  - borde propio en el gateway (`src/gateway/Sesion.{hpp,cpp}`, prueba `test_sesion_borde`);
  - página `assets/web/sesion/`;
  - `asamblea/SesionRemota` (prueba `test_sesion_remota`);
  - habilitar viendo la cámara;
  - presentes sumados a Quórum sin contar dos veces;
  - registro anexado al acta.
- **Recinto:**
  - hemiciclo, grilla o lista;
  - la cara en vivo de los conectados (o la foto del Cuerpo);
  - habilitar desde la banca.
- **Conexiones remotas:** usuario, link y PIN, contraseña, datos de contacto y accesos de la consola.
- **R2 hecha:**
  - fila única (`Bancas`): el celular pide la palabra;
  - al remoto se le da la palabra sin mover cámaras;
  - tiempo configurable con avisos al operador, al celular y a la unidad de la central.
- **R4 hecha:**
  - cartel rojo con tema, opciones y cuenta regresiva;
  - sigue a la votación de la central o se abre sin ella;
  - a Quórum va un voto por integrante, con su origen y la observación del doble voto;
  - los próximos temas aparecen en el celular.
- **Consola de la presidencia hecha** (`assets/web/presidencia/`, §8):
  - recinto con caras, habilitar, fila con 🎙 «apretar para hablar», el que habla con su anillo de
    tiempo, votación con barras;
  - el programa en vivo (la misma página de `/cast/`).
  - El video y la charla van por WebRTC directo entre la consola y el navegador del integrante; el
    gateway solo pasa la señalización.
  - Probado con Chrome sin ventana: la conexión llega a «connected». El video real no se pudo probar
    ahí, porque ese Chrome no da cámara.
- **R3 hecha (2026-10-05):**
  - **Cómo entra:** cuando la app le da la palabra a un remoto, le avisa al gateway (`POST /api/v1/sesion/medios {id}`). El celular de ese integrante abre un WebRTC propio hacia el gateway (`medios-oferta` / `medios-ice`, solo envía) con su micrófono y su cámara.
  - **El gateway** lo recibe en una sesión del pool con la ruta «Remoto» y lo publica en `BA.Audio.Remoto.1` / `BA.Remoto.1`. Hay dos canales: el que tiene la palabra va por el 1 y, en un debate entre dos remotos, el otro por el 2 (`BA.Remoto.2`, «Remoto con la palabra 2»); un tercero saca al más viejo.
  - **En la app** son dos entradas:
    - «Remoto con la palabra (audio)»: global, suena en la sala y en la transmisión, y se le pone el rack de filtros en Audio como a cualquier micrófono;
    - «Remoto con la palabra» (video): oculta a pantalla completa en la primera escena, y se usa en cualquier escena.
  - **«A la sala»:** en la Matriz, el cruce de «Remoto con la palabra (audio)» a una salida virtual con el dispositivo que va a la sala o a la entrada de la central.
  - Así el celular es **micrófono** en el recinto si se cae la central.
  - Al terminar la palabra (o si se desconecta) se cierra.
  - **Probado de punta a punta** con Chrome sin ventana y cámara/micrófono falsos: entra con link y PIN, se habilita y `banca_al_aire` le da la palabra; el gateway acepta la oferta y conecta; en la app «Remoto con la palabra» muestra la imagen del celular y el audio se mueve (−58 a −33 dBFS); `recinto_orador_terminar` cierra los medios y el aviso del celular.
  - No hay vuelta de audio por este camino: el programa lo ve en su página. Un remoto de afuera que necesite oír la sala usa la consola o la página.
- **Hecho el 2026-10-08:**
  - vuelve con la palabra el que se reconecta (con el tiempo que le queda) y la entrada no se queda con la cara del anterior;
  - en el celular: «Yo» (su cámara), la salida principal u otra escena (hasta dos canales, `sesion_vistas`), con su cámara siempre en un recuadro;
  - apretar para hablar desde la app (`sesion_hablar`) y desde la banca en la consola de la presidencia;
  - mostrar contenido mientras habla: pantalla (compu) u otra cámara (celular) → «Remoto con la palabra — contenido»;
  - silenciar a todos y «hablo yo» de la presidencia (también los remotos; lo mismo al apretar prioridad en la unidad);
  - debate entre dos (sala-sala, sala-remoto, remoto-remoto) con escena por tipo y turnos (`debate_*`).
- **Falta:**
  - **R5:** escala hasta 4096, regulador de video y TURN. Sin TURN, el video directo y el WebRTC
    andan en la red de la sala, pero no siempre desde afuera.
  - **R6:** prueba real con celulares por HTTPS.
