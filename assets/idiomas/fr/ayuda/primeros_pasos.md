## Ce que c'est
Un studio de diffusion complet dans une seule application :
- **Programme** : ce qui passe à l'antenne. C'est la **scène à l'antenne** (`scene_switch` la change, avec une transition).
- **Scènes** : compositions de **sources** (caméras, images, textes, vidéos, invités, couleurs...). Une source peut se trouver dans plusieurs scènes (c'est la même).
- **Sorties** : diffusion RTMP (`stream_start`) et enregistrement (`record_start`).
- **Conférence** : les invités entrent depuis le navigateur (WebRTC). Ils attendent d'abord dans une **salle d'attente** privée (ils ne voient que vous) ; une fois admis, ils occupent une **place** (slot 1..N) et leur caméra est la source `Invitado N`. Avec le **mode intervenants**, au lieu d'entrer directement, ils font la queue et passent à l'antenne à tour de rôle avec un chrono.

## Premier coup d'œil (recommandé)
1. `status` — ce qui est à l'antenne, si l'on diffuse ou enregistre, état de santé.
2. `foto_programa` — voir le programme.
3. `scene_list` / `foto_todas_escenas` — toutes les scènes.
4. `room_status` — qui est dans la conférence.

## Conventions
- Positions en **fractions du canevas** (0..1), ou presets (`full`, `pip-br`, `grid-4-2`...). Voir le sujet `fuentes`.
- Volume en **dB** (0 = pas de changement). Temps des intervenants en secondes ou en `m:ss`.
- Les outils enregistrent la configuration tout seuls.
- Les erreurs expliquent ce qui s'est passé et comment continuer : lisez-les et corrigez.
