# Generalizar la estructura para que existan los plugins

*Análisis de arquitectura: qué forma tiene que tomar la aplicación para que una
capacidad nueva —incluida una que se meta en la interfaz— sea una cosa que se
declara, y no una cosa que hay que programar adentro.*

---

## El hallazgo: el patrón ya está inventado, tres veces

Antes de diseñar nada conviene mirar lo que el programa ya hace, porque **la
respuesta ya está escrita en tres lugares distintos, sin nombre**:

| | Describe | Se genera solo |
|---|---|---|
| **Una fuente de libobs** | `obs_properties`: cada fuente declara sus opciones con tipo y etiqueta | El formulario entero (`PropertiesModel` → cualquier fuente de cualquier plugin se configura **sin código a medida**) |
| **Un gráfico** | `grafico.json`: `parametros[]` con clave, título, tipo, por defecto y **ayuda** | El formulario del editor, los campos del cue del guion, el esquema de la herramienta MCP y los botones que ofrece |
| **Un cue del guion** | `tipo` + `datos` + `queLeFalta()` | Los campos del editor, la validación previa y la ayuda del MCP |

Las tres son **la misma idea**: *una cosa declara qué parámetros tiene, con tipo
y con ayuda, y de esa declaración salen solos el formulario, el esquema MCP, la
validación y la documentación.*

**Generalizar la estructura es ponerle nombre a eso y tenerlo una sola vez.** No
hace falta inventar un sistema de plugins: hace falta **extraer el patrón que ya
funciona** y declarar que cualquier cosa que lo cumpla es una extensión.

> Dicho de otra manera: hoy el programa sabe hospedar fuentes de terceros (lo
> hace todos los días, con los plugins de obs) y sabe hospedar gráficos de
> terceros (los escribe el asistente). Lo que **no** sabe es hospedar *cualquier
> otra cosa*, porque ese contrato nunca se escribió aparte.

---

## El contrato, en una página

Una extensión es una carpeta con un **descriptor**. Todo lo demás es opcional:

```
Extensiones/<id>/
├── extension.json      ← el descriptor: lo único obligatorio
├── tarjeta.qml         ← si se mete en la interfaz
└── recetas.json        ← si hace algo
```

```json
{
  "id": "grabacion-por-entrada",
  "nombre": "Grabación por entrada",
  "descripcion": "Graba cada cámara a su propio archivo.",
  "necesita": ["fuentes", "grabacion"],

  "parametros": [
    { "clave": "carpeta", "titulo": "Dónde guardar", "tipo": "ruta",
      "porDefecto": "Recordings/individuales",
      "ayuda": "Un archivo por entrada. Ocho cámaras son ocho codificaciones: mirá la CPU." }
  ],

  "presenta": [ { "punto": "tarjeta.dashboard", "vista": "tarjeta.qml", "titulo": "Grabación por entrada" } ],

  "acciones": [ { "titulo": "Grabar todo", "receta": "grabar-todo" } ]
}
```

De ese archivo salen **sin escribir una línea más**: el formulario de
configuración, el esquema de la herramienta MCP, la validación de lo que falta,
la entrada en la ayuda que lee el asistente, y el lugar donde aparece.

Esa última línea —`presenta`— es la pieza nueva, y es de la que depende todo lo
demás.

---

## Meterse en la interfaz: los puntos de inserción

Hoy la interfaz es catorce páginas fijas compiladas adentro del binario
(`qrc:/qml/...`). Para que algo de afuera se meta hace falta que el **anfitrión
declare dónde se puede meter**. Un plugin no «modifica la interfaz»: **ocupa un
lugar que la interfaz ofrece**. La diferencia es todo: lo primero no se puede
sostener entre versiones, lo segundo sí.

Los puntos de inserción, del más barato al más caro:

