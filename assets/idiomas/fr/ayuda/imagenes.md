## Quoi capturer
| Je veux voir... | Outil |
|---|---|
| ce qui passe à l'antenne | `foto_programa` |
| une scène (à l'antenne ou non) ou une source | `foto_fuente {"name"}` |
| toutes les scènes d'un coup d'œil | `foto_todas_escenas` |
| une personne de la conférence | `foto_invitado {"participant": "Ana"}` (avec `includeScreen`, son écran) |
| toutes les personnes de la conférence | `foto_todos_invitados` |
| une vidéo brute (guest:N, share:N, lobby:K, backstage:K, operator) | `capture_feed {"key"}` (`feeds_list` les affiche) |
| l'application elle-même | `capture_ui {"window": "main"|"all"}` |
| **mouvement / vidéo** | `capture_sequence {"target": "program", "frames": 6, "intervalMs": 400}` |

## Conseils
- Taille : `maxWidth` (640 suffit pour reconnaître des visages et du gros texte ; 1280+ pour lire du petit texte). JPEG par défaut ; `format: "png"` pour des graphiques nets.
- `capture_sequence` avec une personne (`participant`) sert à voir si elle parle (bouche), si elle est présente, si elle a levé la main, si l'image est figée (images identiques) ou noire.
- Après chaque changement visuel, capturez pour vérifier. En direct, la scène à l'antenne change instantanément.
- Les personnes de la file des intervenants au-delà des premières n'envoient pas de vidéo à l'application : `foto_invitado` renvoie leur photo récente (le texte l'indique).
