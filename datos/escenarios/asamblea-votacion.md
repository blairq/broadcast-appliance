# Asamblea grande con votación y quórum

## Para qué
Asamblea de una asociación, sindicato, partido o federación: muchos asistentes que se acreditan al
entrar, quórum, varias votaciones y resultados que todos tienen que ver.

## Preguntas propias
- ¿Cuántos con derecho a voto? ¿Hay padrón (planilla)? ¿Votan todos igual o por ponderación?
- ¿Cómo se acreditan: QR de la invitación en la puerta, a mano, por DNI?
- ¿Cómo votan: con el celular (página del público con su QR), a mano alzada contada, con la
  central de la sala?
- ¿Orden del día y mayorías por punto? ¿Quórum de cuántos?
- ¿Pantallas grandes en la sala para resultados? ¿Transmisión?

## Qué armar
- Padrón completo con `vota: true` y contacto (correo o WhatsApp) para la invitación con QR.
- Acreditación en la puerta (padron_puerta / padron_acreditar) y quórum en vivo (quorum_estado).
- Orden del día con documentos; votaciones por punto.
- Escenas: «Mesa», «Orador», «Votación en curso», «Resultado»; placa con el quórum.
- Pantallas de la sala con la votación (pantalla_*) y QR para votar a la vista.

## Verificar
Dirección FIJA antes de imprimir o mandar los QR (acceso_remoto_probar_direccion); un voto de
prueba desde un celular; la pantalla de resultados.

## Entregar
Invitaciones con QR a cada uno; el enlace del público para votar; al final, acta con quórum y
resultados.