| Punto | Qué es | Costo |
|---|---|---|
| `accion.<contexto>` | Un botón con un pedido ya escrito | **Ya existe** (los `acciones` de los gráficos, el zócalo del asistente) |
| `tarjeta.dashboard` | Una tarjeta en el dashboard | **Casi existe** (ver abajo) |
| `panel.<pagina>` | Un panel adentro de una página | Medio: hay que marcar los lugares |
| `pagina` | Una página entera en el riel | Medio: el riel ya está agrupado y se desplaza |

### La tarjeta del dashboard ya es dinámica

Esto es lo segundo que conviene mirar antes de diseñar nada. El dashboard hoy
hace esto:

```qml
Loader {
    source: "../cards/" + kind.charAt(0).toUpperCase() + kind.slice(1) + "Content.qml"
}
```

Es decir: **la tarjeta se resuelve por nombre, en tiempo de ejecución, y su
disposición ya se guarda en `Config/ui.json`.** El botón «+ Tarjeta» y el encaje
magnético ya están hechos. Que un `kind` que empiece con `ext:` resuelva a
`Extensiones/<id>/tarjeta.qml` en vez de a `qrc:` es **un cambio de una línea en
esa expresión**, más el `Loader` mirando el estado de error.

No hay que construir un sistema de tarjetas: **hay que dejar que el que ya
existe lea de otro lado.** Ese es el punto de entrada, y por eso es el primero
que conviene hacer.

### Lo que una tarjeta ve

Un contexto acotado y explícito: el `Studio` (lo que ya ven todas las páginas) y
un objeto `Ext` con los parámetros de esa extensión, sus recetas y su almacén.
**No es un sandbox de verdad**, y hay que decirlo en voz alta: QML con acceso a
`Studio` puede hacer bastante. El modelo de amenaza acá no es el código
malicioso —es tu máquina, tu drive, tu asistente— sino **el código roto**. Todo
el diseño apunta a sobrevivir a lo segundo. Si algún día una extensión viniera
de afuera, esto hay que rehacerlo.

---

## Publicar toda la superficie de libobs como MCP: no, y hay número

La pregunta era si publicar entera la superficie de libobs y si eso satura el
contexto. Satura, y ya está cerca:

```
202 herramientas · 128 KB de listado · ~32.000 tokens EN CADA PEDIDO
```

Pero el problema no es el tamaño: es que **enumerar es la estrategia
equivocada**, y el propio programa ya lo demuestra. Estas cuatro herramientas:

```
source_types · source_properties · source_set_property · source_update
```

cubren **todas** las fuentes de **todos** los plugins de obs, las que hay hoy y
las que se instalen mañana, porque libobs **se describe a sí mismo**
(`obs_properties`) y esas herramientas devuelven esa descripción. Cuatro
herramientas, cobertura total, costo fijo.

Enumerar la API de libobs serían cientos de herramientas, decenas de miles de
tokens en cada pedido, y **menos** cobertura: cada plugin nuevo quedaría afuera
hasta que alguien le escribiera las suyas.

> **La regla: reflexión, no inventario.** Se publica la *forma* del sistema y
> una manera de preguntarle por los detalles, no la lista de todo lo que hay.

Eso tiene tres consecuencias prácticas:

1. **Las extensiones se publican como UNA familia reflexiva**, no como una
   herramienta por extensión: `ext_listar`, `ext_describir`, `ext_ejecutar`,
   `ext_configurar`. Cien extensiones cuestan lo mismo que una en el contexto.
2. **Hay lugar para limpiar.** Varias de las 202 son familias que se pueden
   colapsar en genérica + descubrimiento, bajando el contexto *y* subiendo la
   cobertura. Conviene medirlo antes y después.
3. **Las herramientas con nombre no desaparecen**, y esto es lo importante: una
   herramienta genérica **no puede avisar**. `source_set_property` no sabe que
   cambiar el códec al aire corta la transmisión; `stream_start` sí, y por eso
   su ayuda lo dice. **Lo que se escribe a mano no es la función: es el
   criterio.** Genérico para todo lo que es mecánico; con nombre para lo que
   sale caro si se hace mal.

