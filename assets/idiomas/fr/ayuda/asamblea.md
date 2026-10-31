Une séance d'assemblée (conseil municipal, conseil, conseil d'administration, copropriété) n'est pas un conducteur de tops : la parole change sans ordre préalable et chaque siège doit être vu et entendu quand c'est son tour. Elle se prépare dans cet ordre, et `asamblea_revisar` dit à chaque instant ce qui manque et comment le corriger. **Lancez-le au début et à la fin.**

1. **L'Assemblée plénière :** chaque membre avec son siège (`cuerpo_importar` depuis un tableur, ou `integrante_editar`). Sans siège, il n'apparaît pas dans l'hémicycle.
2. **L'hémicycle :** la grille des sièges tels qu'on les voit depuis la régie (`recinto_editar {filas, columnas, auto: true}`).
3. **Le plan de chaque siège :** pour chaque siège, une position PTZ qui le cadre (`ptz_posicion_crear`) associée au SIÈGE (`banca_toma {banca, camara, posicion}` ; elle reste attachée au siège même si la personne assise change, et un siège vide en a une aussi). Les scènes de l'intervenant et du débat utilisent la source « Banca con la palabra » (le siège qui a la parole) (`asamblea_escenas_base {crear: true}`) : elle montre le plan de la personne qui parle (emplacement 1 ; dans un débat, aussi le 2). Avec une caméra numérique, elle le recadre dans le plan entier sans rien bouger (le plan large reste identique et deux sièges se voient en même temps) ; une caméra motorisée va toute seule à la position. « Cadrer le siège 10 / Jean Dupont / la Présidence » : `enfocar {que}`.
4. **LES SCÈNES** (ce qu'on oublie le plus) : quand on donne la parole à un siège, on PRÉPARE la scène qui montre sa caméra ; si aucune scène ne la montre, personne ne passe. Il faut :
   - une scène par caméra de sièges (avec le synthé `{orador}` si on veut le nom à l'écran) ;
   - un plan large ;
   - une avec « Remoto con la palabra » (le membre qui parle depuis son téléphone) ;
   - celles du débat : deux membres à distance côte à côte (« Remoto con la palabra » et « Remoto con la palabra 2 »), un de la salle et un à distance, deux de la salle ;
   - s'il y a un vote à l'écran, celle du résultat.
   `asamblea_escenas_base` les propose toutes prêtes avec les caméras et entrées existantes (montrez la proposition à l'opérateur) et avec `crear: true` il les crée et configure le débat. Ensuite on peut les retoucher (synthé, fond, logo).
5. **L'unité centrale de conférence** (s'il y en a une) : `help {"topic": "conferencia"}` ; chaque siège avec son unité.
6. **Les membres à distance :** `help {"topic": "sesion"}` ; lien et PIN pour ceux qui siègent à distance ; ce qu'ils voient sur leur téléphone (`sesion_vistas`).
7. **L'ordre du jour** (`orden_del_dia_cargar`) : il s'affiche sur les téléphones et dans la console de la présidence.
8. **Le débat** (s'il y en a un) : `debate_config` avec la scène de chaque type et le temps de chaque tour.
9. **Le conducteur** (`evento_armar`) pour ce qui a un ordre : ouverture, hymne, clôture. Le corps de la séance est géré par la file des intervenants (`banca_*`), pas par les tops.

Pendant la séance : `banca_al_aire` / `recinto_orador_terminar` (la parole), `presidencia_callar_todos` et `presidencia_hablar`, `debate_iniciar` / `debate_turno`, vote avec `dcn_votacion` ou `sesion_votacion`.

Pour tester ou montrer tout cela sans toucher au réel, il existe une démo complète (un conseil de 15 membres, hémicycle, unité centrale simulée, scènes et débat) : `configuracion_crear {"nombre": "Demo", "desde": "demo:Demo parlamentaria"}` et `configuracion_cambiar` (voir `help {"topic": "configuraciones"}`).
