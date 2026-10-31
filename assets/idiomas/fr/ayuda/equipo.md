- `system_hardware` : tout d'un coup (écrans, interfaces audio d'entrée et de sortie, caméras/cartes d'acquisition, GPU, encodeurs matériels, CPU, mémoire, disque).
- `monitors` : dans l'ordre physique, avec un `id` stable et `orden` (le numéro de Sorties -> Écrans), résolution en pixels, position, échelle, Hz et principal. `output_window.screen` et `monitor_asignar` acceptent l'id ou l'ordre.
- `audio_devices` : entrées et sorties audio du registre de la machine : `name` est le nom que leur a donné l'opérateur (`tecnico`, celui du système), `conectado` s'ils sont présents. La `value` d'une entrée est le `device` de `audio_input_add`. Pour les nommer : `equipo_renombrar`.
- `video_devices` : caméras et cartes d'acquisition, du même registre (nom de l'opérateur et nom technique) ; avec `{"device": value, id o nombre}`, leurs formats, résolutions et images par seconde.

## Recette : occuper tous les écrans
1. `monitors` et `dashboard_list` (fenêtres ouvertes).
2. Le programme sur le plus grand écran qui N'est PAS le principal (le principal reste pour l'application) : `monitor_asignar {"monitor": "<orden>", "cosas": ["programa"]}` (il reste fixe et revient tout seul au redémarrage).
3. S'il y en a un troisième, multiview des scènes ou des invités (`output_window {"open": ["guest:1","guest:2"], "layout": "grid", "screen": ...}`).
4. Dire ce que ça donne.
