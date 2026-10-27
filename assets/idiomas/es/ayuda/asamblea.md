Una sesión de asamblea (concejo, consejo, directorio, consorcio) no es un guion de cues: la palabra cambia sin orden previo y cada banca tiene que verse y oírse cuando le toca. Se prepara en este orden, y `asamblea_revisar` dice en cada momento qué falta y cómo se arregla. **Corrélo al empezar y al terminar.**

1. **El Cuerpo:** cada integrante con su banca (`cuerpo_importar` desde una planilla, o `integrante_editar`). Sin banca no aparece en el recinto.
2. **El recinto:** la grilla de bancas como se ven desde la cabina (`recinto_editar {filas, columnas, auto: true}`).
3. **La toma de cada banca:** para cada banca, una posición PTZ que la encuadre (`ptz_posicion_crear`) asociada a la BANCA (`banca_toma {banca, camara, posicion}`; queda en la banca aunque cambie quién se sienta, y una vacía también la tiene). Las escenas del orador y del debate van con la fuente «Banca con la palabra» (`asamblea_escenas_base {crear: true}`): muestra la toma de quien habla (ranura 1; en un debate, también la 2). Con una cámara digital la recorta de la toma entera sin mover nada (el plano general sigue igual y dos bancas se ven a la vez); una motorizada va sola a la posición. «Enfocar banca 10 / a Pepito López / Presidencia»: `enfocar {que}`.
4. **LAS ESCENAS** (lo que más se olvida): al darle la palabra a una banca se ARMA la escena que muestra su cámara; si ninguna escena la muestra, no sale nadie. Hace falta:
   - una escena por cada cámara de bancas (con el zócalo `{orador}` si se quiere el nombre en pantalla);
   - un plano general;
   - una con «Remoto con la palabra» (el integrante que habla desde el celular);
   - las del debate: dos remotos lado a lado («Remoto con la palabra» y «Remoto con la palabra 2»), uno de la sala y un remoto, dos de la sala;
   - si hay votación en pantalla, la del resultado.
   `asamblea_escenas_base` las propone armadas con las cámaras y entradas que hay (mostrale la propuesta al operador) y con `crear: true` las crea y configura el debate. Después se pueden retocar (zócalo, fondo, logo).
5. **La central de conferencia** (si hay): `help {"topic": "conferencia"}`; cada banca con su unidad.
6. **Los remotos:** `help {"topic": "sesion"}`; link y PIN a los que sesionan a distancia; qué ven en el celular (`sesion_vistas`).
7. **El orden del día** (`orden_del_dia_cargar`): se ve en los celulares y en la consola de la presidencia.
8. **El debate** (si va a haber): `debate_config` con la escena de cada tipo y el tiempo de cada turno.
9. **El guion** (`evento_armar`) para lo que sí tiene orden: apertura, himno, cierre. El cuerpo de la sesión lo maneja la fila de oradores (`banca_*`), no los cues.

Durante la sesión: `banca_al_aire` / `recinto_orador_terminar` (la palabra), `presidencia_callar_todos` y `presidencia_hablar`, `debate_iniciar` / `debate_turno`, votación con `dcn_votacion` o `sesion_votacion`.

Para probar o mostrar todo esto sin tocar lo de verdad hay una demo completa (un consejo con 15 integrantes, recinto, central simulada, escenas y debate): `configuracion_crear {"nombre": "Demo", "desde": "demo:Demo parlamentaria"}` y `configuracion_cambiar` (ver `help {"topic": "configuraciones"}`).
