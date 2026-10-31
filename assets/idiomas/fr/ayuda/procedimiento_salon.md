La procédure standard pour un événement **en salle** : public dans la salle, intervenants, diffusion et enregistrement, et si on le demande des invités à distance et des questions du public. Suivez-la dans l'ordre ; chaque étape indique avec quel outil. **Proposez avant de faire** ce qui modifie l'événement ou passe à l'antenne, et n'inventez pas de données : ce qu'on ne vous a pas dit, demandez-le.

**0. Ce qu'il faut demander** (avant de préparer quoi que ce soit) : nom de l'événement ; date, heure de début et **fuseau** (si quelqu'un se connecte d'ailleurs) ; lieu ; durée et parties (séquences) avec leur durée ; qui parle dans chaque partie, avec **nom et fonction** tels qu'ils apparaîtront à l'écran ; s'il y a des **invités à distance** ; si le **public pose des questions et vote** ; **où l'on diffuse** (YouTube, Twitch, Facebook, un autre serveur) et si l'on enregistre ; musique d'attente et de clôture ; vidéos à passer ; s'il y a une **accréditation** des participants ; s'il faut un prompteur ou des sous-titres.

**1. Configurer** (tout ce qui doit être en place) :
| Quoi | Avec quoi |
|---|---|
| L'événement et son conducteur : séquences avec durées et tops (scène, intervenant avec son temps, vidéo, musique, graphique, prompteur, note, attente) | `evento_armar {nombre, fecha, inicio, huso, invitados, preguntas, bloques}` ; ensuite `guion_bloque` / `guion_cue` |
| Les scènes : chaque caméra, plan large, écran partagé, invités | `scene_define`, `scene_list` (help escenas) |
| Musique et vidéos de l'événement (les tops les nomment) | `media_list`, `media_find` (help medios) |
| Synthés de chaque intervenant (nom et fonction bien écrits) | graphique `zocalo` dans les tops de type graphique ; `grafico_listar`, `grafico_probar` |
| Micros de la salle et niveaux | `audio_input_add`, `audio_measure`, `audio_loudness` (help audio) |
| Où l'on diffuse | comptes : `account_setup_guide`, `accounts_status` ; serveur propre : `stream_destino_agregar` |
| L'enregistrement (disque et qualité) | `record_disco`, `record_ajustes` |
| La page web de l'événement (une page par séquence, se met à jour toute seule) | `evento_web {accion: generar}` ; `evento_web {accion: sitio, participacion: true}` si le public participe ; où le voir en direct : `sitio {vivos}` |
| Invités à distance (s'il y en a) | `room_link`, `room_pin` ; salle d'attente avec `room_set` ; qui est là : `invitado_chequeo` |
| Questions du public (s'il y en a) | la page du public (QR) ; modération avec `preguntas_ver` / `pregunta_moderar` |
| Participants et accréditation (s'il y en a) | `padron_anotar` ; à l'entrée, `padron_puerta` |
| Le début et la fin : mire, carton « Avant de commencer », carton de fin, séquences Previa et Cierre | `evento_preparar {musica_espera, musica_cierre, bajada, cierre}` |

**2. Contrôler** (l'automatique) : `evento_revisar` (scènes, fichiers, disque, destination, entrées sans signal) → `evento_ensayo_rapido` (parcourt tout le conducteur) → `evento_verificar` (la liste de contrôle complète de ce type d'événement). Corrigez chaque « manque » avec ce que dit `como` et relancez `evento_verificar` jusqu'à ce qu'il ne manque plus rien.

**3. Demander à la personne de contrôler** : `evento_verificar` laisse dans la liste de vérification de l'événement ce que seule une personne qui regarde et écoute peut confirmer (état « a_mano »). **Montrez toute cette liste** à la personne qui vous a demandé l'événement et demandez-lui de confirmer chaque point ; au fur et à mesure qu'elle vous dit oui, cochez-le avec `evento_chequeo {item, hecho: true, nota}`. Ne cochez rien qu'on ne vous ait pas confirmé.

**4. Répéter** (tester chaque élément, sans passer à l'antenne) :
- **Musique :** `audio_escuchar {name}` l'envoie dans le casque de l'opérateur ; qu'il l'écoute et dise si le volume est bon.
- **Vidéos :** `scene_preview` avec la scène de la vidéo et `foto_fuente` pour la regarder ; avec son audio.
- **Graphiques :** `grafico_probar` avec les vraies données de chaque synthé et carton (regardez les images ; noms et fonctions bien écrits).
- **Page web :** `evento_web {accion: ver}` et le QR avec `qr_mostrar` pour l'ouvrir sur un téléphone.
- **Enregistrement de test :** `record_start`, une minute, `record_stop`, et qu'on le relise.
- **Invité de test** (s'il y en a) : `invitado_prueba` : on le voit, on l'entend et il nous entend.
- **Question de test** (s'il y en a) : que quelqu'un en envoie une depuis son téléphone et modérez-la.
- **Diffusion de test :** avec la mire à l'antenne, `stream_start` et `stream_salud` ; que la personne la voie sur la plateforme (barres, heure qui défile, tonalité, que ce soit le bon événement). S'il reste longtemps avant le début, `stream_stop`.
- **Tout le parcours :** `evento_correr` avec la répétition activée (help del evento) pour voir l'ordre réel des scènes.

**5. Le laisser prêt à commencer :** `evento_verificar` sans manques et avec tout confirmé ; ensuite `evento_preparar {al_aire: true}` laisse la mire dans le programme. Prévenez la personne que c'est prêt et de ce qui reste à faire le jour J.

**6. Le jour J, de la mire à la fin :**
1. **30 à 15 minutes avant :** `evento_iniciar` ; la Previa du conducteur démarre avec la mire (`evento_correr`) ; `stream_start` et `record_start`. Confirmer qu'on voit et qu'on entend sur la plateforme (le top d'attente le demande).
2. **10 à 5 minutes avant :** `evento_correr` → carton « Avant de commencer » avec la musique d'attente.
3. **C'est l'heure :** `evento_correr` vers la première séquence.
4. **Pendant :** `evento_correr` pour avancer ; synthés à temps ; questions modérées ; surveiller `stream_salud` et `audio_loudness`.
5. **Clôture :** la séquence Cierre met la musique et le carton de fin ; quand c'est terminé, `stream_stop`, `record_stop`, `evento_terminar` et `evento_informe` (la durée de chaque partie, pour la prochaine fois). La page web passe toute seule à la clôture.
