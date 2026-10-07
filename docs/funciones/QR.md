# Códigos QR

Una biblioteca de códigos guardados con su estudio de diseño, y **un solo
generador** para todo el sistema.

## Por qué uno solo

El mismo código sale por cinco lados: la pantalla del operador, un televisor
por Chromecast (que no corre nuestro JavaScript), las páginas del celular, un
archivo para imprimir y, más adelante, un gráfico al aire. Con dos generadores
—uno para la vista previa y otro para lo que se entrega— el código que se
diseña no es el que sale, y eso se descubre tarde y en papel.

Por eso `ba::qr` vive en el núcleo (sin Qt, sin libobs): lo usan la app y el
gateway, y lo prueban los tests sin levantar nada. La vista previa y el PNG
rasterizan **ese mismo SVG**; no hay dos dibujos que puedan diferir.

En SVG porque un QR son cuadraditos: escala a un televisor de 55 pulgadas o a
una hoja A4 sin verse borroso, y pesa menos.

## Lo que se midió, y por qué importa

Las primeras versiones de los ojos se veían bien y **no se leían**.

Un lector encuentra el código barriendo líneas y buscando la proporción
1:1:3:1:1 de negro y blanco. En un ojo cuadrado esa proporción sale por
cualquier línea que lo cruce; cuanto más redondo, menos líneas sirven. Con un
barrido de radios, decodificando cada resultado:

| radio del ojo | ¿lee? |
|---|---|
| 0.00 – 1.00 módulos | sí |
| 1.25 en adelante | **no** |

Se usa **0.9**, con margen. Estaba en 1.8.

Y por eso **no hay ojo circular**, aunque el sistema de referencia lo ofrezca:
no se lee con ningún cuerpo, ni como anillo liso ni armado con puntos (un aro
de puntos deja huecos y se pierde la proporción). Un estilo que se ve lindo y
no escanea es peor que no tenerlo.

El cuerpo, en cambio, no afecta la lectura: las cinco formas leen con los dos
ojos. Lo verificado, decodificando de verdad: las 10 combinaciones, más logo al
10/20/30%, degradados, marcos, vCard, texto largo con logo y zona silenciosa
mínima, a 500 y a 220 píxeles.

## El ícono del centro

Un set propio y chico, vendoreado (`src/core/qr/Iconos.cpp`), en el formato de
Lucide: lienzo de 24×24, trazos de grosor 2 con puntas redondeadas. Agregar uno
es pegar su `d`.

Se guardan **por nombre** con el código, no por su dibujo: cambiar el ícono de
un código guardado es cambiar una palabra, y si se mejora el dibujo, los
códigos ya guardados lo heredan.

Un ícono tapa módulos, así que **sube la corrección de errores a la máxima
solo**. No es una preferencia: sin eso el código deja de leerse.

## Los dos ámbitos

| dónde | qué va | archivo |
|---|---|---|
| **del aparato** | el WiFi de la sala, el teléfono de producción | `Config/qr.json` |
| **del evento** | el formulario de inscripción, la encuesta del cierre | `Eventos/<x>/QR/qr.json` |

Se copian de uno a otro con el clic derecho. El del evento viaja con él a otra
máquina.

## Los del aparato se resuelven al mostrarse

`sala`, `prompter` y `rotulos` no guardan texto: guardan **qué son**. El enlace
de la sala lleva su PIN y el del teleprompter su clave, y esa clave es nueva en
cada arranque. Guardar el texto ya armado daría un código que funciona hoy y
manda a ningún lado mañana.

## Por MCP

Categoría `qr`: `qr_listar`, `qr_generar` (sin guardar ni tocar lo que el
operador esté diseñando), `qr_guardar`, `qr_borrar` y `qr_exportar`. Pasan por
el mismo ViewModel que la pantalla, así que lo que genera el asistente aparece
en la pestaña en el acto.

## Lo que falta

- **El carrusel**: varios códigos rotando en una pantalla.
- **Mandar un código guardado a un televisor** con su estilo: hoy el
  `/pantalla/qr.svg` del gateway dibuja uno simple. Ahora que el renderizador
  es compartido, es pasarle el estilo por la URL.
- **Un logo de imagen** (el hueco y `logoSrc` ya están; falta elegir el archivo
  desde la interfaz).
