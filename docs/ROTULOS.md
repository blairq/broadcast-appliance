# Rótulos: los textos de lo que está al aire

El IDE de Gráficos es para **diseñarlos**. Los rótulos son para **usarlos**
mientras el evento corre. Son tres cosas que se apoyan una en la otra:

1. **La tarjeta** (pantalla Rótulos): escribir a mano y tirar al aire.
2. **El celular**: la misma tarjeta, con QR y clave.
3. **Los vínculos**: decir una vez de dónde sale cada texto, y que se mantenga
   solo.

---

## 1. La tarjeta

Va **por escena**, no por gráfico: se listan las fuentes de «Gráfico animado»
que hay en la escena **que está al aire**. En vivo uno no piensa «el zócalo
grande», piensa «lo que se está viendo».

- Lo que se escribe **no sale al aire hasta el botón**. Un rótulo que cambia
  letra por letra mientras alguien escribe es un desastre en pantalla.
- El botón grande está al lado del texto, no en un menú.
- Es una página como cualquier otra: **clic derecho en el riel** y va a su
  propia ventana. En una cabina, eso va en la pantalla chica al lado del
  teclado.
- Si la escena no tiene ninguna fuente de gráfico, la tarjeta lo dice en vez de
  mostrar campos que no van a ningún lado.

Cada gráfico puede traer además sus **botones de pedido ya escrito**
(`acciones` en su `grafico.json`, ver [GRAFICOS.md](GRAFICOS.md)): aparecen ahí
mismo y le mandan el pedido al asistente con el contexto puesto.

---

## 2. Desde el celular

El que rotula muchas veces no es el que opera. El botón **«Desde el celular»**
da un QR a `/rotulos/` en el gateway.

- **La clave hace falta también para mirar.** No es como el teleprompter, donde
  el texto es público y lo que se protege es cambiarlo: un rótulo suele llevar
  el nombre del invitado que todavía no se anunció.
- La clave viaja en el **fragmento** del enlace (`#k=…`): no llega al servidor
  ni queda en los registros de nada por donde pase.
- En el QR **puede ir o no ir**. Adentro se entra sin tipear nada, y es lo
  cómodo cuando el QR se lo pasás a alguien. Afuera es lo que querés si el QR
  va a quedar pegado en una pared: la página pide la clave y se la decís vos.
- Sin clave configurada la superficie **no atiende a nadie**. Es preferible que
  no ande a que ande abierta.

Por dentro, B no sabe nada de gráficos: es un **buzón** de dos direcciones. A
publica qué fuentes hay y se lleva los encargos de los celulares, todo en un
viaje por segundo. Ese mismo viaje es el latido con el que el teléfono sabe si
la app sigue viva, en vez de mostrar una escena fantasma.

Los encargos se **encolan** (tope de 16, recortando por lo viejo): A puede
estar ocupado. Si A no viene a buscarlos es que no está, y guardar cien no lo
va a resucitar.

---

## 3. Los vínculos: que se escriba solo

Un vínculo dice de dónde sale cada campo. Se escribe una vez —o se le pide al
asistente, que es la idea— y después se mantiene:

```json
{ "fuente": "Zócalo", "grafico": "zocalo",
  "valores": { "nombre": "{orador.nombre}", "cargo": "{orador.cargo}" } }
```

**No hay una lista de disparadores.** Se podría decir «cuando cambie el
orador», «cuando cambie la escena», y habría que mantener esa lista para
siempre. El disparador verdadero es siempre el mismo: **que lo que el vínculo
resuelve haya cambiado**. Si el nombre es otro, el rótulo está mal; si es el
mismo, no hay nada que hacer aunque haya cambiado media docena de cosas
alrededor. Se revisa una vez por segundo.

### Los orígenes

Lo que se puede poner entre llaves sale de la sala, del guion y del motor:

| Origen | De dónde sale |
|---|---|
| `{escena}` | la escena al aire |
| `{orador.nombre}` `{orador.cargo}` | el invitado que está al aire en la sala |
| `{siguiente.nombre}` … | el primero de la cola |
| `{invitado1.…}` `{invitado2.…}` | por posición en la lista de oradores |
| `{guion.bloque}` `{guion.cue}` `{guion.orador}` | el guion del evento |
| `{evento.nombre}` | el evento abierto |

`rotulo_vinculos` los devuelve **con su valor de ahora**, así que la ayuda
nunca miente sobre lo que hay. El `cargo` de un invitado es lo que él mismo
puso al entrar a la sala.

### Reglas que vienen de usarlo

- **La fuente se nombra, no se numera.** El id de una fuente de escena lo
  asigna libobs y cambia cada vez que se abre el programa; el nombre no. Un
  vínculo guardado tiene que seguir valiendo mañana.
- Un valor es **texto** con orígenes entre llaves, así que se pueden mezclar:
  `"{orador.nombre} — {escena}"`.
- **Un origen que no existe hace fallar el vínculo al guardarlo**, no en vivo.
  Casi siempre es un nombre mal escrito, y descubrirlo cuando cambie el orador
  es tarde.
- Un vínculo **que no resuelve se queda quieto** y no grita cada segundo: el
  problema se ve en la lista, en rojo.
- Si la fuente no está en ninguna escena, **espera**. No es un error: es normal
  atar un rótulo antes de armar la escena.
- **Apagar es más seguro que borrar.** Se vuelve a prender con un clic y no hay
  que acordarse de cómo estaba escrito. Al prenderlo sale al aire con lo que
  haya, aunque sea lo mismo de antes.
- Los vínculos **se ven en la tarjeta**, con lo que resuelven ahora. Un texto
  que cambia solo y no se sabe por qué es peor que uno que no cambia.

Se guardan en `Config/vinculos.json` y sobreviven al reinicio.

---

## Lo que falta

- Un cue del guion que **prenda o apague** un vínculo en un bloque
  («durante las entrevistas, zócalo automático»).
- Que la fuente de escena recuerde qué gráfico mostraba al reabrir el programa.
