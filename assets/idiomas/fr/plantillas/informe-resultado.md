# Résultats de la séance — {{cuerpo}}

**{{evento}}** · {{fecha}} {{hora}}

## Quorum

**{{quorum.estado}}** : {{quorum.presentes}} présents, {{quorum.necesarios}} requis ({{quorum.miembros}} membres).

## Ordre du jour

{{#puntos.0}}
| N° | Dossier | Objet | État | Résultat |
|---|---|---|---|---|
{{/puntos.0}}
{{#puntos}}
| {{n}} | {{expediente}} | {{titulo}} | {{estado}} | {{resultado}} |
{{/puntos}}
{{^puntos}}
L'ordre du jour n'a pas été saisi.
{{/puntos}}

## Votes

{{#votaciones}}
### {{asunto}}

Pour **{{afirmativos}}**, contre **{{negativos}}**, abstentions **{{abstenciones}}** (présents {{presentes}} ; {{mayoria}}) : **{{resultado}}**.

{{/votaciones}}
{{^votaciones}}
Aucun vote officiel enregistré.
{{/votaciones}}

## Temps de parole

{{#palabra}}
- {{nombre}} : {{tiempo}}
{{/palabra}}
{{^palabra}}
Aucun enregistrement des temps de parole.
{{/palabra}}

---

_Généré le {{generado}}._
