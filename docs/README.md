# Documentación

| Carpeta | Qué hay |
|---|---|
| [`manual/`](manual/README.md) | Para quien **usa** el aparato: manual del operador y guía del invitado. |
| [`funciones/`](funciones/) | Cómo funciona cada parte, una por archivo: video, audio, plugins, MCP, sesión remota, eventos, PTZ, superresolución… |
| [`desarrollo/`](desarrollo/) | Para quien lo compila: macOS, compilación, tests, el mapa GPU/CPU del motor, la migración a GStreamer. |
| [`planes/`](planes/) | Planes en curso, ideas, decisiones pendientes y el informe de experto con sus tareas. |
| [`modelos/`](modelos/) | El modelo local: entrenamiento, corpus, voz, batería de pruebas y las tarjetas de cada modelo publicado. |
| [`historia/`](historia/README.md) | Lo que fue cierto en su momento y ya no: por qué el sistema quedó como quedó. |
| [`bosch/`](bosch/README.md) | La central de conferencias Bosch DCN (Open Interface, DDI). |
| [`lsp-matrix-mixer/`](lsp-matrix-mixer/PR.md) | Parches propios a LSP Plugins. |
| [`prompts/`](prompts/) | Prompts de referencia. |
| [`modules/`](modules/) | Páginas Sphinx de los módulos de `src/`. |

En la raíz quedan la entrada de Sphinx (`index.rst`, `conf.py`,
`requirements.txt`; `make docs`) y `mcp.json`, el inventario de herramientas MCP
que genera `make mcp-inventario`.
