# El registro

Todo lo que pasa queda escrito, en un solo formato, para los tres procesos:

```
2026-09-22T04:05:34.219Z  error | graficos | 0 | validar: línea 2: NoExiste is not a type
      fecha y hora        nivel    módulo   cód   descripción
```

Se lee de corrido y se parsea sin ambigüedad, que es lo que permite que exista
el visor. **La hora va en UTC en el archivo** —así los archivos de los tres
procesos se entrelazan sin pensar en husos ni en horarios de verano— y el visor
la muestra en **hora local**, que es con la que uno se acuerda de cuándo pasó.

---

## Los niveles

| | |
|---|---|
| `traza` | el detalle fino: cada cuadro, cada paquete. Ruidoso. |
| `depuracion` | lo que le sirve a quien está buscando un problema. |
| `info` | lo que pasó y está bien que pase. **El de todos los días.** |
| `aviso` | algo no está como debería, pero se sigue. |
| `error` | algo no se pudo hacer. |
| `fatal` | algo que impide seguir. |

El mínimo se cambia **en vivo** (en el visor, o con `log_nivel` desde el
asistente) y con la variable de entorno **`BA_LOG_NIVEL`**, que es la que sirve
cuando lo que falla es el arranque y no llegás a tocar nada:

```sh
BA_LOG_NIVEL=depuracion ./BroadcastAppliance
```

Lo que está **por debajo del mínimo no se escribe en ningún lado**, así que no
alcanza con pedirlo después. El visor y `log_leer` dicen cuántas líneas se
descartaron por eso, para que nadie confunda «no pasó nada» con «no lo estaba
mirando».

---

## Los módulos

Salen de quien escribe la línea, y son lo que permite mirar una sola cosa
cuando hay veinte hablando:

| Módulo | Qué es |
|---|---|
| `app` | el Proceso A, y todo lo que no dice de dónde viene (printf, salida cruda) |
| `qml` | lo que se rompe en la interfaz. **El que más se extrañaba.** |
| `qt` | el framework |
| `obs` | el motor de emisión. Muy hablador: su `info` entra como `depuracion` |
| `gateway` | el Proceso B: sala, WebRTC, túnel |
| `graficos` | el renderizador (`ba-grafico`) |
| `launcher` | el supervisor: lanzar, vigilar, integridad, recuperación |
| `marca` | las marcas que pone el operador o el asistente |

Qt, QML y libobs entran por acá con el nivel que ellos mismos declaraban y que
antes se tiraba a la basura. Un aviso de QML trae además **el archivo y la
línea**, que es lo único con lo que se puede arreglar.

---

## Dónde está

Un archivo por proceso en `Logs/`, rotativo (4 MB, 5 archivos):

```
Logs/app.log        Proceso A (y lo que Qt, QML y libobs escriben)
Logs/gateway.log    Proceso B
Logs/graficos.log   el renderizador
Logs/launcher.log   el supervisor
Logs/cloudflared.log  el túnel (formato de ellos, no nuestro)
```

Cuando el launcher lanza un proceso, **captura su salida a ese archivo**, y el
proceso no abre otro: si abriera el suyo, cada línea quedaría dos veces y habría
dos escritores rotando el mismo archivo. Corriendo suelto desde una terminal, sí
abre el suyo, para que igual quede rastro.

---

## El visor

Pantalla **Registro** (riel: `❑`). Clic derecho en el riel la saca a su propia
ventana, que es como conviene tenerla mientras se prueba algo.

- Filtros por **nivel**, **módulo** y **texto**, y los tres a la vez.
- Color por nivel: los errores se ven sin leer.
- **Seguir** va prendido; al apagarlo deja de leer el disco, así la lista no se
  mueve sola mientras estás leyendo una línea.
- **Marcar** escribe una línea propia: se pone *antes* de probar algo y después
  se busca esa marca. Es la forma barata de separar lo que provocaste de lo que
  ya venía pasando.
- **Copiar** se lleva lo que estás viendo, ya filtrado, para pegarlo.
- «Solo esta corrida» lee el anillo en memoria de la app: instantáneo y sin
  tocar disco, pero solo lo de la app y solo desde que arrancó. Apagado lee los
  archivos, que traen también al gateway, al launcher y —lo que de verdad
  importa después de un incidente— **la corrida anterior**.

---

## Desde el asistente

| Herramienta | Para qué |
|---|---|
| `log_leer` | las líneas, con filtros. **Por acá se empieza siempre.** |
| `log_nivel` | leer o bajar el nivel en vivo |
| `log_marcar` | dejar una marca |
| `logs_tail` | un archivo **crudo**, sin interpretar (p. ej. `cloudflared.log`) |

La receta cuando algo falla:

1. `log_leer` sin filtros y con `minimo: "aviso"`. Si hay un error, está ahí.
2. Si no aparece nada, `minimo: "info"` y el módulo que sospeches.
3. Si sigue sin aparecer: `log_nivel` a `depuracion`, hacé que el problema pase
   otra vez, y volvé a leer. **Dejá el nivel como estaba** cuando termines.

---

## Para el que escribe código

```cpp
#include "diag/Registro.hpp"

ba::diag::registro().info("escena al aire: " + nombre);
ba::diag::registro().error("no se pudo abrir la cámara", 12);
// Con módulo propio:
ba::diag::registro().linea(ba::diag::Nivel::Aviso, "sala", 9, "el invitado no contesta");
```

- **No lanza nunca** y descarta antes de armar nada si el nivel no llega.
- **No es para el hilo de audio**: escribe a disco.
- El **código** es un número estable por tipo de error: es lo que permite buscar
  el mismo problema en dos máquinas distintas. 0 cuando no aplica.
- La descripción dice **qué pasó y con qué**, no «error». `no se pudo abrir la
  cámara /dev/video2 (ocupada)` sirve; `fallo de inicialización` no.
- Un salto de línea adentro del texto se marca con `⏎`: un salto de verdad
  partiría la línea y el visor leería media entrada como si fuera otra.

## El arranque, y quién lo rompió

Antes de cargar cada cosa que viene del disco —la disposición del dashboard,
los vínculos, el último evento— se anota en el registro **qué se está por
cargar**, y recién cuando la interfaz **dibujó** se marca el arranque como
terminado (`Runtime/arranque.json`). Si algo quedó a medias, eso fue lo que se
llevó puesto el arranque: a la segunda vez queda **apagado**, el programa abre
sin ello, y el visor lo dice arriba de todo con un botón para volver a
prenderlo.

Se mide **que el arranque haya terminado**, no que el proceso siga vivo, así
que agarra igual una caída que un cuelgue. Ver `core/diag/DiarioDeArranque.hpp`.

Modo seguro (el circuit breaker del launcher, tras varias caídas seguidas) no
carga **nada** del disco, pero no deja antecedentes: salir del modo seguro
alcanza para volver a la normalidad.

## Lo que falta

- El visor no muestra cuánto tardó en cargar cada cosa: una extensión que no
  rompe nada pero hace lento el arranque no se ve.
