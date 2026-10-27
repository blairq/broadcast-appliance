Carpetas (dentro del sistema portable): `Media/` (la biblioteca: logos, placas, fondos, musica, videos), `Shares/` (lo que compartieron los invitados), `Recordings/`.

## Receta: "busca la imagen X y usala de logo arriba de la escena 4"
1. `media_find {"query": "X"}` (sin importar mayusculas, tildes ni guiones).
2. `media_view {"path": ...}` para confirmar que es la correcta (mirala).
3. `scene_list` para encontrar la escena ("la 4" puede ser la cuarta de la lista o una que se llame asi: si hay duda, pregunta).
4. `medio_poner {"path": ..., "scene": ..., "name": "Logo", "layout": "pip-tr"}` (arriba a la derecha; `pip-tl` arriba a la izquierda; o `{x,y,w,h}`).
5. `foto_fuente {"name": escena}` y ajustar con `source_update` (ubicacion, tamaño) hasta que quede bien.

Videos y audios: `medio_poner` con `loop` para fondos o musica en bucle; `alert_create {"type": "media_end"}` para enterarte cuando terminan.

## Ordenar el disco
`media_carpetas` dice COMO estan repartidos (el arbol, con cuantos tiene cada carpeta; las vacias aparecen igual, porque son a donde se mueven cosas). `media_ordenar {"accion": "mover"|"renombrar"|"carpeta"|"borrar"}` los acomoda.

**Borrar no borra**: manda a `Papelera/`. Un archivo puede estar en una escena, o ser el unico que quedaba del evento pasado; deshacer tiene que ser sacarlo de una carpeta. Y si en el destino ya hay algo con ese nombre, se numera en vez de pisar.

Las carpetas del sistema (Media, Shares, Recordings) no se mueven ni se borran. El operador ve todo esto en la pestaña Medios, y lo que cambies aca le aparece sin que tenga que refrescar.

## Presentaciones
Un `.pdf` o un `.pptx` no se "usa" en una escena: se DESARMA en laminas y queda un carrusel, que es lo que deja saltar a cualquiera en vivo. Ver `help {"topic": "laminas"}`.