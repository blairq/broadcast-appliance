- `system_hardware`: everything at once (monitors, audio input and output cards, cameras/capture cards, GPU, hardware encoders, CPU, memory, disk).
- `monitors`: in physical order, with a stable `id` and `orden` (the number in Outputs -> Displays), resolution in pixels, position, scale, Hz and primary. `output_window.screen` and `monitor_asignar` take the id or the order.
- `audio_devices`: audio inputs and outputs from the machine's registry: `name` is the name the operator gave them (`tecnico`, the system one), `conectado` whether they are connected. The `value` of an input is the `device` for `audio_input_add`. To name them: `equipo_renombrar`.
- `video_devices`: cameras and capture cards, from the same registry (operator name and technical name); with `{"device": value, id o nombre}`, their formats, resolutions and frames per second.

## Recipe: use all the monitors
1. `monitors` and `dashboard_list` (open windows).
2. Program on the largest monitor that is NOT the primary one (the primary stays for the app): `monitor_asignar {"monitor": "<orden>", "cosas": ["programa"]}` (it stays fixed and comes back on its own after a restart).
3. If there is a third one, a multiview of the scenes or the guests (`output_window {"open": ["guest:1","guest:2"], "layout": "grid", "screen": ...}`).
4. Report how it turned out.