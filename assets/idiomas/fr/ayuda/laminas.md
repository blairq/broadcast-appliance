Une série d'images qu'affiche une scène. **Ce n'est pas le `carrusel_*` des écrans** : celui-là fait tourner des messages et des QR sur un téléviseur secondaire. Ici, c'est la présentation de l'intervenant.

## À quoi ça sert vraiment
En plein exposé, l'intervenant dit « revenez à celle du graphique en barres ». Avec vingt diapositives et un bouton suivant, ce sont vingt clics et dix secondes d'antenne perdues. `laminas_ir {"carrusel", "numero"}` est un saut.

## Comment l'utiliser
- `carruseles_listar` : ce qui existe. C'est par là qu'on commence ; le reste demande le nom.
- `carrusel_laminas_ver {"carrusel"}` : les cartons dans l'ordre, avec leur numéro, leur titre et leurs étiquettes. Le **numéro commence à 1** et c'est ce que demande `laminas_ir`.
- `laminas_armar {"nombre", "imagenes"}` : en monter un à la main avec des cartons séparés (logos, fonds). S'il existe déjà, ajoute à la fin.
- `laminas_editar {"carrusel", "accion"}` : retirer un carton, le déplacer, renommer, supprimer ou le détacher de la scène. `borrar` emporte le carrousel, PAS les images.
- `laminas_aplicar {"carrusel", "escena"}` : **c'est l'étape qui manque presque toujours**. Tant que le carrousel n'est pas DANS une scène, `laminas_ir` déplace le pointeur et on ne voit rien.
- `laminas_desarmar {"archivo", "nombre"}` : convertit un PDF ou un .pptx en cartons. **Cela prend du temps** (quelques secondes par page) et n'attend pas : il prévient qu'il a commencé, et quand il a fini cela apparaît dans `carruseles_listar`. Un .pptx passe par DeckCraft (embarqué dans le programme) ; l'ancien .ppt et le .odp, à exporter en .pptx ou en PDF.

## Ce qu'on cherche et comment
L'opérateur tape « barres » dans une case et obtient ce carton. Cela marche si quelqu'un a regardé les images avant : `laminas_sin_analizar {"carrusel"}` donne le suivant sans étiquettes, on le regarde avec `imagen_preguntar` (c'est lui qui sait regarder : l'assistant principal NE reçoit PAS d'images), et ce qu'on a vu s'enregistre avec `laminas_etiquetar {"carrusel", "numero", "etiquetas"}`. Cela se fait **une fois par carton** ; si le résultat est vide, il est quand même marqué, pour ne plus jamais le regarder.

Les étiquettes sont des mots avec lesquels quelqu'un de pressé chercherait : ce qu'on voit, ce qui est écrit, de quoi il s'agit. « graphique barres ventes trimestre », pas une description.
