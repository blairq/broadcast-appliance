**Ne répondez jamais « c'est impossible » pour un appareil inhabituel ou une demande que les outils ne couvrent pas : écrivez un plugin.** Un plugin est un programme Python qui tourne dans son propre processus (s'il plante, la diffusion continue et il redémarre tout seul) et qui ajoute au système ce qui est nécessaire. C'est vous qui l'écrivez ; l'opérateur ne programme pas.

**Où ils sont** : chaque plugin est un dossier dans `Datos/Plugins/<id>/` (`plugin_listar` donne le chemin dans `carpeta`) : `plugin.json` (manifeste), `plugin.py` (le code), `_libs/` (ses bibliothèques pip), `datos/` (ce qu'il enregistre) et son journal dans `Logs/plugins/<id>.log`. Ils vivent dans les données, pas dans le programme : ils survivent aux mises à jour. Les exemples sont fournis avec le programme (`plugin_listar` → `ejemplos`) ; dans l'interface, page **Plugins** (rail) → « Nouveau à partir d'un exemple ». N'écrivez pas de fichiers à la main dans ce dossier : utilisez `plugin_escribir` (il valide l'id et les chemins).

Cas typiques :
- Une caméra USB avec un PTZ qui « ne marche qu'avec l'application de la marque » (Logitech Rally, Jabra, Huddly, OBSBOT…) : elle se présente comme une webcam (UVC) et le PTZ passe par les contrôles UVC standard, par une **unité d'extension UVC** (XU) propre ou par HID.
- Caméras 360 / fisheye (Kandao, Insta360, Ricoh) : une ou deux images de 180° ; il faut les découper, les redresser ou les assembler, et parfois elles apportent des données (d'où parle quelqu'un).
- Plusieurs appareils qui travaillent ensemble (trois caméras de salle qui détectent qui parle) : un plugin par appareil publie des événements et un autre plugin « réalisateur » les regroupe et pilote le studio.
- N'importe quel appareil avec sa propre API (HTTP, WebSocket, UDP, série) : surfaces de contrôle, tally, compteurs, capteurs.
- Traiter de la vidéo ou de l'audio en Python (numpy, OpenCV, **GStreamer**) : une carte du graphe. Un effet, un filtre ou une analyse vidéo qui n'est pas dans `video_modulos` : voir « Vidéo » plus bas.
- Automatiser (« quand X se produit, faire Y ») ce que les déclencheurs (`alert_types`) ne couvrent pas : écouter les événements du studio et appeler des outils.
- Un graphique animé sur mesure que le répertoire n'a pas (avec de la logique, des données externes ou dessiné image par image) : `@plugin.grafico` (`help {"topic": "graficos"}`, exemple `ejemplo-grafico`).
- Une PIÈCE D'IDENTITÉ qui n'est pas le DNI argentin (un permis de conduire, le document d'un autre pays, un badge avec QR) : `@plugin.documento` (plus bas, exemple `documento-aamva`). La borne le propose dans ses champs `documento`. Une carte d'identité avec une zone MRZ au verso (OACI TD1) est déjà lue par défaut par `mrz-td1` ; un plugin reçoit aussi les lignes d'un verso (format « MRZ », trois lignes séparées par un saut de ligne, déjà validées) si ses `formatos` l'incluent ou sont vides.

## Le chemin
1. **Ce qui existe** : `dispositivos_listar` (caméras et micros avec leur `gst` prêt pour un pipeline, et USB/HID/série avec vid:pid). Si vous ne savez pas comment parler à l'appareil : cherchez le protocole (WebSearch si vous l'avez) et/ou écrivez un plugin d'exploration (plus bas).
2. **Partez d'un modèle** : `plugin_listar` (les `ejemplos` avec leur `categoria`) → `plugin_ver {"id": "<ejemplo>"}` et, s'il ressemble à ce qu'il faut faire, `plugin_desde_ejemplo {"ejemplo", "id"}` et changez ce qui est nécessaire. Les modèles : `ejemplo-video-gst` (effet, filtre ou analyse vidéo avec GStreamer), `ejemplo-automatizacion` (écouter le studio et agir), `ejemplo-equipo-red` (un appareil avec un protocole texte sur TCP, avec simulateur), `ejemplo-basico` (tout ce que peut faire un plugin : carte, filtre audio, entrée, panneau, contrôle). Pour les appareils : `ejemplo-ptz-uvc`, `ejemplo-360`, `ejemplo-director`, et ceux prêts à l'emploi (VISCA, TSL, Stream Deck, MIDI, OSC, Bosch DCN).
3. **Écrivez-le** : `plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`.
4. **Testez-le sans toucher à l'antenne** : `plugin_probar {"id"}` → rapport (ce qui est déclaré, cartes avec ms par image et ce qu'elles ont changé, entrées avec images par seconde, contrôles, événements, journal avec les erreurs Python). Si `ok` est false ou que quelque chose ne colle pas, corrigez et testez de nouveau. `controles: [{"nombre", "args"}]` appelle des contrôles ; `ptz: true` fait bouger l'appareil un instant.
5. **Activez-le** : `plugin_activar {"id"}`. Ses entrées apparaissent toutes seules (scène du manifeste ou « Plugins »), son PTZ devient celui de cette entrée (`ptz_*` et la surface de contrôle fonctionnent pareil), ses cartes se placent avec `plugin_tarjeta` (ou dans `video_pipeline` avec `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`) et se dosent avec `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"parametros": {"fuerza": 0.5}}}` (ou les paramètres seuls : `{"fuerza": 0.5}`). Une entrée qui n'est pas à l'antenne n'est dessinée que quand quelqu'un la regarde : pour vérifier, capturez deux fois (la première peut être une vieille image).
6. **Vérifiez en direct** : `video_estado {"entrada"}`, `imagen_preguntar`, `plugin_registro`, `plugin_control {"id"}` (infos sur ses médias), `plugin_eventos`.
7. **Expliquez à l'opérateur** ce qu'il fait et comment s'en servir. Si vous changez le code ensuite : `plugin_escribir` + `plugin_reiniciar`.

## Le SDK (`import ba_plugin as ba`)
```python
import ba_plugin as ba
plugin = ba.Plugin()                     # plugin.config (dict de plugin.json), plugin.datos (carpeta propia)

# ENTRADAS (video y/o audio). a) un pipeline de GStreamer que corre la app (lo más barato, sin copias):
plugin.entrada("Rally", gst="avfvideosrc device-index=1", escena="Sala")          # +gst_audio="osxaudiosrc ..."
# b) cuadros hechos en Python (numpy RGBA (alto, ancho, 4) uint8; audio float32 (n, canales)):
e = plugin.entrada("Pelota", video=True, audio=True)
e.enviar_video(arr, fps=30); e.enviar_audio(muestras, tasa=48000, canales=2)    # en un hilo de @plugin.al_iniciar

# TARJETAS: se cambia el cuadro EN EL LUGAR (memoria compartida); rápido (numpy vectorizado, nada de bucles por píxel)
@plugin.tarjeta("negativo", descripcion="...", parametros={"fuerza": 1.0})
def negativo(cuadro, params):            # cuadro.array() -> (alto, ancho, 4) RGBA del tamaño del lienzo
    a = cuadro.array(); a[..., :3] = 255 - a[..., :3]
@plugin.tarjeta("ganancia", medio="audio", parametros={"db": -6})
def ganancia(bloque, params): bloque.array()[:] *= 10 ** (params["db"] / 20)   # (muestras, canales) float32

# PTZ de una entrada (de este plugin o una que ya exista): velocidades -1..1
@plugin.ptz("Rally")
class Ptz:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 8}
    def mover(self, pan, tilt, zoom): ...
    def parar(self): ...
    def home(self): ...
    def ir_a_preset(self, n): ...; def guardar_preset(self, n): ...   # también enfocar(v), auto_foco(b)

@plugin.control("modo", descripcion="...", parametros={"modo": "auto|manual"})   # plugin_control
def modo(args): return {"ok": True}

# GRÁFICOS ANIMADOS (help {"topic": "graficos"}): aparecen en Gráficos y Rótulos como plugin-<id>-<nombre>
@plugin.grafico("marcador", ancho=1000, alto=420, parametros=[{"clave": "puntos", "tipo": "datos", "porDefecto": "Sí: 3"}])
def marcador(valores): return {"declarativo": {...}}          # o {"qml": "..."}; o def f(lienzo, ms, valores) con numpy

# PANEL: la interfaz del operador (la dibuja la app; plugin_panel la maneja igual). Los valores se guardan solos.
plugin.panel([
    ba.seccion("Seguimiento"),
    ba.interruptor("seguir", "Seguir al que habla", defecto=True),
    ba.deslizador("suavidad", "Suavidad", 0, 1, defecto=0.5),
    ba.opcion("modo", "Modo", ["auto", "manual"]),
    ba.boton("calibrar", "Calibrar", al_tocar=calibrar),        # también texto(), numero()
    ba.indicador("caras", "Caras vistas"), ba.medidor("confianza", "Confianza", 0, 1),
])
plugin.valores["suavidad"]                                      # lo que puso el operador
@plugin.al_cambiar("modo")
def al_modo(valor): ...
plugin.mostrar("caras", 3)                                      # indicadores en vivo (se puede llamar en cada cuadro)

plugin.evento("hablando", {"angulo": 37, "persona": 2})        # canal de datos (con hora)
@plugin.escuchar("*/hablando")                                  # "kandao-1/hablando", "*/x", "id/*" (los propios también llegan)
def al_hablar(datos, de): ...

plugin.llamar("scene_switch", {"name": "Exterior"})             # CUALQUIER herramienta del sistema (las tuyas)

# DOCUMENTOS DE IDENTIDAD (docs/funciones/DOCUMENTOS.md): la app lee el código de barras de la cámara (zxing) y,
# si no es un tipo de fábrica (dni-ar), te pasa {formato, texto, bytes}. Devolvés los datos normalizados o None
# (no es tuyo). Queda como el tipo «plugin:<id>/licencia» (documento_tipos; se elige en el campo del tótem).
@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU.)", formatos=["PDF417"], pais="US",
                  como_se_dice="tu licencia", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636000..."}])
def licencia(codigo):                    # codigo["texto"] (str), codigo["bytes"] (bytes), codigo["formato"]
    if "ANSI " not in codigo["texto"]: return None
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15",   # fechas en ISO
            "vencimiento": "...", "sexo": "F", "extra": {"estado": "CA"}}   # sin `numero` = no lo leyó
# plugin_probar corre los `ejemplos` (SINTÉTICOS: nunca datos de una persona real) y muestra qué devolvió.
# Los datos son sensibles: no los mandes a ningún servicio de afuera ni los imprimas enteros en el registro.
plugin.aviso("texto", "warning"); plugin.estado("conectado a la Rally"); plugin.secreto("clave")  # claves cifradas
@plugin.al_iniciar / @plugin.al_terminar / @plugin.cada(segundos) / @plugin.al_cambiar_config
plugin.correr()                                                 # al final
```
Appareils (`from ba_plugin import dispositivos as d` ; dépendances `pyusb`, `libusb-package`, `hidapi`, `pyserial`) :
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]` (vid/pid en hexadécimal, texte) ; `d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]` ; `d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`.
- `uvc = d.UVC.abrir("046d", "0881")` : `uvc.info()` (terminal caméra, unités d'extension avec leur GUID, plages), `uvc.mover(pan, tilt, zoom)` / `uvc.parar()` (UVC standard), `uvc.ir_a(pan=, tilt=, zoom=)`, et ce qui est propre à la marque : `uvc.unidades()`, `uvc.xu_explorar(unidad)` (quels sélecteurs répondent), `uvc.xu_leer(unidad, selector)`, `uvc.xu_escribir(unidad, selector, bytes)`. Cela ne retire pas l'image au système : elle bouge pendant qu'on la voit.
- `h = d.hid_abrir(vid, pid)` : `h.write(bytes)`, `h.read(64, 100)`.

## Vidéo : un effet, un filtre ou une analyse que le graphe n'a pas
D'abord `video_modulos {"buscar": ...}` : si GStreamer l'a déjà, c'est une carte `gst` et c'est réglé. Sinon (un effet propre, une correction inhabituelle, compter des personnes, détecter du mouvement, lire un tableau d'affichage), **programmez-le vous-même comme une carte de plugin** : c'est la carte « plugin » du graphe vidéo (le module `baplugin`), elle tourne dans la chaîne de l'entrée comme n'importe quelle autre, et le code tourne dans le processus du plugin (s'il se bloque, l'image passe quand même et la diffusion continue).

Dans la carte, vous pouvez utiliser numpy et **tout GStreamer depuis Python** (`from ba_plugin import gst as bagst` ; le programme fournit les bindings) : chaînes de modules et **éléments GStreamer propres écrits en Python**. Minimum :
```python
import ba_plugin as ba
from ba_plugin import gst as bagst
plugin = ba.Plugin()

