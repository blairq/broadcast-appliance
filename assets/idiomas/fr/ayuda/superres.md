**C'est une CARTE du graphe vidéo** (un filtre GStreamer qui agrandit x2), sur n'importe quelle entrée : `superres_tarjeta {"entrada", "motor"}`. Ce qui la suit travaille au double et la scène la reçoit en grand : elle sort au double de ce qui entre (une SD de 720x576 sort en 1440x1152). Sur une entrée réseau, on peut aussi la mettre sur la source elle-même (`superres_aplicar`), avant le graphe. L'audio n'y passe pas ; la synchronisation se maintient tant que le temps de traitement tient dans le tampon de l'entrée (`latencia_ms`) : si `superres.ms_por_cuadro` (video_estado) s'approche du tampon, augmentez-le (video_entrada_red `latencia_ms`).

## Moteurs (`superres_estado` dit lesquels sont disponibles)
| Moteur | Ce que c'est | Coût | Quand |
|---|---|---|---|
| `apple` | La super-résolution d'Apple (VideoToolbox, macOS 26), entrée jusqu'au maximum du système (aujourd'hui 960x960) | ~3 ms | Sur un Mac : la première option |
| `fsr` | FSR 1 d'AMD (EASU + RCAS), sans modèle, en OpenGL | <1 ms sur GPU | N'importe quel GPU (Windows, Linux) ou comme base |
| `onnx` + petit modèle | Un modèle de luminance entraîné avec LA caméra (`superres_entrenar`) | ~14 ms avec CoreML ; ~65 ms CPU seul | Quand la caméra a sa propre compression ou son propre bruit |
| `onnx` + SPAN | SPAN x2 (Apache), fidèle | ~140 ms sur un Mac | Tests / GPU puissant |
| `onnx` + Real-ESRGAN | Compact x4 (BSD), nettoie beaucoup | ~290 ms sur un Mac | Pas tenable en direct ici ; pour comparer |

## Recettes
- Une vieille caméra sur un Mac : `superres_tarjeta {"entrada": "Cámara 1", "motor": "apple"}`.
- Tester FSR : `superres_tarjeta {"entrada", "motor": "fsr"}`.
- Un modèle sur une caméra de taille inhabituelle (CoreML fait un agrandissement ordinaire et le signale dans `error`) : `superres_variante_crear {"plugin", "faltantes": true}` compile le modèle pour la taille de chaque caméra qui n'a pas le sien (sans PyTorch ; superres_estado montre l'avancement) ; `superres_variantes {"plugin"}` les liste avec leur fiche (usage, tailles, ms mesurées).
- Entraîner d'un clic : `superres_entrenar {"entrada": "Cámara 1", "minutos": 5}` (enregistre ~1 minute de la caméra sans toucher à l'antenne, entraîne et applique ; indique le gain en dB par rapport à l'agrandissement ordinaire). La première fois, il télécharge le laboratoire (PyTorch, ~800 Mo, une seule fois). Réentraîner repart du modèle précédent.
- SPAN ou Real-ESRGAN : `superres_modelo {"cual": "span"}` puis `superres_tarjeta {"entrada", "motor": "onnx", "modelo": "span-x2.onnx"}`.
- La retirer : `superres_aplicar {"entrada", "motor": ""}`.
- Après l'application, au bout de quelques secondes, `video_estado` : la nouvelle `tamaño` et `superres.ms_por_cuadro`. Si cela dépasse ~3/4 de la durée d'une image du canevas (~25 ms à 30 fps, ~12 ms à 60), l'entrée perd des images : utilisez un moteur plus léger.
