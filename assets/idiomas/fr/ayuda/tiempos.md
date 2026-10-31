## De l'extérieur vers l'intérieur
1. `tiempos_mapa` : de chaque entrée à chaque sortie, ce qu'ajoute chaque nœud (entrée, carte, module audio, scène, mixage, sortie) et le cumul. `salidas[].camino` est le chemin le plus lent : c'est par là qu'on commence.
2. `tiempos_pipeline {"entrada"}` : l'entrée suspecte vue de l'intérieur, élément par élément (quelques secondes de sondes ; ne touche pas à l'antenne). Lisez d'abord `hallazgos` : ils arrivent déjà analysés, par gravité, avec la carte coupable.
3. `tiempos_plugin {"entrada", "tarjeta"}` : la carte coupable, comment elle travaille (ses mémoires, propriétés, documentation) et ce qu'elle fait dans cette chaîne. `tiempos_plugin {"elemento"}` des alternatives pour comparer (`interior.gpu`).
4. Si vous avez changé quelque chose, `tiempos_pipeline` de nouveau et comparez. Pour voir dans le temps : `tiempos_evento_agregar` (une note) et `tiempos_evento_impacto`.

## Comment lire les mesures
- `agrega_ms` = âge de l'image à la sortie de l'élément moins à l'entrée. Cela compte ce qu'il MET et ce qu'il RETIENT (un filtre temporel qui garde des images ajoute même s'il traite vite). Le `ms_por_cuadro` que rapporte un plugin, c'est seulement ce qu'il met.
- `memoria_*` est ce qu'il a négocié ; `puede_*` ce qu'il sait utiliser. Un passage (`baja` du GPU vers la RAM, `sube` de la RAM vers le GPU) copie l'image entière : en 1080p, c'est ~8 Mo par image, plus de CPU et de bande passante mémoire. Un **aller-retour** (descend puis remonte) est presque toujours un plugin qui ne travaille qu'en RAM entre deux qui travaillent sur le GPU.
- Si un plugin n'accepte que « sistema » : il n'a pas de version GPU ; cherchez-en un autre (video_modulos, les variantes gl/d3d12) ou placez-le au début ou à la fin de la chaîne, là où la vidéo est déjà en RAM, pour ne pas la couper.
- S'il `puede` GPU mais a négocié la RAM : ce qui est à côté l'y a obligé (un autre plugin seulement-RAM accolé, ou un format que le GPU ne gère pas).
- `declara_ms` est ce que l'élément annonce au pipeline (c'est avec cela que le studio compense). `declara_menos` : il met plus que ce qu'il annonce ; si la différence s'approche du tampon de l'entrée, les images arrivent en retard au mixage → augmenter le tampon (`latencia_ms` de l'entrée) ou changer de plugin.
- `conversion_cpu` : videoconvert/videoscale en RAM. Beaucoup de ms en 1080p = il vaut mieux que l'étape précédente livre le format que demande la suivante.
- Une file avec un `nivel_cola_ms` élevé stocke de la vidéo : quelque chose après est lent ou attend.

## Résolution : que chaque entrée livre ce qui est vu
`resolucion_plan {"entrada"}` croise l'endroit où l'entrée est vue (sa plus grande taille dans les scènes) avec chaque carte et la caméra : la super-résolution qui agrandit quelque chose qui couvre déjà ce qui est vu, le PTZ après la super-résolution, le zoom numérique qui étire peu de pixels, la caméra qui livre trop ou trop peu (avec le mode qui convient), les ips trop faibles. Les mêmes constats colorent les cartes (jaune / orange / rouge) dans Vidéo et dans Latences. Les modes réels de chaque caméra (brut, MJPEG, H.264 et qui décode) : `video_devices {"device"}` ; tout ce qu'elle annonce : `dispositivos_video`.

## Dites-le à l'opérateur
En français et avec la conséquence : « le fondu par mouvement retient 800 ms (24 images) et travaille en RAM : il oblige à descendre et remonter la vidéo du GPU ». Proposez le changement et attendez le feu vert avant de toucher à une carte à l'antenne.
