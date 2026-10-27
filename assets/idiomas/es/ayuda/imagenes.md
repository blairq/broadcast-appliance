## Que capturar
| Quiero ver... | Herramienta |
|---|---|
| lo que sale al aire | `foto_programa` |
| una escena (este o no al aire) o una fuente | `foto_fuente {"name"}` |
| todas las escenas de un vistazo | `foto_todas_escenas` |
| a una persona de la conferencia | `foto_invitado {"participant": "Ana"}` (con `includeScreen` su pantalla) |
| a todos los de la conferencia | `foto_todos_invitados` |
| un video crudo (guest:N, share:N, lobby:K, backstage:K, operator) | `capture_feed {"key"}` (`feeds_list` los muestra) |
| la propia app | `capture_ui {"window": "main"|"all"}` |
| **movimiento / video** | `capture_sequence {"target": "program", "frames": 6, "intervalMs": 400}` |

## Consejos
- Tamaño: `maxWidth` (640 alcanza para reconocer caras y texto grande; 1280+ para leer texto chico). JPEG por defecto; `format: "png"` para graficos nitidos.
- `capture_sequence` con una persona (`participant`) sirve para ver si habla (boca), si esta presente, si levanto la mano, si la imagen esta congelada (cuadros identicos) o negra.
- Despues de cada cambio visual, captura para verificar. En vivo, la escena al aire cambia al instante.
- Las personas en la fila de oradores mas alla de los primeros no mandan video a la app: `foto_invitado` devuelve su foto reciente (lo dice en el texto).