# Resultado de la sesión — {{cuerpo}}

**{{evento}}** · {{fecha}} {{hora}}

## Quórum

**{{quorum.estado}}**: {{quorum.presentes}} presentes, {{quorum.necesarios}} necesarios ({{quorum.miembros}} miembros).

## Orden del día

{{#puntos.0}}
| N.º | Expediente | Asunto | Estado | Resultado |
|---|---|---|---|---|
{{/puntos.0}}
{{#puntos}}
| {{n}} | {{expediente}} | {{titulo}} | {{estado}} | {{resultado}} |
{{/puntos}}
{{^puntos}}
No se cargó el orden del día.
{{/puntos}}

## Votaciones

{{#votaciones}}
### {{asunto}}

Afirmativos **{{afirmativos}}**, negativos **{{negativos}}**, abstenciones **{{abstenciones}}** (presentes {{presentes}}; {{mayoria}}): **{{resultado}}**.

{{/votaciones}}
{{^votaciones}}
No hay votaciones oficiales cargadas.
{{/votaciones}}

## Uso de la palabra

{{#palabra}}
- {{nombre}}: {{tiempo}}
{{/palabra}}
{{^palabra}}
Sin registro del uso de la palabra.
{{/palabra}}

---

_Generado el {{generado}}._
