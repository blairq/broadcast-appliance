## L'idée
Les entrées **synchronisées** (classe `sala` : caméras, cartes d'acquisition et micros du lieu) sortent alignées entre elles et avec l'audio : le studio attend la plus lente. C'est pourquoi la latence du studio est fixée par UNE entrée (la plus lente des synchronisées) : pour la réduire, il faut améliorer celle-là. Celles de classe `propia` (invités par WebRTC, séance à distance, fenêtres, écrans partagés) n'entraînent pas les autres : si elles arrivent en retard, on décale leur horloge, audio et vidéo ensemble.

La latence **ne bouge pas à chaque changement de scène** : la modifier en direct fige ou fait sauter les sorties. En direct, elle ne fait que monter (si c'est demandé de façon soutenue pendant 2 s, une fois, avec un avertissement) ; elle baisse toute seule quand rien n'est à l'antenne, ou sur demande.

## Comment on travaille
1. `estudio_latencia` : la latence fixée, `la_marca` et chaque entrée avec sa classe, ce qu'elle demande et ce qu'elle met.
2. Si elle est fixée par quelque chose qui n'a pas à être aligné avec la salle (un invité, une fenêtre, une caméra qui filme autre chose) : `sincronia_clase {"entrada", "clase": "propia"}` (hors antenne : l'entrée est reconstruite).
3. Si elle est fixée par une caméra de la salle : `tiempos_pipeline {"entrada"}` montre si c'est la capture (essayez un autre mode : `video_devices {"device"}`) ou une carte.
4. Avec le nouveau réglage en place et rien à l'antenne, elle baisse toute seule ; sinon, `estudio_latencia {"bajar": true}` (avec quelque chose à l'antenne il y a un saut : prévenez l'opérateur).
5. Regardez `cuadros_tarde_60s` dans estudio_latencia : il doit valoir 0. Chaque entrée demande son âge p90 + sa gigue + 5 ms, et le compositeur vidéo n'ajoute que 5 ms (`video.margenComposicionMs`) ; si des images arrivent en retard, cette entrée tremble plus que ce qu'elle a demandé.
6. Pour vérifier que deux caméras sont alignées : une horloge (celle d'un téléphone, avec les millisecondes) devant les deux et `video_sincronia {"a", "b"}` : le décalage doit être inférieur à une image.

## Dites-le à l'opérateur
Avec des chiffres : « la latence du studio est de 95 ms et elle est fixée par la Caméra 2 (capture 75 ms) ; l'invité a son propre retard, 40 ms de plus ».
