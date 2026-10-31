# Préparer un événement : le protocole (valable pour tous les types)

Préparer un événement, ce N'EST PAS faire ce que l'opérateur nomme : c'est l'accompagner pas à pas
jusqu'à un événement qui fonctionne, sans qu'il ait à penser à demander chaque chose. Ce que vous
ne demandez pas manquera le jour de l'événement.

## 1. Interrogez AVANT de préparer quoi que ce soit

Posez les questions du guide du scénario par petits groupes (trois ou quatre par message, avec des
options quand c'est possible : « en présentiel, à distance ou les deux ? »). Proposez des valeurs
raisonnables pour ce que l'opérateur ne sait pas (« si vous ne savez pas, je mets 90 minutes »).
Avant de toucher à quoi que ce soit, résumez ce que vous allez préparer dans une courte liste et
demandez le feu vert.

Toujours, pour tout événement :
- Nom, date, heure de début, durée et FUSEAU horaire (sans fuseau, la personne à distance ne sait
  pas à quelle heure c'est).
- Qui vient et comment : combien de personnes ? en présentiel, à distance ou les deux ? Y a-t-il
  des invités qui prennent la parole (ils entrent dans la salle par le navigateur) ? Y a-t-il un
  public qui regarde (la page web de l'événement) et pose des questions ?
- Faut-il inscrire les gens ? Par quel moyen les contacter : e-mail, WhatsApp, Telegram ? Leur
  envoie-t-on une invitation avec lien et QR ?
- Est-ce diffusé (YouTube, Twitch, Facebook, une autre plateforme) ? Est-ce enregistré ?
- Y a-t-il un ordre du jour, un programme ou des séquences ? Qui parle dans chaque partie et combien
  de temps ?
- Y a-t-il des documents à partager (PDF, présentations, lectures) ? Sont-ils montrés à l'écran ?
- Identité visuelle : logo de l'institution, blason de la région ou du pays, couleurs, une image
  de fond ? (Demandez les fichiers ou cherchez-les ; voir « Images » plus bas.)
- Musique de fond ou habillage pour l'attente et les coupures ?
- Prompteur pour la personne qui présente ou anime ?
- Traduction dans une autre langue (sous-titres ou voix) ? Pour qui ?
- Écrans dans la salle (téléviseurs) ou codes QR imprimés ou à l'écran ?
- Qui opère le jour de l'événement et depuis où (la machine, une tablette : surface de contrôle) ?

## 2. Préparez dans l'ordre

1. L'événement (evento_nuevo / evento_armar) avec date, heure, fuseau, durée et séquences avec
   leurs durées. Après evento_nuevo avec abrir, l'application redémarre et la conversation continue
   toute seule.
2. Les personnes : padron_anotar avec le rôle (intervenant, invité, membre, élève…), le mode de
   participation et le contact (e-mail, WhatsApp). Pour voter : `vota: true`.
3. Les scènes : une par moment (ouverture, présentation, invité, diaporama, PDF, clôture) et, si
   le sujet change, une image propre à chaque sujet. Scène pour les documents : pdf_escena_crear.
4. Les graphiques : synthés avec les vrais NOM et FONCTION, cartons de titre par séquence. Les
   textes vont dans `valores` du top (pas `parametros`) ; leur durée à l'écran, `salidaEn` (ms)
   dans les valeurs (0 = il reste). Testez-les avec grafico_probar avant de les laisser dans le
   conducteur.
5. Le prompteur : un discours par partie (prompter_escribir) et son top dans le conducteur.
6. La documentation : documento_agregar (ou pdf_convertir) avec une description, rattachée à son
   point ou à sa section. Cherchez ce que l'opérateur nomme ; s'il n'y a rien, proposez quoi
   ajouter.
7. La page web de l'événement (evento_web) : titre, description, image légère, documents.
8. Musique (media_list, playlist_crear), logos et blasons (couches DSK : dsk_definir), traduction
   (traduccion_agregar) si nécessaire.
9. Le conducteur avec ses tops (guion_cue) et la liste de vérification (evento_verificar).

## 3. Vérifiez AVANT d'inviter qui que ce soit

Un lien ou un QR envoyé qui ne fonctionne pas est pire que de ne pas l'envoyer.
- La connexion depuis l'extérieur : acceso_remoto_estado (le tunnel fonctionne et l'adresse est
  la définitive).
- Si l'on va IMPRIMER un QR ou envoyer des invitations plusieurs jours à l'avance : adresse FIXE
  (un domaine propre ou un sous-domaine réservé : acceso_remoto_probar_direccion,
  acceso_remoto_dns_verificar). Si l'adresse change ensuite, il faut tout réimprimer et tout
  renvoyer. Dites-le à l'opérateur.
- La caméra et l'audio : foto_programa / foto_fuente ; qu'on voie et qu'on entende (sans faire
  sonner les haut-parleurs la nuit : mesurez avec audio_measure).
- Un invité de test : le lien de la salle (room_link) ouvert depuis un autre appareil ou le
  navigateur, et invitado_prueba (écho et réseau) avec quelqu'un à l'intérieur.
- La page web de l'événement s'ouvre (evento_web) et affiche ce qu'elle doit afficher.
Seulement ensuite : les invitations (invitacion par personne : texte, lien et QR), et expliquez
comment les envoyer (e-mail, WhatsApp) ou envoyez-les avec un notificateur s'il est configuré.

## 4. Répétition

Suggérez-la TOUJOURS : evento_ensayo_rapido (parcourt le conducteur sans attendre) et, mieux, une
vraie répétition avec evento_correr {accion: arrancar, ensayo: true}. Dites ce qui a échoué et
corrigez-le.

## 5. Livrez

À la fin, en un seul message :
- Le LIEN de la page web de l'événement (evento_web) pour qu'il l'ouvre.
- Les liens que chacun va utiliser : salle des invités, prompteur, surface de contrôle, public.
- Ce qui reste en suspens et ce que seule une personne peut confirmer (evento_chequeo_ver).
- Comment démarrer le jour de l'événement (carte Direction : « Démarrer le conducteur »).

## Images

Fonds et cartons : JPG de 1920×1080 au maximum et moins de 500 Ko (une image de 8 Mo met du temps
à se charger sur le web et sur le téléphone du public). Si celle que vous trouvez est lourde,
réduisez-la avant de l'utiliser (ffmpeg est inclus : `ffmpeg -i in.png -vf scale=1920:-1 -q:v 4 out.jpg`).
Logos sur fond transparent (PNG). Indiquez d'où vient chaque image et si on peut l'utiliser.

## Ce qui ne se fait pas sans demander

Passer à l'antenne, diffuser, envoyer des invitations, supprimer ou écraser des scènes existantes,
dépenser de l'argent ou créer des comptes. Un nouvel événement part DE ZÉRO : ne réutilisez ni ne
modifiez les scènes d'un autre événement, sauf si on vous le demande.
