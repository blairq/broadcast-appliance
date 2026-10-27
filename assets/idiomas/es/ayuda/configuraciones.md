Cada configuración es una carpeta con TODO lo suyo: escenas y ajustes (Config), medios, gráficos, plugins, fotos de los integrantes, miniaturas, eventos y grabaciones. **Nada se comparte**: tocar un plugin o una escena en una no rompe otra. De la máquina (de todas) quedan los modelos, las credenciales, los logs y el caché.

- `configuraciones_listar`: cuáles hay, la activa, su descripción, tamaño y de dónde salió; y las demos que trae el programa.
- `configuracion_crear {"nombre", "desde", "descripcion"}` crea una (no la activa). `desde`: `vacia` (de fábrica), `actual` (copia de la que está en uso), el nombre de otra, `demo:<nombre>` (una demo del programa) o `evento:<carpeta>/<inicio|cierre|ultima>` (la configuración con la que se hizo un evento: su foto de ajustes y los medios de la actual). Las grabaciones no se copian salvo `con_grabaciones`.
- `configuracion_cambiar {"nombre"}` pasa a otra: **la app se reinicia** con esa carpeta (no copia nada). Corta lo que esté andando (transmisión, grabación, conferencia): nunca lo hagas en vivo sin que lo pida el operador.
- `configuracion_editar {"nombre", "renombrar_a"|"descripcion"|"borrar": true}`: renombrar, describir o mandar a la papelera (`Configuraciones/.papelera`). La activa no se renombra ni se borra.

**La demo**: `demo:Demo parlamentaria` es un sistema parlamentario genérico listo para probar: el Consejo de la Ciudad de Puerto Ejemplo (15 integrantes y presidencia con nombres inventados), recinto, cámaras, central de conferencia simulada, escenas de asamblea y de debate, botonera y orden del día.

**Portable**: todas las rutas se guardan relativas a la configuración (`Media/logo.png`, también dentro de un pipeline: `location=Media/x.png`). La carpeta de datos entera se puede copiar a otra máquina tal cual. Al crear una fuente con una ruta relativa, se resuelve contra la configuración en uso.

En la interfaz: Ajustes › Sistema › Configuraciones (usar, duplicar, renombrar, borrar, crear desde una demo).