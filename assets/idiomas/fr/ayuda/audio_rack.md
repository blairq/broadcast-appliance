## Comment c'est construit
Chaque source avec audio (micros, captures, vidéos) peut avoir un **rack** de type JACK : `in` (la source brute) -> modules LSP reliés par des câbles -> `out` (ce qui part vers le mixage). La personne le voit dans l'onglet **Audio** (patchbay) ; `audio_show {"source"}` l'ouvre sur cette source.
- `audio_sources` : sources avec audio et si elles ont un rack. `audio_rack {"source"}` : nœuds, câbles et vumètres.
- `audio_plantillas` : enregistrer un rack sous un nom et l'appliquer à une autre source. Marquée `porDefecto` pour `ba_guest_audio`, les NOUVEAUX invités la prennent tout seuls en se connectant : c'est la seule façon qu'ils sonnent bien sans rien leur préparer pendant que l'événement commence.
- Chaque module a ses propres vumètres : `entradaDb`, `salidaDb` et `cambioDb` (ce qu'il baisse ou monte MAINTENANT : sur un gate/compresseur, c'est la réduction de gain).
- Un rack vide ne change pas le son. `audio_module_remove {"node": "*"}` retire tout le rack.
- Dans l'onglet, chaque carte se range dans une grille (bouton « Ranger » pour tout aligner) et son chevron ouvre le détail : nom réel de chaque port du module et s'il est mono ou stéréo.

