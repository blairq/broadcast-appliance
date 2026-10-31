# Diffusion de jeux (streamer)

## À quoi ça sert
Un streamer joue en direct : la capture du jeu, sa caméra dans un coin, le chat du public à
l'écran et des alertes. L'audio du jeu et le micro bien équilibrés.

## Questions propres
- Quelle plateforme (Twitch, YouTube, Facebook) ? Le compte est-il connecté ?
- Quel jeu et comment est-il capturé (fenêtre, écran, console via un boîtier d'acquisition) ?
- Caméra du streamer ? Avec le fond détouré (incrustation chroma) ?
- Chat à l'écran ? Mettre des messages en avant ? Alertes d'abonnés ?
- Musique de fond (attention aux droits) ?

## Quoi préparer
- Scènes : « Jeu » (capture + petite caméra + chat), « Discussion » (grande caméra), « Pause »,
  « Début ».
- Chat à l'écran (chat_overlay_add) avec celui de la plateforme (twitch_chat / youtube_chat).
- Audio : jeu et micro avec leur niveau ; le micro avec compresseur et gate.
- Diffusion (twitch_live_start / youtube_live_create) avec titre et catégorie.

## Vérifier
Test de diffusion ; que le chat apparaisse ; niveaux (le jeu ne couvre pas la voix).

## Livrer
Le lien de la chaîne et la carte de chat pour mettre des messages en avant.
