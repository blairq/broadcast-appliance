Dossiers (dans le système portable) : `Media/` (la bibliothèque : logos, cartons, fonds, musique, vidéos), `Shares/` (ce que les invités ont partagé), `Recordings/`.

## Recette : « cherche l'image X et utilise-la comme logo en haut de la scène 4 »
1. `media_find {"query": "X"}` (sans tenir compte des majuscules, des accents ni des tirets).
2. `media_view {"path": ...}` pour confirmer que c'est la bonne (regardez-la).
3. `scene_list` pour trouver la scène (« la 4 » peut être la quatrième de la liste ou une scène qui porte ce nom : en cas de doute, demandez).
4. `medio_poner {"path": ..., "scene": ..., "name": "Logo", "layout": "pip-tr"}` (en haut à droite ; `pip-tl` en haut à gauche ; ou `{x,y,w,h}`).
5. `foto_fuente {"name": escena}` et ajuster avec `source_update` (position, taille) jusqu'à ce que ce soit bien.

Vidéos et audios : `medio_poner` avec `loop` pour des fonds ou de la musique en boucle ; `alert_create {"type": "media_end"}` pour être prévenu quand ils se terminent.

## Ranger le disque
`media_carpetas` dit COMMENT ils sont répartis (l'arborescence, avec le nombre d'éléments de chaque dossier ; les vides apparaissent aussi, car c'est là qu'on déplace des choses). `media_ordenar {"accion": "mover"|"renombrar"|"carpeta"|"borrar"}` les range.

**Supprimer ne supprime pas** : cela envoie dans `Papelera/`. Un fichier peut être dans une scène, ou être le seul qui restait de l'événement précédent ; annuler doit consister à le sortir d'un dossier. Et si la destination contient déjà quelque chose avec ce nom, il est numéroté au lieu d'être écrasé.

Les dossiers du système (Media, Shares, Recordings) ne se déplacent ni ne se suppriment. L'opérateur voit tout cela dans l'onglet Médias, et ce que vous changez ici lui apparaît sans qu'il ait à actualiser.

## Présentations
Un `.pdf` ou un `.pptx` ne « s'utilise » pas dans une scène : on le DÉCOUPE en cartons et cela donne un carrousel, qui permet de sauter à n'importe lequel en direct. Voir `help {"topic": "laminas"}`.
