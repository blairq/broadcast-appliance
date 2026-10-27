- `system_hardware`: todo de una vez (monitores, placas de audio de entrada y salida, camaras/capturadoras, GPU, codificadores de hardware, CPU, memoria, disco).
- `monitors`: en orden fisico, con `id` estable y `orden` (el numero de Salidas -> Pantallas), resolucion en pixeles, posicion, escala, Hz y principal. `output_window.screen` y `monitor_asignar` toman el id o el orden.
- `audio_devices`: entradas y salidas de audio del registro de la maquina: `name` es el nombre que les puso el operador (`tecnico`, el del sistema), `conectado` si estan. El `value` de una entrada es el `device` de `audio_input_add`. Para nombrarlas: `equipo_renombrar`.
- `video_devices`: camaras y capturadoras, del mismo registro (nombre del operador y tecnico); con `{"device": value, id o nombre}`, sus formatos, resoluciones y cuadros por segundo.

## Receta: ocupar todos los monitores
1. `monitors` y `dashboard_list` (ventanas abiertas).
2. Programa en el monitor mas grande que NO es el principal (el principal queda para la app): `monitor_asignar {"monitor": "<orden>", "cosas": ["programa"]}` (queda fijo y vuelve solo al reiniciar).
3. Si hay un tercero, multiview de las escenas o de los invitados (`output_window {"open": ["guest:1","guest:2"], "layout": "grid", "screen": ...}`).
4. Contar como quedo.