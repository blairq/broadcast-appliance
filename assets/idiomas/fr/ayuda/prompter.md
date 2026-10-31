## Comment c'est construit
Le texte est affiché par une page web : `/prompter/` est l'écran de l'intervenant (lecture seule) et `/prompter/#control` le contrôle de l'opérateur (avec clé). L'état —quel texte, où on en est, à quelle vitesse— vit dans le gateway, pas dans la tablette : si quelque chose redémarre, le discours reprend là où il en était.

Les discours sont des fichiers texte dans `Discursos/`, À L'INTÉRIEUR du dossier de l'événement ouvert, avec des sous-dossiers si nécessaire (`Apertura/intendente.txt`). Ce sont les MÊMES que charge le top `teleprompter` du conducteur : il n'y a pas de bibliothèque à part.

## Pour quoi on va vous appeler
Presque toujours pour la même chose : **« ce discours dure neuf minutes et j'en ai six »**. Le chemin, c'est `prompter_leer`, compter, réécrire et `prompter_escribir`. À 150 mots par minute (une lecture normale en français), six minutes font environ 900 mots. `prompter_documentos` vous donne la durée de chacun sans l'ouvrir.

## Comment écrire un texte qui sera lu à voix haute
- **Une ligne vide sépare les paragraphes**, et le contrôle peut sauter de l'un à l'autre : mieux vaut que chaque paragraphe soit une idée, ainsi l'opérateur peut en sauter un s'il faut raccourcir en direct.
- **Pas de markdown** : ni astérisques, ni dièses, ni tirets de puce. Sur la tablette, cela s'affiche tel que c'est écrit.
- Des phrases courtes. Ce qui se lit bien sur papier ne se dit pas toujours bien.
- Des nombres ronds et écrits comme ils se prononcent (« trois mille cinq cents », pas « 3500 »).

## Pendant que quelqu'un le lit
`prompter_control` lance et arrête le texte, change la vitesse, saute et écrit des messages à l'intervenant (« Concluez »). **De l'autre côté, il y a une personne qui lit à voix haute** : lancer ou arrêter le texte sans qu'on vous le demande se remarque instantanément à l'antenne. Si l'intervenant va plus lentement que le texte, baissez la vitesse de 10 mots/min à la fois au lieu de l'arrêter.

`prompter_estado` indique aussi quels écrans sont connectés : avant que quelqu'un monte au pupitre, ce qui compte, c'est qu'il y ait au moins une vue `orador`. S'il n'y en a aucune, la tablette est éteinte ou personne n'a ouvert le lien, et il faut le signaler MAINTENANT, pas quand l'intervenant sera déjà là.
