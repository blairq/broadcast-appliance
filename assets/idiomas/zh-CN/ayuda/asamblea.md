大会会议（市议会、理事会、董事会、业主委员会）不是一份由提示点组成的串联单：发言权的变化没有事先排好的顺序，每个席位在轮到时都必须能被看到、被听到。按以下顺序准备，`asamblea_revisar` 会随时告诉你缺什么以及如何解决。**开始时和结束时都运行一次。**

1. **议事机构：** 每位成员及其席位（用 `cuerpo_importar` 从电子表格导入，或用 `integrante_editar`）。没有席位的成员不会出现在会场中。
2. **会场：** 从导播间看过去的席位网格（`recinto_editar {filas, columnas, auto: true}`）。
3. **每个席位的镜头：** 为每个席位设置一个能框住它的 PTZ 位置（`ptz_posicion_crear`），并关联到该**席位**（`banca_toma {banca, camara, posicion}`；即使坐在那里的人换了，它仍属于该席位，空席位也有）。发言人和辩论场景使用来源“Banca con la palabra”（`asamblea_escenas_base {crear: true}`）：它显示正在发言者的镜头（槽位 1；辩论时还有槽位 2）。使用数字摄像机时，它从完整画面中裁切，不移动任何东西（全景保持不变，两个席位可以同时出现）；电动摄像机会自行转到该位置。“对准第 10 席 / 张三 / 主席”：`enfocar {que}`。
4. **场景**（最常被忘记的）：给某个席位准予发言时，会**载入**显示其摄像机的场景；如果没有场景显示它，就谁也不会出现。需要：
   - 每台拍席位的摄像机一个场景（如果想在画面上显示姓名，加上字幕条 `{orador}`）；
   - 一个全景；
   - 一个带“Remoto con la palabra”的场景（用手机发言的成员）；
   - 辩论用的场景：两位远程成员并排（“Remoto con la palabra”和“Remoto con la palabra 2”）、一位在会场一位远程、两位都在会场；
   - 如果要在画面上显示表决，再加一个结果场景。
   `asamblea_escenas_base` 会用现有的摄像机和输入提出一套搭好的方案（把方案给操作员看），加 `crear: true` 会创建它们并配置辩论。之后还可以再修饰（字幕条、背景、标志）。
5. **会议中央单元**（如果有）：`help {"topic": "conferencia"}`；每个席位对应其单元。
6. **远程成员：** `help {"topic": "sesion"}`；给远程参会的人链接和 PIN；他们在手机上看到什么（`sesion_vistas`）。
7. **议程**（`orden_del_dia_cargar`）：在手机上和主席控制台上显示。
8. **辩论**（如果会有）：`debate_config`，设置每种类型的场景和每轮的时间。
9. **串联单**（`evento_armar`）用于确实有固定顺序的部分：开场、奏国歌、结束。会议的主体由发言队列（`banca_*`）掌控，而不是提示点。

会议进行中：`banca_al_aire` / `recinto_orador_terminar`（发言权）、`presidencia_callar_todos` 和 `presidencia_hablar`、`debate_iniciar` / `debate_turno`，表决用 `dcn_votacion` 或 `sesion_votacion`。

要在不动真实数据的情况下测试或演示这一切，有一个完整的演示（一个有 15 名成员的理事会、会场、模拟的中央单元、场景和辩论）：`configuracion_crear {"nombre": "Demo", "desde": "demo:Demo parlamentaria"}`，然后 `configuracion_cambiar`（见 `help {"topic": "configuraciones"}`）。
