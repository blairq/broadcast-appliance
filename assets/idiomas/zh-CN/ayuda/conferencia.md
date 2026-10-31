会场的中央单元（Bosch DCN Next Generation）由插件 `bosch-dcn` 控制；这些工具说的是**席位**（议事机构的席位），而不是单元。
1. **连接：** `dcn_puertos`（USB 转串口适配器）和 `dcn_conectar {"transporte": "serie", "puerto": ..., "velocidad": 19200}`；通过网络（CCU2）用 `{"transporte": "tcp", "host": ...}`；没有设备时测试用 `{"transporte": "simulador", "solo_escuchar": false}`。默认处于**只监听**模式（接收一切，不发送指令）：会场响应正常后，用 `dcn_conectar {"solo_escuchar": false}` 关闭它。
2. **单元：** 每个席位都需要对应的单元（议事机构中的 `dcn_unidad`）。`dcn_unidades` 显示缺少哪些；`dcn_unidad_asignar {"aprender_todas": true}`，然后有人在会场中按顺序按下每个席位的按钮。
3. **操作：** `dcn_estado`（已打开的、按顺序排列的发言申请、`siguiente`）、`dcn_microfono {"banca", "abierto"}`、`dcn_siguiente`、`dcn_pedido {"banca", "accion"}`、`dcn_cerrar_todos`（不影响主席）、`dcn_modo {"modo": "operador", "max_abiertos": 4}`。
- 程序和控制台只有在**操作员**模式下才能准予发言；在其他模式下由中央单元决定，但照样能看到发生了什么。
- 连接中央单元时，`banca_al_aire` 也会打开该席位的麦克风（并关闭上一位的）；`recinto_orador_terminar` 关闭它。
- 打开或关闭麦克风的工具属于**在播类**：如果在推流时由自动化触发，需要确认。
- 每个麦克风通知都会以 `plugin/bosch-dcn/conferencia/microfono {banca, abierto}` 的形式发到总线上：“跟随麦克风”（来源 «conferencia»）会用到它。
- **表决：** `dcn_votacion {"accion": "preparar", "tema": ..., "menu": "si_no_abstencion"}`，然后 `abrir` 和 `cerrar`；`dcn_votacion_resultado`（中间结果和按席位的记名结果）；正式结果用 `dcn_votacion_a_quorum` 送到法定人数（没有人要求时绝不要载入）。
- **出席：** `dcn_asistencia`；`dcn_presentes_quorum {"activo": true}` 让法定人数计入这些出席者。
- **时间：** 现场发言人在播且分配了时间时，其单元的计时器会自动设置，并在剩一分钟和结束时提醒（`dcn_tiempo` 可手动设置）。
- **双人 DDI：** 双人模式下的一个 DCN-DDI 是两个麦克风：左半边是单元 N，右半边是 N+256；各自分配给自己的席位（docs/bosch/dcn-ddi.md）。
- **未公开的功能**（来自 Bosch 软件，未在真实 CCU 上测试）：`dcn_funciones` 列出它们，`dcn_funcion {"nombre", "args"}` 调用它们；危险的需要 `confirmar`。有专用名称的：`dcn_proximo`（下一位发言者，点名的轮次）、`dcn_cerrar_tras_siguiente {"activo": true}`、`dcn_llamado_nominal {"bancas": [...]}`。如果主席在自己的单元上操作表决，`dcn_votacion_resultado` 的状态会自动跟随。
- 问题排查：`dcn_traza {"activa": true}` 和 `dcn_traza {"ultimas": 50}`（十六进制的数据帧和重发计数）。完整指南：docs/bosch/open-interface.md。
