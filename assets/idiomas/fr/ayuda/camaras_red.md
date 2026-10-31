Pour « quelles caméras y a-t-il ? », « ajoute la caméra de la cour », « connecte le DVR », « j'ai une Axis/Hikvision/EZVIZ… ».

## Le chemin (toujours celui-ci)
1. `red_escanear` (une dizaine de secondes) : chaque appareil avec son IP, sa MAC et son fabricant, la marque (et pourquoi elle a été reconnue), ses ports, les URL RTSP typiques testées (`se ve` / `pide usuario y clave` / `no existe`), les `notas` de la marque et `siguiente_paso`. **Suivez le `siguiente_paso`.** Dites à l'opérateur ce que vous avez trouvé en quelques lignes (combien de caméras, de quelle marque, lesquelles marchent déjà et lesquelles demandent un mot de passe), ne lui envoyez pas le JSON.
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
2. Si elle demande un mot de passe : `camara_credenciales {"ips": [...], "pedir": true}` (UN formulaire pour toutes celles qui le demandent ; s'il y en a déjà un ouvert, il les y ajoute. Rend la main tout de suite : quand l'opérateur le remplit, vous recevez « [OPERADOR] guardo… » dans le chat ; en attendant, continuez avec le reste). C'est un formulaire sécurisé dans la barre : **ne lui demandez jamais le mot de passe par le chat**. Avant, dites-lui quel identifiant utiliser (`usuario_de_fabrica`) et d'où vient le mot de passe (les `notas` : EZVIZ = le code de vérification de l'étiquette, et le RTSP doit être activé dans son application ; Hikvision = celui de l'enregistreur ; Axis = celui d'un utilisateur ONVIF).
3. `camara_ficha {"ip"}` : ONVIF complet (profils avec leur URL exacte, PTZ, image) + l'analyse GStreamer du flux + `plan`. Lisez le plan à l'opérateur en deux lignes.
4. `camara_agregar {"ip", "escena", "nombre"}` avec `plan.argumentos` (si elle est déjà connectée, `reusar: true` l'ajoute à une autre scène sans ouvrir un autre flux ; si le mot de passe ONVIF arrive plus tard, `solo_ptz: true` + `entrada` lui configure le PTZ) : l'entrée est synchronisée (tampon et horloge du studio), normalisée seulement si nécessaire, et avec son PTZ ONVIF si elle en a un. Demandez dans quelle scène elle va si on ne vous l'a pas dit (ne la mettez pas dans celle qui est à l'antenne sans demander). Il attend la connexion et renvoie `estado.diagnostico` : appliquez ce qu'il propose (par ex. `video_entrada_red {"nombre", "ntp_sync": false}` si l'horloge de la caméra est décalée ; `video_entrada_red` sur une entrée existante la met à jour).
5. `video_estado {"entrada"}` quelques secondes après : résolution réelle et diagnostic. Si quelque chose ne va pas, le diagnostic donne la correction.

## À savoir
- Un **enregistreur** (DVR/NVR) a une caméra par canal : `/Streaming/Channels/101` est le canal 1 (principal), `102` le secondaire, `201` le canal 2… Chaque canal s'ajoute à part (`camara_agregar` avec l'`url` du canal).
- Si la marque n'a été reconnue que par la MAC, la confiance est faible : si aucune URL ne marche, demandez le modèle ou l'URL à l'opérateur.
- EZVIZ utilise des MAC de Hikvision : c'est le nom ONVIF (CS-…) qui compte.
- `camara_urls {"ip"}` reteste les URL (après avoir enregistré le mot de passe ou activé le RTSP).
- Les appareils sans RTSP ni ONVIF (routeurs, PC) ne sont pas des caméras : ne les proposez pas.
- Une caméra réseau peut aussi s'ajouter à la main avec `video_entrada_red` si on vous donne l'URL.
