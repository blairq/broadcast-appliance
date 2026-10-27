## What to capture
| I want to see... | Tool |
|---|---|
| what goes on air | `foto_programa` |
| a scene (on air or not) or a source | `foto_fuente {"name"}` |
| all the scenes at a glance | `foto_todas_escenas` |
| a person in the conference | `foto_invitado {"participant": "Ana"}` (with `includeScreen` their screen) |
| everyone in the conference | `foto_todos_invitados` |
| a raw video (guest:N, share:N, lobby:K, backstage:K, operator) | `capture_feed {"key"}` (`feeds_list` shows them) |
| the app itself | `capture_ui {"window": "main"|"all"}` |
| **motion / video** | `capture_sequence {"target": "program", "frames": 6, "intervalMs": 400}` |

## Tips
- Size: `maxWidth` (640 is enough to recognize faces and large text; 1280+ to read small text). JPEG by default; `format: "png"` for sharp graphics.
- `capture_sequence` with a person (`participant`) is useful to see whether they are talking (mouth), whether they are present, whether they raised their hand, whether the picture is frozen (identical frames) or black.
- After every visual change, capture to verify. Live, the on-air scene changes instantly.
- People in the speakers queue beyond the first few don't send video to the app: `foto_invitado` returns their recent photo (it says so in the text).