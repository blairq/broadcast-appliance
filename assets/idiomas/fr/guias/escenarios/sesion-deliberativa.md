# Séance d'un organe délibérant

## À quoi ça sert
Une séance formelle avec les membres à leurs sièges, présidence, secrétariat, ordre du jour,
temps de parole réglementé, quorum et votes nominaux avec résultat officiel. Publique et diffusée.
Lisez aussi help {"topic": "asamblea"} et la procédure de séance de l'assemblée.

## Questions propres
- Quel organe, combien de sièges, qui préside, qui est secrétaire ?
- L'Assemblée plénière est-elle chargée (membres, groupes, sièges) ? Sinon : cuerpo_importar ou
  cuerpo_plantilla.
- Y a-t-il une unité centrale de conférence (micros de siège, par ex. Bosch DCN) ? Des caméras PTZ
  avec une position par siège ?
- Des membres à distance (siège virtuel) ? Votent-ils à distance ?
- L'ordre du jour et les dossiers. Des majorités spéciales sur certains points ?
- Temps de parole ? Questions de privilège, répliques ?
- Diffusion, interprète en langue des signes (couche DSK), sous-titres ?
- Des téléviseurs dans l'hémicycle pour afficher le vote ?

## Quoi préparer
- Assemblée plénière et hémicycle (recinto_editar) avec chaque siège et son preset de caméra
  (ptz_posicion_crear).
- Scènes de base de l'assemblée (asamblea_escenas_base) : présidence, siège qui a la parole, plan
  large, vote, suspension de séance ; vérifiez avec asamblea_revisar.
- Ordre du jour (orden_del_dia_cargar) avec les documents par point.
- Quorum (quorum_regla) et vote (votacion_abrir / votacion_resultado_oficial).
- S'il y a des membres à distance : sesion_habilitar, identifiants (sesion_credencial) et test de
  chacun.
- Écrans de l'hémicycle avec le vote (pantalla_mostrar contenu votacion).
- Surface de contrôle de la présidence ou des micros si l'on opère depuis une tablette (modèles
  « Assemblée », « Micros des sièges »).

## Vérifier
Unité centrale connectée (dcn_conectar), caméras sur leurs positions, quorum avec la vraie liste
des membres, un membre à distance qui vote pour tester, la page web publique avec l'ordre du jour.

## Livrer
Identifiants pour les membres à distance, la page web publique, la surface de contrôle de la
présidence ; à la fin, le procès-verbal avec les votes (evento_acta) et l'enregistrement avec des
chapitres par point (evento_capitulos).
