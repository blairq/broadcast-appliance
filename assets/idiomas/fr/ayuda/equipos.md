Un catalogue unique d'appareils, avec des actions déclarées par type. C'est le chemin général pour « qu'est-ce qui est branché ? » et « fais X à tel appareil ».

## Le chemin
1. `equipos_escanear {"detector"?}` (red, local, ndi, usb ; sans détecteur, tous) : chaque appareil avec un `id` stable (« red:192.168.1.90 », « local:video:<id de la camara> », « local:audio:<id del microfono> », « local:salida:<id> », « usb:046d:0881:… »), `tipo`, `nombre`, s'il est `conectado` et ses `acciones`. `equipos_listar` donne ce qui a déjà été trouvé, sans chercher.
2. `equipo_ver {"id"}` : tout ce qu'on sait et chaque action avec ses paramètres.
3. `equipo_accion {"id", "accion", "args"}`.

## Actions utiles
- Caméra réseau : `pedir_clave` (formulaire sécurisé : ne demandez jamais le mot de passe par le chat), `ficha` (ONVIF + analyse + plan), `funciones_onvif` (quels axes bougent et comment, limites, mise au point, home, presets, auxiliaires, et sa position), `onvif` (une opération brute : `servicio` ptz|media|imaging|device et `cuerpo` avec le XML du Body ; {perfil}, {config_ptz} et {fuente} sont remplacés), `http` (l'API propre à la marque : Axis VAPIX `/axis-cgi/com/ptz.cgi?query=limits`, Hikvision ISAPI…), `agregar`, `activar_ptz`.
- Caméra de cette machine : `agregar` (comme entrée vidéo, avec son nom et son format préféré). Micro : `agregar` (entrée audio). NDI : `agregar`.

## Noms des périphériques (de la machine)
Les caméras, cartes d'acquisition, micros, interfaces audio, émetteurs NDI et caméras réseau portent le nom que leur donne l'opérateur dans « Périphériques et appareils » : `equipo_renombrar {"id", "nombre"}` (vide : revient à celui du système). Il appartient à la MACHINE, pas à l'événement, et il est attaché à l'id stable de l'appareil. Les entrées vidéo et audio, la console de mixage et les sorties l'affichent ainsi, et une nouvelle entrée prend ce nom. `equipo_configurar {"id", "config": {"nota", "modo"}}` (`modo` : le format préféré d'une caméra). Ceux que la machine a déjà vus et qui ne sont pas là aujourd'hui restent dans la liste comme absents (`estado` « ausente ») : une entrée qui les utilise les attend et revient toute seule ; `equipo_olvidar` supprime un appareil absent.
- USB avec caméra UVC : `explorar_uvc` (PTZ standard et unités d'extension de la marque : la première étape pour un plugin).

## Pilotes et quarantaine
Chaque pilote ou fournisseur de périphériques de la machine (ceux de GStreamer : WASAPI, Media Foundation, Core Audio, AVFoundation, Pulse, V4L2, DeckLink, plugins ; et chaque pilote ASIO sous Windows) est testé dans un processus séparé avec une limite de temps : s'il se bloque ou plante, l'application continue. Au bout de 4 échecs consécutifs (réglage `drivers.fallasCuarentena`) il est MIS EN QUARANTAINE : il n'est plus rechargé tout seul, ni au démarrage ni quand les périphériques changent. `drivers_listar` donne l'état de chacun avec le `motivo` (par ex. JackRouter : « no contesta: ¿está corriendo el servidor de JACK? »). `driver_probar {"id"}` le teste une fois de plus (après avoir corrigé la cause) ; `drivers_revisar` reteste tout. C'est PAR MACHINE : sur une nouvelle machine, tout est testé au démarrage ; sur une machine connue, seulement ce qui est nouveau (ou ce qui a changé de version).

## Diagnostiquer un PTZ qui ne répond pas
`funciones_onvif` dit ce que déclare la caméra ; `onvif` avec `<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>` dit où elle est ; tester un mouvement (`RelativeMove`, `AbsoluteMove`) et regarder de nouveau. Si le protocole accepte et que la position ne change pas, tester l'API de la marque avec `http` ; si cela ne marche pas non plus, c'est la caméra (mécanique ou une limite dans sa configuration).
