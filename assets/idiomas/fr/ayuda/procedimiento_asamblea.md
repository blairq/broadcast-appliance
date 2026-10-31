La procédure standard pour une **séance d'assemblée** (conseil municipal, conseil, conseil d'administration, copropriété) : membres à leurs sièges, quorum, ordre du jour, votes et, s'il y en a, membres à distance. Une séance **n'est pas un conducteur de tops** : la parole change sans ordre préalable et c'est la file des intervenants qui la gère. Suivez-la dans l'ordre, **proposez avant de faire** et n'inventez pas de données. Pour le détail de chaque élément : help asamblea, conferencia et sesion.

**0. Ce qu'il faut demander :** nom de l'organe et de la séance ; date, heure et **fuseau** ; la **liste des membres** avec siège, nom, fonction et groupe (de préférence un tableur) ; qui **vote** ; la **règle du quorum** et la majorité ; l'**ordre du jour** (points et dossiers) ; s'il y a une **unité centrale de conférence** (micros des sièges) ou si l'on travaille sans ; s'il y a des **membres à distance** ; où l'on diffuse et si l'on enregistre ; s'il y a un hymne, une minute de silence ou d'autres moments fixes.

**1. Configurer :**
| Quoi | Avec quoi |
|---|---|
| L'événement comme assemblée et son conducteur (seulement ce qui a un ordre : Previa, ouverture, clôture) | `evento_armar {nombre, fecha, inicio, huso, asamblea: true, gente, bloques}` |
| L'Assemblée plénière : chaque membre avec son siège, sa fonction et son groupe (validés) | `cuerpo_importar` (tableur), `integrante_editar`, `integrante_validar` |
| L'hémicycle : la grille des sièges | `recinto_editar {filas, columnas, auto: true}` |
| Le plan de chaque siège | `ptz_posicion_crear` et `banca_toma {banca, camara, posicion}` ; tester avec `enfocar {que: "banca N"}` |
| LES SCÈNES des sièges, plan large, membre à distance qui a la parole et débat | `asamblea_escenas_base` (montrez la proposition ; `crear: true`) |
| L'unité centrale de conférence (s'il y en a une) | `dcn_conectar` ; chaque siège avec son unité (help conferencia) |
| Les membres à distance (s'il y en a) | `sesion_credencial {banca}` (lien et PIN), `sesion_acceso_opciones` ; la console de la présidence : `sesion_consola {accion: crear}` |
| La liste des membres avec qui vote | `evento_armar {gente: [{…, vota: true}]}` ou `padron_anotar` |
| Le quorum | `quorum_regla` |
| L'ordre du jour | `orden_del_dia_cargar` |
| Les graphiques d'assemblée : synthé de membre, point de l'ordre du jour, quorum, résultat du vote, suspension de séance | ils se lient tout seuls à ce qui est validé (help asamblea, `grafico_listar` catégorie Asamblea) |
| Où l'on diffuse et l'enregistrement | comme pour un événement en salle (help procedimiento_salon, étape 1) |
| La page web de l'événement (affiche l'assemblée, l'hémicycle avec les sièges par groupe, le quorum et les votes) | `evento_web {accion: generar}` |
| Le début et la fin : mire, carton d'attente, carton de fin | `evento_preparar` |

**2. Contrôler :** `asamblea_revisar` (organe, hémicycle, presets, scènes, unité centrale, siège virtuel, ordre du jour) → `evento_revisar` → `evento_verificar {tipo: asamblea}`. Corrigez chaque « manque » et recommencez jusqu'à ce qu'il ne manque plus rien.

**3. Demander à la personne de contrôler :** la liste « a_mano » d'`evento_verificar` : en plus du général (caméras, son, graphiques, page, enregistrement, diffusion de test), l'**unité centrale testée siège par siège**, le **plan de chaque siège**, un **vote de test**, les **membres à distance avec leur lien et leur PIN** (un testé), la **console de la présidence** et **qui rédige le procès-verbal**. Ne cochez avec `evento_chequeo` que ce qu'on vous confirme.

**4. Répéter :**
- **Chaque siège :** lui donner la parole (`banca_al_aire`) et la lui retirer (`recinto_orador_terminar`) : la caméra bouge-t-elle, la scène se prépare-t-elle, le synthé sort-il avec le bon nom, le micro s'ouvre-t-il ?
- **Un membre à distance :** qu'il entre avec son lien et son PIN, l'autoriser, lui donner la parole.
- **Vote de test :** avec l'unité centrale ou avec le siège virtuel (`sesion_votacion`) ; le résultat officiel (`votacion_resultado_oficial`) à l'écran.
- **Quorum :** `quorum_presentes` avec les présents.
- Et ce qui vaut pour tout événement : musique, graphiques, page web, enregistrement et diffusion de test (help procedimiento_salon, étape 4).

**5. Le laisser prêt :** `evento_verificar {tipo: asamblea}` sans manques et avec tout confirmé ; `evento_preparar {al_aire: true}` laisse la mire dans le programme. Prévenez la personne.

**6. Le jour J, de la mire à la fin :**
1. **30 à 15 minutes avant :** `evento_iniciar` ; mire ; `stream_start` et `record_start` ; confirmer sur la plateforme.
2. **Avant de commencer :** carton d'attente avec musique ; les membres à distance entrent et sont autorisés ; `quorum_presentes`.
3. **Ouverture :** avec le quorum, la première séquence (hymne, ouverture par la présidence).
4. **La séance :** la file des intervenants (`banca_cola`, `banca_al_aire`, `recinto_orador_terminar`) ; le point en cours d'examen (`orden_del_dia_punto`) ; les votes et leur résultat officiel ; suspension de séance avec son carton si nécessaire. Le registre va tout seul au procès-verbal (`sesion_registro`).
5. **Clôture :** la séquence Clôture (carton de fin) ; `stream_stop`, `record_stop`, `evento_terminar` et `evento_informe`.
