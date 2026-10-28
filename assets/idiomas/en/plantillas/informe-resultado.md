# Session results — {{cuerpo}}

**{{evento}}** · {{fecha}} {{hora}}

## Quorum

**{{quorum.estado}}**: {{quorum.presentes}} present, {{quorum.necesarios}} needed ({{quorum.miembros}} members).

## Agenda

{{#puntos.0}}
| No. | File | Item | Status | Result |
|---|---|---|---|---|
{{/puntos.0}}
{{#puntos}}
| {{n}} | {{expediente}} | {{titulo}} | {{estado}} | {{resultado}} |
{{/puntos}}
{{^puntos}}
No agenda was loaded.
{{/puntos}}

## Votes

{{#votaciones}}
### {{asunto}}

In favor **{{afirmativos}}**, against **{{negativos}}**, abstentions **{{abstenciones}}** (present {{presentes}}; {{mayoria}}): **{{resultado}}**.

{{/votaciones}}
{{^votaciones}}
No official votes were recorded.
{{/votaciones}}

## Speaking time

{{#palabra}}
- {{nombre}}: {{tiempo}}
{{/palabra}}
{{^palabra}}
No speaking time was recorded.
{{/palabra}}

---

_Generated on {{generado}}._
