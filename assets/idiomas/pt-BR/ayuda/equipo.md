- `system_hardware`: tudo de uma vez (monitores, interfaces de áudio de entrada e saída, câmeras/placas de captura, GPU, codificadores de hardware, CPU, memória, disco).
- `monitors`: em ordem física, com `id` estável e `orden` (o número de Saídas -> Telas), resolução em pixels, posição, escala, Hz e principal. `output_window.screen` e `monitor_asignar` aceitam o id ou a ordem.
- `audio_devices`: entradas e saídas de áudio do registro da máquina: `name` é o nome que o operador deu a elas (`tecnico`, o do sistema), `conectado` se estão presentes. O `value` de uma entrada é o `device` de `audio_input_add`. Para nomeá-las: `equipo_renombrar`.
- `video_devices`: câmeras e placas de captura, do mesmo registro (nome do operador e técnico); com `{"device": value, id o nombre}`, seus formatos, resoluções e quadros por segundo.

## Receita: ocupar todos os monitores
1. `monitors` e `dashboard_list` (janelas abertas).
2. Programa no monitor maior que NÃO é o principal (o principal fica para o app): `monitor_asignar {"monitor": "<orden>", "cosas": ["programa"]}` (fica fixo e volta sozinho ao reiniciar).
3. Se houver um terceiro, multiview das cenas ou dos convidados (`output_window {"open": ["guest:1","guest:2"], "layout": "grid", "screen": ...}`).
4. Contar como ficou.