# Capacidades que no están en el programa

*Análisis de arquitectura: cómo hacer que el sistema pueda lo que todavía no
sabe hacer, y que lo escriba el asistente.*

---

## La pregunta, dicha con precisión

«Poder pedirle a la LLM que grabe cada entrada por separado» y «poder agregar
una tarjeta nueva a la interfaz» **no son el mismo problema**, aunque suenen
parecido. Confundirlos es lo que hace que los sistemas de plugins salgan mal.

Hay dos cosas distintas:

| | |
|---|---|
| **Automatización** | Combinar de una forma nueva cosas que el sistema **ya sabe hacer**. |
| **Capacidad** | Hacer algo que el motor **no sabe hacer de ninguna manera**. |

El asistente puede resolver la primera **solo y para siempre**, sin que nadie
compile nada. La segunda **siempre** necesita que alguien agregue una pieza
abajo: ningún sistema de plugins convierte «libobs no tiene una salida por
fuente» en «sí la tiene».

Grabar cada entrada por separado cae del lado de **capacidad**: hoy el motor
tiene una sola salida de grabación (`recordOutput_`), y grabar ocho fuentes son
ocho salidas, ocho codificadores y ocho archivos. Eso es C++, no es un plugin.

Por eso la propuesta no es «un sistema de plugins», sino **cuatro niveles**, de
los cuales conviene construir tres y **no** construir el cuarto.

---

## Lo que ya hay, y que es más de lo que parece

Antes de inventar nada, conviene ver con qué se cuenta:

- **Superficie MCP**: unas cien herramientas con esquema, ayuda y categorías.
  El asistente ya maneja el motor, las escenas, el audio, la sala, el guion,
  los gráficos y el registro. **Es una API de automatización completa que ya
  existe.**
- **El asistente es un CLI con acceso al disco**: ya escribe archivos. Los
  gráficos nuevos los escribe él, en `Graficos/<nombre>/`, y el programa los
  levanta sin recompilar nada. **Ya hay un precedente de extensión que funciona.**
- **Vínculos** (`Config/vinculos.json`): reglas declarativas que el programa
  ejecuta solo. **Ya hay un precedente de automatización persistente.**
- **El launcher ya sabe recuperarse**: verifica el manifest, reinicia con
  backoff, y con más de cinco caídas en dos minutos prende el *circuit breaker*
  y relanza A con `--safe-mode`.
- **El latido** (`BA.Health`): si A se cuelga —no se muere, se cuelga— el
  launcher lo ve porque deja de latir.

Sobre esas cuatro cosas se apoya todo lo que sigue. **No hay que empezar de
cero: hay que conectar lo que ya está.**

---

## Los cuatro niveles

### Nivel 1 — Recetas (automatización declarativa) · *bajo costo, sin riesgo*

Un archivo JSON que describe **cuándo** y **qué**, y que el programa ejecuta.
Es exactamente lo que ya son los vínculos, generalizado:

```json
{ "cuando": { "escena": "Entrevista" },
  "hacer": [ { "tool": "grafico_mostrar", "args": { "fuente": "Zócalo", "…": "…" } },
             { "tool": "audio_volumen",   "args": { "fuente": "Música", "db": -18 } } ] }
```

- **No ejecuta código**: es una lista de llamadas a herramientas que ya existen,
  con los mismos permisos y las mismas validaciones.
- Un error de una receta es un error de una herramienta: ya sabemos mostrarlo.
- La escribe el asistente en castellano y queda en disco, revisable.

**Qué cubre:** «cuando pase a la escena X, poné el zócalo y bajá la música».
**Qué NO cubre:** nada que el motor no sepa hacer.

### Nivel 2 — Acciones (un botón con nombre) · *bajo costo, sin riesgo*

Una receta con un nombre, un ícono y un lugar donde aparecer. Es el mismo
mecanismo que los `acciones` que ya traen los gráficos, pero para todo el
programa: el asistente escribe la acción, y aparece un botón.

