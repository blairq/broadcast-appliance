# Reunión de comisión

## Para qué
La comisión trata expedientes de un orden del día y saca dictámenes. Integrantes del cuerpo,
asesores e invitados (funcionarios, vecinos) que exponen. Suele ser pública y grabada.

## Preguntas propias
- ¿Qué comisión, cuántos integrantes, quién preside? ¿Hay integrantes remotos?
- ¿El orden del día? (Pedí el texto: orden_del_dia_cargar acepta pegarlo.)
- ¿Vienen invitados a exponer? ¿Cuánto tiempo cada uno? ¿Traen presentación?
- ¿Se vota el dictamen? ¿Hace falta quórum? ¿Quórum de cuántos?
- ¿Se transmite? ¿Se publica la documentación de cada expediente?

## Qué armar
- Orden del día cargado (orden_del_dia_cargar) y la documentación de cada expediente relacionada
  a su punto (documento_agregar con `puntos`).
- Padrón con los integrantes (vota: true) y los invitados con su papel.
- Escenas: «Presidencia», «Integrante con la palabra», «Invitado expone» (con su presentación),
  «Votación» si se vota; zócalo con nombre y cargo de quien habla.
- Uso de la palabra con tiempos (speakers_config, debate_config si hay réplicas).
- Si se vota: quórum (quorum_regla) y votación (votacion_abrir).

## Verificar
Quórum calculado con el padrón; los invitados remotos probados; los documentos en la web.

## Entregar
La web del evento con orden del día y documentos; las invitaciones; al final, el acta
(evento_acta) con lo tratado y lo votado.
