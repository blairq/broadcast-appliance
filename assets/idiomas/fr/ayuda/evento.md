## Ce que c'est
Un **événement** est le conducteur de ce qui va se passer : des **séquences** (Ouverture, Allocution du maire, Clôture) et dans chacune les **tops** concrets. L'événement a TOUT ce qui lui est propre (scènes, console de mixage, médias, assemblée, web) : le dossier entier de l'événement peut être emporté sur une autre machine. Les fichiers des tops de musique et de vidéo sont les **médias de l'événement** (la page Médias, `media_list`) ; les discours vont dans `Discursos/` du conducteur.

## Les types de top et ce dont chacun a besoin dans `datos`
| Type | `datos` | S'exécute aujourd'hui ? |
|---|---|---|
| `escena` | `{escena}` | oui |
| `orador` | `{participante}` + `duracionSeg > 0` | oui |
| `musica` | `{archivo, accion, repetir}` — un fichier des médias de l'événement (`media_list`) | oui |
| `teleprompter` | `{discurso, arrancar}` — texte dans `Discursos/` | oui |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | oui |
| `video` | `{archivo, alTerminar}` | pas encore |
| `pantalla` | `{pantalla, contenido}` | pas encore |
| `pausa` | attend ce que dit `duracionSeg` | oui |
| `espera` | s'arrête jusqu'à ce que l'opérateur confirme | oui |
| `nota` | ne fait rien : c'est une indication | oui |

Ceux qui ne s'exécutent pas encore peuvent quand même être écrits : ils comptent pour les durées et disent à l'opérateur quoi faire. Quand leur tour arrive, le système le note au lieu d'échouer en silence.

## Durées
De la `duracionSeg` de chaque top découlent celle de la séquence et celle de l'événement ; avec `inicio` (HH:MM) on obtient l'heure prévue de chaque chose, et avec `fecha` (YYYY-MM-DD) plus `huso` (nom IANA) on obtient `cuando`, le début comme instant absolu pour qui est dans un autre fuseau. Pendant le déroulement, ce qui compte, c'est l'**écart** : `desvioSeg` positif = on est en retard. Ce qui est en pause ne compte pas comme retard.

## Avant l'événement
- `evento_revisar` : tops incomplets, scènes qui n'existent pas, fichiers manquants ou vides.
- `evento_ensayo_rapido` : parcourt TOUT le conducteur d'un coup, sans rien toucher, et dit pour chaque top s'il pourrait s'exécuter. C'est ce qu'il vaut mieux lancer après avoir préparé un long conducteur.
- L'interrupteur de **répétition** (`evento_correr {"ensayo": true}`) déroule le conducteur en temps réel sans passer à l'antenne.

## Pendant le déroulement
`evento_correr` avec `arrancar`, `siguiente`, `atras`, `ir`, `pausar`, `reanudar`, `parar`. **Il passe à l'antenne** sauf en répétition, donc c'est l'opérateur qui décide.

Dérouler le conducteur N'EST PAS être en direct : être en direct, c'est que quelque chose sorte vraiment (diffusion ou enregistrement), et c'est ce qu'indique `en_vivo` dans `status`. En direct, l'application prévient l'opérateur avant toute action risquée et `settings_set` n'applique pas ce qui redémarre la conférence ou le moteur (cela ferait sortir tous les invités ou couperait la sortie au pire moment). Avec le conducteur qui se déroule pour tester, sans diffusion, rien n'est bloqué.

## Prompteur
Le top `teleprompter` charge un texte de `Discursos/` et le laisse PRÊT mais arrêté. Tout le reste (écrire les discours, les envoyer, piloter le texte à l'antenne) a sa propre catégorie : `help {"topic": "prompter"}`.
