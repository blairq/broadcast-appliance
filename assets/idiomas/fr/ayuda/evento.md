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
## Le visage de l'événement sur le web (thème)
Chaque événement a son **thème** : accent, fond, police, logo et nom, sur **toutes** les pages web du système : celle de l'événement, celle des invités, la session à distance (où votent les membres), les questions du public, la borne d'accueil et les écrans de la salle. Les consoles des opérateurs (régie, pupitre de boutons, présidence, titrages, correction) prennent ce que dit `alcance` : par défaut, seulement l'accent et le logo. Les couleurs qui ont un sens ne changent jamais : tally, à l'antenne et armé, vumètres, oui et non d'un vote.

- `evento_tema {"accion": "ver"}` : le thème, les couleurs qui vont sortir (`resuelto`) et les `avisos` de contraste. S'il y a des avertissements, dites-le à l'opérateur et proposez une autre couleur.
- `evento_tema {"accion": "cambiar", "colores": {"acento": "#00a86b"}, "marca": {"nombre": "Forum 2026"}}` : ce qui n'est pas indiqué est calculé (le texte sur l'accent découle du contraste).
- `evento_tema {"accion": "logo", "archivo": "<chemin>", "cual": "logo"}` (SVG, PNG ou WebP, jusqu'à 2 Mo ; `logoClaro` pour les fonds sombres, `favicon` pour l'onglet) et `{"accion": "fuente", "archivo": "<chemin>", "familia": "…"}` (WOFF2, WOFF, TTF ou OTF, jusqu'à 4 Mo). Sans internet : pas de Google Fonts, la police voyage avec l'événement. Un SVG avec des scripts ou des gestionnaires d'événements est refusé.
- `alcance` : `{"publico": "todo", "control": "acento"}` (chacun nada, acento ou todo).
- Un site généré avant le thème : `evento_tema {"accion": "migrar_sitio"}` (il respecte ce qui a été modifié à la main dans `estilo.css`).

Il est enregistré dans le dossier de l'événement (`tema.json` et `Tema/`). Sans thème propre, l'accent et le fond viennent de ceux du site (`evento_web sitio {color, fondo}`). Dans l'application : Services → Apparence des pages web → Thème de l'événement.

Hors du web, c'est aussi la valeur par défaut : les **graphismes** avec une couleur d'accent (bandeaux, cartons, tickers) sortent avec celle de l'événement si on ne leur en donne pas une autre, et les nouveaux **QR codes** prennent ses couleurs (toujours sombre sur clair, pour qu'ils se lisent) et son logo au centre. Avec un thème clair, les pages du public s'inversent entièrement (surlignages, champs et ombres compris).

