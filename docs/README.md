# Documentación

| Carpeta | Qué hay |
|---|---|
| [`es/manual/`](es/manual/README.md) · [`en/manual/`](en/manual/README.md) | Para quien **usa** el aparato: manual del operador y guía del invitado, en español y en inglés. |
| [`es/`](es/) · [`en/`](en/) | Para quien lo compila: compilar, arquitectura y configuración, en español y en inglés. |
| [`funciones/`](funciones/) | Cómo funciona cada parte, una por archivo: video, audio, plugins, MCP, sesión remota, eventos, PTZ, superresolución… |
| [`desarrollo/`](desarrollo/) | Para quien lo compila: macOS, compilación, tests, el mapa GPU/CPU del motor, la migración a GStreamer. |
| [`planes/`](planes/) | Planes en curso, ideas, decisiones pendientes y el informe de experto con sus tareas. |
| [`modelos/`](modelos/README.md) | El modelo local: el índice (qué leer según para qué), cómo se entrena hoy, el corpus, la voz y las tarjetas de los modelos publicados (`modelos/publicados/`). |
| [`historia/`](historia/README.md) | Lo que fue cierto en su momento y ya no: por qué el sistema quedó como quedó. |
| [`bosch/`](bosch/README.md) | La central de conferencias Bosch DCN (Open Interface, DDI). |
| [`lsp-matrix-mixer/`](lsp-matrix-mixer/PR.md) | Parches propios a LSP Plugins. |
| [`prompts/`](prompts/) | Prompts de referencia. |
| [`modules/`](modules/) | Páginas Sphinx de los módulos de `src/`. |

En la raíz quedan la entrada de Sphinx (`index.rst`, `conf.py`,
`requirements.txt`; `make docs`) y `mcp.json`, el inventario de herramientas MCP
que genera `make mcp-inventario`.
