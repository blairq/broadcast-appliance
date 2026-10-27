# Clase híbrida

## Para qué
Un docente da una clase con alumnos presentes y otros conectados. Sin quórum ni votación, pero con
asistencia (se toma a mano), materiales, preguntas y una grabación para el que faltó.

## Preguntas propias
- ¿Qué materia, tema y nivel? ¿Cuánto dura y en qué partes se divide?
- ¿Cuántos alumnos en el aula y cuántos remotos? ¿Necesitan registrarse o con el enlace alcanza?
- ¿Se toma asistencia? (A mano: padron_anotar / padron_acreditar por alumno.)
- ¿Materiales: fichas, lecturas, presentaciones? ¿Se muestran en pantalla? (Escena PDF.)
- ¿Teleprompter para el docente con el guion de la clase?
- ¿Algún alumno necesita traducción o subtítulos (otro idioma, hipoacusia)?
- ¿Logo de la escuela o escudo de la provincia? ¿Placas por tema con imágenes?
- ¿Los remotos preguntan por chat o hablan? ¿Se graba para subir después?

## Qué armar
- Bloques por tema con una placa de título por tema (texto real en `valores`, salidaEn 4000-5000)
  y una imagen liviana del tema en su escena.
- Escena «Docente», «Pizarra/Presentación», «PDF» (pdf_escena_crear) con las fichas cargadas en
  la documentación del evento, relacionadas a cada tema.
- Teleprompter con el guion de cada parte.
- Padrón de alumnos para la asistencia (sin `vota`), con su contacto si se manda la invitación.
- Subtítulos o traducción si se pidió (traduccion_agregar).
- Logo de la escuela como capa DSK.

## Verificar
Enlace de los remotos probado desde un celular; la escena PDF con las fichas; audio del docente
(micrófono corbatero si hay).

## Entregar
El enlace para los alumnos (o la invitación por alumno), la web de la clase con los materiales,
y al final la grabación con capítulos por tema.