---

## El contexto por enfoque: analizar, armar el juego, disparar

Lo de arriba baja el costo de *agregar* herramientas. Falta lo otro: que el
asistente no cargue las doscientas cuando va a hacer una sola cosa. La forma es
en tres tiempos —**se analiza qué hay que hacer, se arma el juego de
herramientas, y recién ahí se dispara el agente con ese juego y con los
ejemplos**— y encaja con lo que ya hay, porque **la jerarquía ya está escrita**:
cada herramienta declara su `category` y cada una tiene sus `details`. Lo único
que pasa es que `tools/list` las manda todas, siempre.

### Cómo se reparte hoy

```
TOTAL          202 herramientas   ~32.000 tokens en cada pedido
audio           27 herramientas     6.279
youtube         15                  2.195
facebook        13                  1.689
capture          8                  1.686
escenas          9                  1.395
…                                  34 familias en total
```

Un juego enfocado —el núcleo, el mapa y una o dos familias— cuesta **alrededor
de 4.000 tokens**: **un octavo**. Y no es solo plata: doscientas herramientas
también son doscientas formas de elegir mal.

### Los tres tiempos

1. **Núcleo, siempre puesto** (~10 herramientas): estado, registro, captura de
   pantalla, avisos, `ayuda` y `enfocar`. Es lo que hace falta para entender qué
   está pasando y para pedir el resto.
2. **`enfocar(["graficos", "evento"])`**: el servidor cambia su lista de
   herramientas a ese juego y avisa con `notifications/tools/list_changed`, que
   es parte del protocolo MCP. Junto con las herramientas viaja **la ayuda de
   esas categorías, con ejemplos**, que es la otra mitad del pedido: un juego de
   herramientas sin ejemplos se usa mal.
3. **Trabajar.** Y al terminar, volver al núcleo.

### Las tres cosas que hay que hacer bien

**El mapa va siempre, y es lo más importante del diseño.** Un juego reducido sin
un índice de lo que existe es peor que la lista completa: el agente concluye
«esto no se puede hacer» cuando la herramienta está a una categoría de
distancia. El núcleo lleva **una línea por categoría** —nombre, para qué sirve,
cuántas herramientas tiene—: treinta y cuatro renglones, unos 400 tokens, y
nunca se puede no saber que algo existe.

**El enfoque lo propone la pantalla.** El programa ya sabe en qué página está
parado el operador —el zócalo del asistente arma el contexto con eso— así que
la página **preselecciona** el enfoque: en Gráficos, `graficos`; en el Guion,
`evento`; en Audio, `audio`. Da bien casi siempre y no cuesta nada. El agente
puede cambiarlo con `enfocar` cuando se equivoque.

**Cambiar de enfoque invalida la caché del prompt.** Es el costo real y hay que
decirlo: cada cambio obliga a rearmar la caché de esa conversación. Con dos o
tres cambios por tarea, gana por lejos frente a 32.000 tokens en *cada* pedido;
con un agente que cambia de enfoque cada dos mensajes, **pierde**. De ahí que
convenga que el enfoque sea grueso (categorías, no herramientas sueltas) y que
lo proponga la pantalla, que cambia poco.

### Por qué esto y las extensiones son el mismo problema

Una extensión también declara a qué categoría pertenece. Entonces no agrega
herramientas al montón: **agrega una rama al árbol**, que solo se carga cuando
alguien enfoca ahí. Cien extensiones instaladas cuestan, en el contexto de
siempre, un renglón cada una en el mapa.

Es la misma regla de antes vista desde el otro lado: **se publica la forma del
sistema, y los detalles se piden cuando hacen falta.**

---

## El arranque a prueba de fallos

Tres líneas de defensa, y **dos ya existen**:

