# Écrans dans la salle et signalétique

## À quoi ça sert
Utiliser le système pour ce qu'on voit sur place : téléviseurs avec le programme, l'intervenant
en cours, le vote ou un QR pour poser des questions ; affiches imprimées avec QR.

## Questions propres
- Combien de téléviseurs et où ? Sont-ce des Smart TV sur le réseau ou passent-ils par HDMI ?
- Que doit afficher chacun et quand cela change-t-il ?
- Imprime-t-on des QR (affiches, badges) ? Combien de temps à l'avance ?

## Quoi préparer
- Écrans (pantalla_buscar, pantalla_agregar, pantalla_mostrar) avec leur contenu pour chaque
  moment (programme, horloge, vote, message, QR).
- Scènes propres pour les écrans (sortie propre sans synthés : salida_limpia).
- Codes QR (qr_generar / qr_guardar) avec un style, exportés pour l'impression (qr_exportar).

## Vérifier
AVANT D'IMPRIMER : adresse fixe (domaine propre ou sous-domaine réservé :
acceso_remoto_probar_direccion et acceso_remoto_dns_verificar). Si l'adresse change, tous les QR
imprimés deviennent inutiles et il faut réimprimer. Testez chaque QR imprimé avec un téléphone.

## Livrer
Les QR à imprimer (en haute résolution), la liste de ce qu'affiche chaque écran.