## Ports et canaux (L/R)
Un module peut avoir plusieurs entrées et sorties : `in` (principale), `sc` (sidechain) et, s'il en a davantage, `in2`, `in3`... et `out`, `out2`... (`audio_rack` les indique dans `entradas`/`salidas`, et dans `puertosEntrada`/`puertosSalida` avec leur nom réel et leur nombre de canaux). Une console LSP à 16 entrées les indique toutes les 16.
- Par défaut, un câble transporte le **port entier** : gauche avec gauche et droite avec droite.
- On peut câbler **un canal isolé** en ajoutant `canalSalida` et `canalEntrada` ("L" ou "R") dans `audio_patch` : par ex. envoyer la droite d'un module vers la gauche d'un autre, ou n'utiliser qu'un canal et laisser l'autre en silence.
- Entre les deux mêmes ports, un câble de port entier et un câble de canal ne coexistent pas : le dernier remplace le précédent (sinon l'audio serait additionné deux fois).

## Écouter sur une sortie de l'appareil (casque)
Sert à vérifier le résultat à un point du rack SANS toucher à ce qui part à l'antenne ni au mixage du programme.
1. `audio_outputs` : les sorties de cette machine, regroupées par interface.
2. `audio_output_set {"source", "salida": <id>}` : ajoute au graphe de cette source une carte portant le nom de la sortie.
3. `audio_patch {"source", "conectar": [{"desde": "in", "hacia": "salida-audio"}]}` : c'est seulement là qu'on entend (on peut brancher la sortie de n'importe quel module, pas seulement `in`).
ATTENTION : le moteur lit sur UNE sortie à la fois ; en choisir une autre sur une autre source change celle de toutes.

## Micros et entrées audio de l'appareil
Ils n'apparaissent pas tout seuls : il faut les ajouter. `audio_devices` les liste (`inputs`) et `audio_input_add {"device"}` en ajoute une. Elle va sur un canal GLOBAL (comme l'audio des invités) : elle s'entend dans TOUTES les scènes, ce n'est pas un élément de scène et elle reste enregistrée pour le prochain démarrage. Ensuite, elle se traite comme n'importe quelle source avec son rack.

## Règle d'or : mesurer, changer, mesurer de nouveau
1. `audio_analyze {"source", "punto": "in", "segundos": 5}` : crête, RMS, LUFS, `pisoDeRuidoDbfs`, `nivelActivoDbfs`, `rangoDinamicoDb`, spectre par tiers d'octave et par zones (graves, corps, médiums, présence, sifflantes, air), écrêtages et `pistas`. S'il indique qu'aucun audio n'arrive, la source est muette, inactive ou personne ne parle : demandez à la personne de parler (`operator_ask`).
2. Changer avec les outils nommés (valeurs en unités réelles : dB, ms, Hz).
3. Vérifier : `audio_analyze {"punto": "out"}` et/ou `audio_measure` (copies hors ligne, ne touche pas à l'antenne).
Dites à la personne ce que vous avez mesuré et ce que vous avez changé, avec les chiffres.

## Réglages typiques (voix)
- **Gate** (`audio_gate`) : mesurer DEUX fois, avec la personne silencieuse (bruit de fond) et en train de parler (niveau actif). `umbral_db` = bruit de fond + 6 à 10 dB, et toujours bien en dessous du niveau actif. `reduccion_db` -20 à -40 (pas besoin de coupure totale), `ataque_ms` 1-5, `sosten_ms` 50-100, `liberacion_ms` 100-250. Vérifier avec `audio_measure {"tipo": "curva"}` (où il s'ouvre) et en écoutant qu'il ne coupe pas les fins de mots. Si le bruit de fond et la voix sont à moins de ~15 dB, le gate ne suffit pas : suggérer de rapprocher le micro ou de baisser le gain d'entrée.
- **Compresseur** (`audio_compressor`) : preset `voz` et ajuster `umbral_db` pour que `cambioDb` indique -3 à -6 dB sur les passages forts (pas tout le temps). `ratio` 2-4, `ataque_ms` 5-20, `liberacion_ms` 80-200, `ganancia_db` pour récupérer le niveau.
- **De-esser** (`audio_deesser`) : regarder la zone `sibilancia` (5-9 kHz) de l'analyse. `deteccion_hz` sur la crête des sifflantes (5-7 kHz en général), `umbral_db` pour qu'il baisse de 3-6 dB seulement sur les sifflantes. `escuchar: true` permet d'entendre ce qu'il détecte (seulement pour régler : pensez à le désactiver).
- **EQ** (`audio_eq`, par `bandas`) : passe-haut à 70-100 Hz pour la voix ; si la zone `cuerpo` (200-400 Hz) ressort, cloche à -2 à -4 dB ; s'il manque de clarté, +1 à +3 dB vers 3-5 kHz. De petits changements. Vérifier avec `audio_measure {"tipo": "respuesta"}`.
- **Multibande** (`audio_multiband`) : quand un seul compresseur pompe ou que le problème est dans une zone (graves qui résonnent, aigus qui s'envolent). `corte_hz` et seuils par bande.
- **Limiteur** (`audio_limiter`, preset `seguridad`) : en fin de chaîne, plafond à -1 dBFS. C'est un filet de sécurité, pas du volume.
- Ordre habituel : EQ passe-haut -> gate -> compresseur -> de-esser -> EQ fin -> limiteur. `audio_patch` reconnecte ; `audio_bypass` compare avec/sans.

## Loudness
Pour la diffusion, visez -16 à -14 LUFS intégrés sur le mixage (YouTube/Twitch normalisent autour de -14). Une voix seule : -18 à -16 LUFS en `out`. Les crêtes qui touchent 0 dBFS (`recortes`) se corrigent avec le gain d'entrée, pas avec le limiteur.

## Générateur et autres modules
- `audio_generator {"senal": "rosa"|"seno"|"barrido"|"blanco", "nivel_db", "conectar_a"}` crée un signal de test ISOLÉ (par ex. vers un analyseur de `audio_module_add {"module": "analizador"}`). Le brancher sur ce qui part à l'antenne exige `al_aire: true` : confirmez avec la personne avant.
- `audio_modules {"buscar"}` liste les ~200 plugins LSP ; `audio_module_add` en ajoute n'importe lequel et `audio_module_get`/`audio_module_set` lisent et modifient tous ses paramètres.
- `audio_editor` ouvre l'interface graphique d'origine du module LSP (pour la personne).
