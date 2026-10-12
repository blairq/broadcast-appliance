# Procedimientos estándar de un evento

Lo que el asistente sigue para **diseñar, controlar, ensayar y salir** con un evento. Hay dos,
como temas de ayuda del MCP (son la fuente: este documento los resume):

- `help {"topic": "procedimiento_salon"}` — evento en salón: público, oradores, transmisión y
  grabación; con o sin invitados remotos y preguntas del público.
- `help {"topic": "procedimiento_asamblea"}` — sesión de asamblea: cuerpo con bancas, quórum,
  orden del día, votaciones, integrantes a distancia.

Los dos tienen las mismas partes: **0** lo que hay que preguntar, **1** todo lo que hay que
configurar (con la herramienta de cada cosa), **2** el control automático, **3** lo que se le
pide a la persona que controle, **4** el ensayo de cada pieza, **5** dejarlo listo y **6** el día,
de la señal de ajuste al final de la transmisión.

## Las herramientas que agregan

**`evento_preparar`** deja listo el principio y el final del evento abierto:

- escena «Señal de ajuste»: barras HD con la hora (muestran si la señal está viva) + tono de
  1 kHz a −20 dBFS (fuente aparte, solo audio, que sigue a la escena: suena solo con ella al
  aire) + la banda «SEÑAL DE AJUSTE» con el nombre del evento y cuándo empieza;
- escena «Antes de empezar»: la placa con el nombre y «Empezamos a las HH:MM» (con el huso si no
  es el del aparato);
- escena «Fin de transmisión»: gracias + el nombre del evento + dónde queda la grabación;
- bloques del guion «Previa» (señal de ajuste → espera la confirmación de que se ve en la
  plataforma → placa de espera con música → espera para arrancar) y «Cierre» (música → placa de
  fin → espera para cortar transmisión y grabación);
- con `al_aire`, la señal de ajuste queda en el programa; si no, armada en el preview.

Se puede correr las veces que haga falta: reusa lo que existe y vuelve a dibujar las placas con
los datos de ahora. Trabaja llamando a las herramientas de siempre (mismas validaciones, misma
guarda de aire).

**`evento_verificar {tipo: salon|asamblea}`** es la lista de control. Cada ítem trae `estado`
(«ok», «falta», «aviso», «a_mano»), el detalle y `como` arreglarlo. Revisa el evento (fecha,
hora, huso), el guion (`evento_revisar`), las escenas del principio y el final, el destino de la
transmisión y las cuentas, el disco, el audio, la página web, la sala de invitados y las preguntas
si el evento las usa, el padrón; en una asamblea también `asamblea_revisar`, el quórum, el orden
del día y los votantes. Lo que solo confirma una persona lo anota en la lista de chequeo del
evento (nunca lo marca hecho): el asistente se lo pide y lo marca con `evento_chequeo` cuando le
dicen que sí.

Gráficos nuevos de fábrica: `antes-de-empezar`, `fin-transmision`, `identificacion-ajuste`.

## Probado

Con un evento de prueba: `evento_preparar` en 25 pasos sin error; las tres escenas capturadas
(barras con la hora que corre, banda de identificación, placas); el tono medido con
`audio_analyze`: −20,0 dBFS en la banda de 1 kHz. `evento_verificar` en salón y en asamblea; un
ítem confirmado con `evento_chequeo` pasa a «ok» con su nota.

En el camino se arreglaron tres cosas que le pasaban a cualquiera:

- `source_add` con un nombre con «:» decía «agregada» pero el motor no la creaba (el «:» separa
  entrada y salida): ahora lo rechaza con el motivo.
- Dos tarjetas PTZ en el grafo de una entrada (un grafo viejo de una fuente borrada cuyo nombre
  «aplanado» coincide) daban dos `baptz` y la cadena no enganchaba nunca: uno por entrada, y el
  grafo viejo que choca se descarta.
- `evento_revisar` daba «sin señal» a una entrada solo de audio.
