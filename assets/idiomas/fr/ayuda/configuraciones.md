Chaque configuration est un dossier avec TOUT ce qui lui est propre : scènes et réglages (Config), médias, graphiques, plugins, photos des membres, vignettes, événements et enregistrements. **Rien n'est partagé** : toucher à un plugin ou à une scène dans l'une ne casse pas une autre. De la machine (communs à toutes), restent les modèles, les identifiants, les journaux et le cache.

- `configuraciones_listar` : celles qui existent, l'active, leur description, leur taille et leur origine ; et les démos qu'embarque le programme.
- `configuracion_crear {"nombre", "desde", "descripcion"}` en crée une (sans l'activer). `desde` : `vacia` (par défaut), `actual` (copie de celle en cours d'utilisation), le nom d'une autre, `demo:<nombre>` (une démo du programme) ou `evento:<carpeta>/<inicio|cierre|ultima>` (la configuration avec laquelle un événement a été fait : son instantané des réglages et les médias de l'actuelle). Les enregistrements ne sont pas copiés sauf avec `con_grabaciones`.
- `configuracion_cambiar {"nombre"}` passe à une autre : **l'application redémarre** avec ce dossier (elle ne copie rien). Cela coupe ce qui tourne (diffusion, enregistrement, conférence) : ne le faites jamais en direct sans que l'opérateur le demande.
- `configuracion_editar {"nombre", "renombrar_a"|"descripcion"|"borrar": true}` : renommer, décrire ou envoyer à la corbeille (`Configuraciones/.papelera`). L'active ne se renomme pas et ne se supprime pas.

**La démo** : `demo:Demo parlamentaria` est un système d'assemblée générique prêt à tester : le Conseil de la Ville de Puerto Ejemplo (15 membres et une présidence aux noms inventés), hémicycle, caméras, unité centrale de conférence simulée, scènes d'assemblée et de débat, surface de contrôle et ordre du jour.

**Portable** : tous les chemins sont enregistrés relativement à la configuration (`Media/logo.png`, aussi dans un pipeline : `location=Media/x.png`). Le dossier de données entier peut être copié tel quel sur une autre machine. Quand on crée une source avec un chemin relatif, il est résolu par rapport à la configuration en cours.

Dans l'interface : Paramètres › Système › Configurations (utiliser, dupliquer, renommer, supprimer, créer à partir d'une démo).