Esto es **la mitad barata de «agregar una tarjeta a la interfaz»**, y resuelve
el 80 % de los casos sin cargar una línea de código ajeno.

### Nivel 3 — Tarjetas (QML desde el disco) · *costo medio, riesgo acotado*

```
Extensiones/<id>/
├── extension.json    nombre, qué necesita, si está activa
├── tarjeta.qml       la interfaz
└── recetas.json      lo que sabe hacer
```

Hoy el QML viaja **compilado adentro del binario** (`qrc:/qml/...`), así que
esto es lo primero que hay que cambiar: un `Loader` que lea de disco, con un
contexto **acotado a propósito** (el `Studio`, y un objeto `Extension` con lo
que esa extensión declaró necesitar). No es un sandbox de verdad —QML con
acceso a `Studio` puede hacer bastante—, y **eso hay que decirlo en voz alta**:
el modelo de amenaza acá no es el código malicioso (es tu máquina, tu drive,
tu asistente), es **el código roto**. Todo el diseño apunta a sobrevivir a lo
segundo, no a defenderse de lo primero.

**El riesgo real de QML de terceros no es que explote: es que se cuelgue.** Un
`while(true)` en un binding congela el hilo de la interfaz, y ahí no hay
`try/catch` que valga. Para eso está el nivel 0 de abajo.

### Nivel 4 — Plugins nativos (`.so`/`.dll` cargados con dlopen) · **no hacerlo**

Es la única forma de agregar una capacidad de verdad desde afuera, y es la que
**no** conviene:

- Un plugin nativo comparte el espacio de memoria: un puntero suelto no se
  puede contener, tumba el proceso que está al aire y no hay manera de saber
  de quién fue.
- Obliga a congelar una ABI de C++ (o a exponer una de C, que es peor de usar)
  y a mantenerla entre versiones, en tres plataformas.
- El asistente escribiendo C++ que se carga en el proceso del aire, sin
  revisión, es exactamente el escenario que uno no quiere en vivo.
- Y no hace falta: **cuando falta una capacidad, lo correcto es agregarla como
  herramienta MCP de primera clase**, con su prueba, y a partir de ahí los
  niveles 1 a 3 la componen. Es más trabajo la primera vez y muchísimo menos
  para siempre.

> **La regla:** si el asistente necesita algo que no existe, no se lo inventa
> con un plugin: **te lo pide a vos**, y se agrega abajo. Que el sistema sepa
> decir «esto no lo sé hacer, hace falta una herramienta nueva» vale más que un
> mecanismo para improvisarlo.

---

## El arranque a prueba de fallos

Esto es lo que hace que lo de arriba sea aceptable en un equipo que sale al
aire. Hay **tres líneas de defensa**, y las dos últimas ya existen.

### 1. El diario de arranque (hay que escribirlo)

Antes de cargar cada extensión se escribe, **y se sincroniza a disco**:

```json
{ "arranque": 47, "cargando": "mi-tarjeta", "desdeMs": 1758546202145 }
```

Cuando la interfaz **dibujó su primer cuadro**, se escribe `{"arranque": 47,
"estado": "ok"}`. Al arrancar:

- Si el diario quedó en `cargando: X` **sin** su `ok`, esa extensión se llevó
  puesto el arranque anterior: **se desactiva sola**, se anota en el registro
  con su nombre, y el programa arranca sin ella avisando qué pasó.
- Si el mismo id rompe dos arranques seguidos, queda desactivado y marcado, no
  se reintenta más.

Es cómo lo hacen los navegadores, y funciona igual de bien para un cuelgue que
para una caída: **lo que se mide es que el arranque haya terminado**, no que el
proceso siga vivo.

### 2. El circuit breaker del launcher (ya existe)

