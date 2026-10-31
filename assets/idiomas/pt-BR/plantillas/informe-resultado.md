# Resultado da sessão — {{cuerpo}}

**{{evento}}** · {{fecha}} {{hora}}

## Quórum

**{{quorum.estado}}**: {{quorum.presentes}} presentes, {{quorum.necesarios}} necessários ({{quorum.miembros}} membros).

## Ordem do dia

{{#puntos.0}}
| N.º | Processo | Assunto | Estado | Resultado |
|---|---|---|---|---|
{{/puntos.0}}
{{#puntos}}
| {{n}} | {{expediente}} | {{titulo}} | {{estado}} | {{resultado}} |
{{/puntos}}
{{^puntos}}
A ordem do dia não foi carregada.
{{/puntos}}

## Votações

{{#votaciones}}
### {{asunto}}

Sim **{{afirmativos}}**, não **{{negativos}}**, abstenções **{{abstenciones}}** (presentes {{presentes}}; {{mayoria}}): **{{resultado}}**.

{{/votaciones}}
{{^votaciones}}
Não há votações oficiais registradas.
{{/votaciones}}

## Uso da palavra

{{#palabra}}
- {{nombre}}: {{tiempo}}
{{/palabra}}
{{^palabra}}
Sem registro do uso da palavra.
{{/palabra}}

---

_Gerado em {{generado}}._
