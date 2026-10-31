## O que capturar
| Quero ver... | Ferramenta |
|---|---|
| o que vai ao ar | `foto_programa` |
| uma cena (no ar ou não) ou uma fonte | `foto_fuente {"name"}` |
| todas as cenas de uma vez | `foto_todas_escenas` |
| uma pessoa da conferência | `foto_invitado {"participant": "Ana"}` (com `includeScreen`, a tela dela) |
| todos os da conferência | `foto_todos_invitados` |
| um vídeo bruto (guest:N, share:N, lobby:K, backstage:K, operator) | `capture_feed {"key"}` (`feeds_list` os mostra) |
| o próprio app | `capture_ui {"window": "main"|"all"}` |
| **movimento / vídeo** | `capture_sequence {"target": "program", "frames": 6, "intervalMs": 400}` |

## Dicas
- Tamanho: `maxWidth` (640 basta para reconhecer rostos e texto grande; 1280+ para ler texto pequeno). JPEG por padrão; `format: "png"` para gráficos nítidos.
- `capture_sequence` com uma pessoa (`participant`) serve para ver se ela fala (boca), se está presente, se levantou a mão, se a imagem está congelada (quadros idênticos) ou preta.
- Depois de cada mudança visual, capture para verificar. Ao vivo, a cena no ar muda na hora.
- As pessoas na fila de oradores além das primeiras não enviam vídeo ao app: `foto_invitado` devolve a foto recente delas (diz isso no texto).