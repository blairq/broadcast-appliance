# Ventanas y monitores

Un puesto de televisión se arma repartiendo ventanas entre dos o tres
monitores: el programa acá, el guion allá, el registro abajo. Y se arma **una
vez** y se repite igual antes de cada evento, no a mano cada día.

Todo esto está en MCP, así que se puede pedir hablando y guardar como receta:

| herramienta | para qué |
|---|---|
| `ventana_listar` | qué hay abierto, en qué monitor, de qué tamaño y cómo (normal, maximizada, pantalla completa) |
| `ventana_mover` | a otro monitor, a una zona, a un tamaño exacto, a pantalla completa |
| `ventanas_ordenar` | repartir todo de una, sin que quede nada encimado |
| `ventana_cerrar` | cerrar una suelta |
| `monitor_identificar` | poner en cada pantalla un cartel grande con su número y su nombre |

Las ventanas se nombran con un id estable que les pone la interfaz —
`principal`, `pagina:<id>`, `salida:<n>` — y no por su título, que es para leer
y se repite.

## Por zonas, no por píxeles

`zona: "izquierda"`, `"tercio-der"`, `"inf-izq"`, `"completa"`. Calcular
x/y/ancho/alto contra la geometría de cada monitor es justo donde se falla —con
monitores de distinto tamaño, o con el área útil recortada por un dock— y el
error se ve como una ventana a medio salir de la pantalla. Los píxeles siguen
estando para cuando de verdad hagan falta.

## La interfaz corre sobre X11, a propósito

**En Wayland un programa no puede decir dónde va su ventana**: ubicarlas es del
compositor. Y lo peor no es que no se pueda: es que **pedirlo no falla**. Qt
hasta informa la pantalla nueva, la ventana se queda donde estaba, y el
operador ve botones que no hacen nada.

Por eso el launcher arranca la interfaz con `QT_QPA_PLATFORM=xcb`
(`src/launcher/ChildProcess.cpp`). Sobre X11 —XWayland si la sesión del sistema
es Wayland— acomodar ventanas funciona. Medido sobre el sistema andando:

    dos ventanas     x=66..960 y 960..1920      sin hueco ni superposición
    cuatro ventanas  los cuatro cuadrantes      cubriendo el monitor entero

**Lo que se paga:** sobre XWayland el escalado fraccionario se ve más blando y
no hay DPI por monitor. Si algún día eso pesa más que poder acomodar el puesto,
se saca esa línea y todo lo demás sigue andando: las herramientas detectan
solas que no se puede ubicar y lo dicen en vez de mentir.

**Cómo volver a Wayland**, para probar: lo que ya venga puesto manda.

    QT_QPA_PLATFORM=wayland ./BroadcastAppliance

## Se mide, no se adivina

`setGeometry` ubica el área de **cliente**; lo que una persona llama «la mitad
izquierda» incluye la barra de título. Y encima el escritorio recorta: no deja
meter una ventana debajo del dock ni de la barra de arriba, y Qt informa el
área del monitor **sin descontarlos**.

Medido acá: pidiendo la mitad derecha caía exacta; pidiendo la izquierda caía
66 px corrida y 32 abajo — 66 de dock, 32 de barra.

Con eso, la corrección obvia está mal: pedir −66 para compensar vuelve a topar
contra lo mismo. Lo que se hace es **aceptar dónde el escritorio la dejó
empezar y ajustar el tamaño** para que el borde de la otra punta caiga donde
tenía que caer.

No se modela al gestor de ventanas: se le pide, se mira dónde quedó, y se
corrige una vez. Cada escritorio tiene su propia mezcla de marcos, docks y
recortes, y adivinarla es perder.

## Cambiar de monitor estando en pantalla completa

Hay que **salir, cambiar y volver a entrar**, con una pausa entre las dos
cosas. El compositor recibió «pantalla completa en el monitor A»; cambiarle la
pantalla después no le vuelve a pedir nada, así que se queda donde estaba. Y
pidiendo la entrada en el mismo instante que la salida, todavía está procesando
la primera y se come la segunda.

Está escrito una sola vez, en `controls/BarraVentana.qml` (la barra que usan
las ventanas de video y las pestañas sacadas) y en `ventana_mover`.

## Identificar pantallas

`eDP-1` y `DP-1` no le dicen nada a nadie. `monitor_identificar` pone unos
segundos un cartel grande con el número y el nombre en cada monitor.

Aparece con **pantalla completa** a propósito: en un escritorio que no deja
colocar ventanas es lo único que igual funciona, así que de paso es la prueba
—si los carteles salen donde tienen que salir, mandar una ventana a pantalla
completa a un monitor también va a andar—.