Más de cinco caídas de A en dos minutos → `--safe-mode`. **Modo seguro pasa a
significar también «sin ninguna extensión»**, que hoy no significa. Un cambio
de dos líneas que convierte lo que ya hay en la red de contención de esto.

### 3. El latido (ya existe)

Si A se **cuelga** en vez de morirse, deja de latir en `BA.Health` y el
launcher lo reinicia. Combinado con el diario, un `while(true)` dentro de una
tarjeta termina con esa tarjeta desactivada y el programa andando, sin que
nadie toque nada.

### Detalles que hay que acordarse

- `Extensiones/` va en la lista de exclusiones del manifest (junto a `Config/`,
  `Credenciales/`, `Eventos/`): si no, cada extensión nueva rompe la
  verificación de integridad.
- `--update` no las toca, igual que las credenciales.
- Una extensión **declara qué necesita** en su `extension.json`. Aunque hoy eso
  no se pueda hacer cumplir del todo, sirve para dos cosas que valen: que el
  operador vea qué pidió antes de activarla, y que el día que se quiera hacer
  cumplir, el dato ya esté escrito.

---

## Grabar cada entrada por separado: el ejemplo, resuelto

Sirve para ver cómo se reparte el trabajo entre los niveles:

| Pieza | Nivel | Trabajo |
|---|---|---|
| Una salida de grabación **por fuente**, con su codificador y su archivo | **4 → C++** | Nuevo en `BroadcastEngine`: N `obs_output` con `obs_output_set_media` sobre un encoder atado a esa fuente. Ojo con el costo: ocho codificaciones simultáneas no las hace cualquier máquina, así que va con tope y con aviso. |
| `grabacion_individual` como herramienta MCP (arrancar, parar, listar, con ayuda) | **4 → C++** | Una vez. A partir de acá el asistente ya «sabe cómo hacerlo». |
| «Grabá todas las entradas cuando arranque el evento» | **1 → receta** | La escribe el asistente. |
| Un botón «Grabar todo por separado» | **2 → acción** | Ídem. |
| Una tarjeta con una fila por entrada, su tiempo y su tamaño | **3 → tarjeta** | QML de disco. |

**Lo único que hay que programar en C++ es lo primero.** Todo el resto lo
escribe el asistente después, y sin tocar el programa. Ese reparto es la
prueba de que el diseño está bien: el trabajo caro se hace una vez y abajo.

---

## Qué conviene construir, y en qué orden

1. **El diario de arranque y que modo seguro signifique «sin extensiones».**
   Primero la red, después el trapecio. Es chico y hace segura toda la etapa
   siguiente.
2. **Recetas y acciones (niveles 1 y 2).** Es donde está casi todo el valor y
   no tiene riesgo: no ejecuta código, solo compone herramientas que ya están
   probadas. Generalizar lo que ya hacen los `vinculos` y los `acciones`.
3. **Grabación por entrada** como capacidad de primera clase, con su
   herramienta MCP. Es una función que un equipo de emisión quiere de todos
   modos, y de paso es el caso de prueba de los niveles 1 y 2.
4. **Tarjetas QML desde disco (nivel 3).** Recién acá, con la red puesta y con
   dos ejemplos reales que ya funcionan como referencia.
5. **Nunca el nivel 4.** Cuando falte una capacidad, se agrega abajo.

## Lo que hay que aceptar

- **Las tarjetas no van a estar aisladas de verdad.** Se elige recuperarse en
  vez de defenderse, porque el que escribe es tu asistente en tu máquina. Si
  algún día una extensión viniera de afuera, esto hay que rehacerlo.
- **Una extensión puede degradar el rendimiento sin romper nada**, y eso el
  diario no lo agarra. Mitigación honesta: medir el tiempo de carga de cada
  una y mostrarlo; lo que tarda se ve.
- **Se agranda la superficie que hay que mantener.** Cada herramienta MCP nueva
  es una promesa hacia las recetas que alguien escribió. Conviene versionarlas
  y no romperlas en silencio.
