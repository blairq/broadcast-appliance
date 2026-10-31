虚拟席位（docs/funciones/SESION-REMOTA.md）：议事机构的每位成员都可以用浏览器（手机、平板、电脑）凭其**个人链接和 PIN** 加入，无论是在会场还是在家里。它也是中央单元的备份：如果 CCU 故障，成员可以用手机加入。
1. **链接和 PIN：** `sesion_credencial {"banca": "12"}` 生成并**只显示一次**（如果之前已有，旧的会失效）。用 `sesion_acceso_opciones {"acceso_fijo": true}` 会固定下来：可以再次获取且返回相同的内容，只有 `sesion_credencial {"banca", "renovar": true}` 才会改变；用 `{"pin_en_qr": true}` 会把 PIN 放进二维码（以及 `link_con_pin`）：扫码即可进入，无需输入。通过安全渠道交给成员。`sesion_clave {"banca", "usuario"}` 给他用户名和密码，可以不用链接、直接输入登录；`sesion_datos` 保存他的邮箱、电话和备注。`sesion_revocar` 切断其访问。这些在“大会 → 远程连接”中也可以操作。
2. **准许：** 成员进入后处于“等待中”。由一个人通过摄像头看他（在“大会 → 会场”中，他的席位会显示人脸并闪烁琥珀色），然后准许：`sesion_habilitar {"banca": "12"}`。准许后，他在法定人数中计为出席（如果中央单元已经把他算作出席，不会重复计算）。**绝不要准许没有任何人看过的人。**
3. `sesion_estado`：谁已连接、从哪里（会场内/外）、由谁准许。`sesion_expulsar` 把他移出。
4. `sesion_registro {"texto": true}`：要写入会议纪要的内容（进入、准许、失败的尝试、封锁）。
- 安全：PIN 错误 5 次会封锁该访问 15 分钟；一位成员，一个连接。
- **发言：** 手机申请发言，进入同一个队列（`banca_cola`）；`banca_al_aire` 准予发言，手机上显示倒计时；`banca_tiempo` 增加或减少时间。
- **表决：** 如果中央单元发起表决，已准许的成员手机上会出现红色提示；没有中央单元时，用 `sesion_votacion {"accion": "abrir", "tema", "menu", "segundos"}`。每位成员向法定人数计一票。
- **主席控制台：** `sesion_consola {"accion": "crear"}` 给出 …/presidencia/ 的链接和 PIN（带人脸的会场、队列和席位，有“按下发言”、全部静音、“我来发言”、辩论、表决、在播节目）。
- **他的画面和声音：** 正在发言的人以“Remoto con la palabra”进入（音频、视频和“— contenido”，即他展示的内容）；在远程成员之间的辩论中，另一位以“Remoto con la palabra 2”进入。场景中必须显示这些输入。
- **他在手机上看到的：** 自己的摄像头、主输出或另一个场景（`sesion_vistas`），自己的画面显示在一个小框里。
- **对他说话：** `sesion_hablar {"banca", "activo"}`（你在会场的麦克风声音会传到他的手机）。
