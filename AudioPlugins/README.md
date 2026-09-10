# AudioPlugins

Directorio donde viven los bundles `.vst3` de **LSP Plugins** (versión fijada
`LSP_VERSION = 1.2.35`, ver `versions.cmake`).

## Qué contiene

- Los bundles `.vst3` de LSP que el host de plugins (`ba_plugin_host`,
  spec D6/§7.5 — LSP como componente CORE) carga y embebe su editor gráfico.
- `manifest.json`: índice generado por el paso de despliegue
  (`ba::plugin::PluginDeployer::deploy`) con la lista de bundles
  `{name, bundlePath, dllPath}` que el host consume para descubrirlos.

## Cómo se pobla

Este directorio se **puebla con el paso de deploy** (LSP como componente
CORE): `PluginDeployer::deploy(sourceDir, installDir)` copia cada bundle
`.vst3` desde el directorio de origen hasta aquí y reescribe `manifest.json`.
No se edita a mano.

> Nota: los bundles son un **asset de despliegue** (no se compilan). La
> versión de LSP de la que se toman los `.vst3` está fijada en
> `versions.cmake` (`LSP_VERSION`).