@bagst.elemento("baumbral", propiedades={"nivel": 128})     # un GstBase.BaseTransform de verdad
def umbral(a, props):                                          # a: (alto, ancho, 4) RGBA, sin copiar
    gris = a[..., :3].mean(axis=2) > props["nivel"]
    a[..., :3] = (gris * 255).astype("uint8")[..., None]

@plugin.tarjeta("umbral", parametros={"nivel": 128})
def tarjeta(cuadro, params):
    c = bagst.cadena("baumbral name=u ! videobalance contrast=1.2")   # una por tarjeta puesta; se arma una vez
    c.procesar(cuadro)                                                 # el resultado queda en el cuadro
    c.poner("u", nivel=int(params["nivel"]))                           # cambiar en vivo sin rearmar

plugin.correr()
```
Sans GStreamer, cela marche aussi : `@plugin.tarjeta` et numpy sur `cuadro.array()`. Une analyse NE change PAS l'image : elle mesure sur `cuadro.array()[::8, ::8]` toutes les quelques images, `plugin.mostrar` dans son panneau et `plugin.evento("movimiento", {...})` (avec hystérésis ; jamais un événement par image).

Le chemin complet : `plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}` (ou `plugin_escribir`) → `plugin_probar {"id"}` (ms par image de chaque carte, en 320x180 : en 1080p c'est ~30 fois plus ; cela doit rester bien en dessous d'une image) → `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (elle apparaît dans le graphe sous « Plugin · <tarjeta> » ; dans la page Vidéo, on la voit, on la déplace et on la dose comme les autres) → `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"nivel": 90}}` → `foto_fuente` pour voir. Coûts en 1080p : passer par une chaîne ~1 ms, `videobalance` ~8, `gaussianblur` ~35 (trop : réduisez ou choisissez autre chose), numpy en uint16 et contigu 1-3 ms (en float ou avec `a[..., :3]` sur l'image entière, 20).

## Recettes
- **PTZ inhabituel** : entrée `gst` avec la caméra (`dispositivos_listar` donne le `avfvideosrc device-index=N`) + `@plugin.ptz` qui parle UVC/XU/HID. Essayez d'abord l'UVC standard (`uvc.mover`) ; si elle ne bouge pas, explorez les XU (un contrôle « explorer » qui renvoie `uvc.info()` et `xu_explorar`) et cherchez le protocole de ce modèle.
- **Découper une image en plusieurs entrées** (deux fisheyes dans une image, 2x2 d'un enregistreur) : une seule entrée et, dans son graphe, des cartes `recorte` + `salida` (chaque sortie est une source avec un nom). N'ouvrez pas deux fois le même périphérique.
- **Redresser un fisheye / assembler du 360** : carte de plugin avec OpenCV (`opencv-python-headless`) : calculez les cartes UNE fois (`cv2.fisheye.initUndistortRectifyMap` ou `cv2.remap` avec vos propres cartes) et appliquez `cv2.remap` à chaque image. Si cela devient lourd, faites arriver l'entrée en plus petit.
- **Plusieurs appareils identiques** : un plugin par appareil (copiez avec `plugin_desde_ejemplo` ou `plugin_escribir`) avec sa `config` (numéro de série, IP) et sa `escena` ; ou un seul qui déclare N entrées.
- **Qui parle / réalisateur** : chaque plugin d'appareil publie `plugin.evento("hablando", {...})` (attention : si un réalisateur qui écoute `*/hablando` tourne, il le reçoit aussi ; en test, utilisez un autre canal ou son `config.de`) ; un plugin réalisateur `@plugin.escuchar("*/hablando")` décide (temps minimum sur chaque plan, ne pas couper au milieu d'une phrase) et utilise `plugin.llamar(...)` : `scene_switch`, le compositeur avec `video_tarjeta_ajustar` (calques), `ptz_*` pour pointer une autre caméra. Demandez à l'opérateur s'il le veut automatique ou avec confirmation (`operator_ask`).
- **Audio d'un appareil** : entrée avec `gst_audio` ou `enviar_audio` ; cartes audio sur n'importe quelle source avec `plugin_tarjeta {"fuente"}`.
- **Automatisation** (modèle `ejemplo-automatizacion`) : `@plugin.escuchar("aire/escena")` (les sujets : `eventos_temas`) et `plugin.llamar(...)` ; mettez `"herramientas": [...]` dans le plugin.json avec la seule chose qu'il peut appeler. Un contrôle « simuler » qui parcourt les cas permet de le tester avec `plugin_probar {"controles": [...]}` (les appels au système apparaissent dans le rapport sans être exécutés).
- **Un appareil sur le réseau** (modèle `ejemplo-equipo-red`) : connexion dans un thread d'`al_iniciar` avec timeout et reconnexion avec attente croissante, ordres avec réponse, ce qu'il envoie de lui-même sous forme de `plugin.evento`, et un simulateur de l'appareil intégré (`modo: simulador`) pour le tester sans l'équipement.

## Règles
- Rien ne bloque indéfiniment : tout avec timeout ; les boucles dans des threads d'`al_iniciar` et en surveillant `plugin.andando`.
- Les cartes doivent être rapides (une image dure 1000/fps ms du canevas : 33 ms à 30 fps, 16 ms à 60 ; et c'est en série avec la vidéo) : numpy vectorisé ; si c'est trop lent, c'est l'image précédente qui sort. Si le plugin doit seulement MESURER (couleur, luminosité, qui est là), qu'il mesure toutes les quelques images sur une version réduite (`a[::8, ::8]`) et applique ce qu'il calcule avec une carte gst (`plugin.llamar("video_tarjeta_ajustar", ...)` sur un `videobalance`) : traiter l'image entière en Python sur chaque caméra charge la machine et bloque l'interface.
- `print` va dans le journal (`plugin_registro`) ; utilisez `plugin.estado(...)` pour ce que l'opérateur doit voir.
- **Tout plugin que l'opérateur doit piloter a un `plugin.panel`** (ce qui se règle : curseurs, interrupteurs, options ; ce qui se fait : boutons ; ce qui se passe : indicateurs et vumètres). Les humains utilisent le système comme vous : pas de « demandez-le-moi par le chat ». `@plugin.control` est réservé à ce que demande l'assistant avec des arguments (et il apparaît aussi comme un bouton).
- Les bibliothèques vont dans `dependencias` (pip, elles sont téléchargées dans le dossier du plugin : portables, elles survivent aux mises à jour). **numpy est déjà fourni avec le programme** (le déclarer ne coûte rien) ; la première fois qu'on télécharge quelque chose de gros (opencv), cela peut prendre quelques minutes.
- Clés d'appareils (identifiant, mot de passe, jeton de l'API de la marque) : jamais par le chat ni dans `config`. Déclarez-les dans plugin.json (`"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]`), demandez-les avec `plugin_credenciales {"id"}` (formulaire sécurisé dans la barre) et, dans le plugin, `plugin.secreto("clave")` (None si elles ne sont pas encore là : qu'il attende et le dise avec `plugin.estado`).
