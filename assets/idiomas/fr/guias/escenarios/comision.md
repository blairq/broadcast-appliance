# Réunion de commission

## À quoi ça sert
La commission examine les dossiers d'un ordre du jour et rend des avis. Membres de l'organe,
conseillers et invités (fonctionnaires, habitants) qui exposent. Elle est souvent publique et
enregistrée.

## Questions propres
- Quelle commission, combien de membres, qui préside ? Y a-t-il des membres à distance ?
- L'ordre du jour ? (Demandez le texte : orden_del_dia_cargar accepte qu'on le colle.)
- Des invités viennent-ils exposer ? Combien de temps chacun ? Apportent-ils une présentation ?
- Vote-t-on l'avis ? Faut-il un quorum ? Un quorum de combien ?
- Est-ce diffusé ? Publie-t-on la documentation de chaque dossier ?

## Quoi préparer
- Ordre du jour chargé (orden_del_dia_cargar) et documentation de chaque dossier rattachée à son
  point (documento_agregar avec `puntos`).
- Liste des membres (vota: true) et des invités avec leur rôle.
- Scènes : « Présidence », « Membre qui a la parole », « Exposé de l'invité » (avec sa
  présentation), « Vote » si l'on vote ; synthé avec le nom et la fonction de la personne qui
  parle.
- Temps de parole (speakers_config, debate_config s'il y a des répliques).
- Si l'on vote : quorum (quorum_regla) et vote (votacion_abrir).

## Vérifier
Quorum calculé avec la liste des membres ; les invités à distance testés ; les documents sur la
page web.

## Livrer
La page web de l'événement avec l'ordre du jour et les documents ; les invitations ; à la fin, le
procès-verbal (evento_acta) avec ce qui a été examiné et voté.
