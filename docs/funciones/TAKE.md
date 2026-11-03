# TAKE atómico: la escena sale entera

Cuando el operador (o la línea de tiempo, o el asistente) hace **TAKE**, la escena que entra
sale al aire **completa**: nunca a medio armar. Antes, cada capa aparecía al aire encima de
la anterior cuando le llegaba su primer cuadro (el fondo primero, después los invitados de a
uno), y en el lugar de un invitado que tardaba se veía un instante la placa «sin señal».

## Cómo funciona

1. **La escena se arma fuera del aire.** El programa pone las capas de la escena que entra
   con alfa 0 y sigue mostrando la anterior.
2. **Espera a que todas tengan imagen.** Una entrada que se rearma al volver a usarse arranca
   «esperando señal»: los cuadros de la placa «sin señal» no cuentan como listos (ni en el
   programa ni en el preview). Si la entrada venía de la placa, sigue esperando hasta que
   vence el último cuadro de la placa que salió al aire.
3. **Corta con la transición** apenas están todas: la mezcla, el empuje y la cortina arrancan
   recién con todo listo.
4. **El audio sigue a la imagen**: mientras la escena se arma, suena la anterior; el audio de
   la nueva entra en el mismo tic en que corta la imagen (o arranca su mezcla), con el fundido
   de la transición.

Lo que no se espera, porque no se ve o no tiene video:

- una capa **oculta o con alfa 0** (puede no dar cuadros nunca: un lugar de invitado vacío);
- una entrada **sin video** en la escena (un micrófono);
- una escena metida dentro de otra se espera hasta que esté completa.

## El tope

`video.esperaCorteMs` (Configuración → Avanzado, de fábrica **1500 ms**): si alguna capa no
llegó a tiempo, corta igual con lo que hay y avisa cuáles no llegaron. Así un invitado que se
cayó no frena el programa.

## Relacionado

- A quien está en una escena fuera del aire **que se muestra** en algún lado (un monitor, una
  ventana, el multiview, una distribución de pantallas) no se lo pausa: su video sigue
  llegando en la capa baja, así la escena ya está lista cuando se hace TAKE
  ([CONFERENCIA.md](CONFERENCIA.md), «Lo que cuesta decodificar»).
- Pruebas: `test_composicion` (un take con capas que llegan tarde, tic por tic, nunca muestra
  la escena a medio armar ni la placa en lugar de la señal; una capa oculta que no llega; el
  micrófono) y `test_motor_salidas` (el audio no se adelanta a la imagen).