**1. El diario de arranque** *(hay que escribirlo)*. Antes de cargar cada
extensión se anota en disco, sincronizado, `{"arranque": 47, "cargando": "<id>"}`;
cuando la interfaz **dibujó su primer cuadro** se marca `ok`. Al arrancar, si el
diario quedó en `cargando: X` sin su `ok`, esa extensión se llevó puesto el
arranque anterior: **se desactiva sola**, se anota en el registro con su nombre y
el programa arranca sin ella, avisando. Lo que se mide es **que el arranque haya
terminado**, así que sirve igual para una caída que para un cuelgue.

**2. El circuit breaker** *(ya existe)*. Más de cinco caídas de A en dos minutos
→ `--safe-mode`. Solo falta que modo seguro signifique además **«sin ninguna
extensión»**: dos líneas.

**3. El latido** *(ya existe)*. Si A se **cuelga** en vez de morirse deja de
latir en `BA.Health` y el launcher lo reinicia. Un `while(true)` en un binding no
mata el proceso, lo congela, y no hay `try/catch` que sirva: el latido es lo
único que lo agarra.

Detalles que hay que acordarse: `Extensiones/` va en la lista de exclusiones del
manifest (junto a `Config/`, `Credenciales/`, `Eventos/`), o cada extensión nueva
rompe la verificación de integridad; y `--update` no las toca.

---

## Dónde sigue haciendo falta C++

La generalización no borra este límite, y conviene tenerlo dicho: una extensión
puede **componer** lo que el sistema sabe hacer y **presentarlo**, pero no puede
inventar una capacidad que el motor no tiene. Grabar cada entrada por separado
son N salidas de libobs con N codificadores: eso se agrega abajo, como
herramienta MCP de primera clase, y **recién entonces** una extensión lo compone,
le pone una tarjeta y lo ata al guion.

De ahí la regla que conviene dejar escrita en la ayuda del asistente: **si te
falta una capacidad, no la improvises: pedila.** Que el sistema sepa decir «esto
no lo sé hacer, hace falta una herramienta nueva» vale más que un mecanismo para
improvisarlo, porque lo improvisado es lo que se rompe en vivo.

Y por eso mismo **no** conviene un nivel de plugins nativos (`.so`/`.dll`):
comparten memoria con el proceso que está al aire, obligan a congelar una ABI de
C++ en tres plataformas, y no hacen falta si la superficie MCP crece a
propósito.

---

## Qué construir, y en qué orden

1. **Extraer el descriptor.** Un solo módulo que lea «parámetros con tipo, ayuda
   y valor por defecto» y genere formulario + esquema MCP + validación. Hoy eso
   está escrito tres veces; que sea uno solo es el trabajo de fondo y el que
   habilita todo lo demás. *No cambia nada visible: es la condición para que el
   resto sea barato.*
2. **El diario de arranque y modo seguro sin extensiones.** Primero la red.
3. **Tarjetas en el dashboard desde disco.** El `Loader` ya resuelve por nombre:
   es el punto de inserción más barato que existe y el que prueba el contrato
   entero de punta a punta.
4. **El mapa de categorías y `enfocar`.** Es independiente de todo lo demás,
   se puede hacer hoy y baja el contexto de 32.000 a unos 4.000 tokens. Empezar
   por el mapa: aunque nunca se enfoque nada, un índice de lo que existe ya
   mejora las decisiones del asistente.
5. **La familia reflexiva `ext_*` en MCP**, y de paso medir cuánto contexto se
   recupera colapsando las familias que hoy están enumeradas.
6. **Recetas** (composición declarativa de herramientas) y el resto de los
   puntos de inserción, a medida que hagan falta.

## Lo que hay que aceptar

- **Las tarjetas no van a estar aisladas de verdad.** Se elige recuperarse en
  vez de defenderse.
- **Una extensión puede degradar el rendimiento sin romper nada**, y el diario
  no lo agarra. Mitigación honesta: medir cuánto tarda en cargar cada una y
  mostrarlo.
- **Cada punto de inserción es una promesa** hacia las extensiones que alguien
  escribió. Conviene abrir pocos, y no romperlos en silencio.
