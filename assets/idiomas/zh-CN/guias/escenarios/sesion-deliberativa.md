# 议事机构会议

## 用途
一场正式会议：成员在各自席位上，有主席、秘书处、议程、按规定使用的发言权、法定人数，以及有正式结果的记名表决。公开并推流。
另请阅读 help {"topic": "asamblea"} 和大会会议流程。

## 专门要问的
- 哪个议事机构，多少席位，谁主持，谁是秘书？
- 议事机构（成员、党团、席位）已经载入了吗？如果没有：cuerpo_importar 或 cuerpo_plantilla。
- 有会议中央单元（席位麦克风，例如 Bosch DCN）吗？有按席位设置位置的 PTZ 摄像机吗？
- 有远程成员（虚拟席位）吗？他们远程表决吗？
- 议程和议案。有议题需要特别多数吗？
- 发言时间？程序性问题、答辩？
- 推流、手语翻译（DSK 图层）、字幕？
- 会场里有电视显示表决吗？

## 要搭建的
- 议事机构和会场（recinto_editar），每个席位及其摄像机预置位（ptz_posicion_crear）。
- 大会基础场景（asamblea_escenas_base）：主席、正在发言的席位、全景、表决、休会；用 asamblea_revisar 检查。
- 议程（orden_del_dia_cargar），按议题附上文件。
- 法定人数（quorum_regla）和表决（votacion_abrir / votacion_resultado_oficial）。
- 如果有远程成员：sesion_habilitar、凭据（sesion_credencial），以及逐一测试。
- 会场屏幕显示表决（pantalla_mostrar 内容 votacion）。
- 如果用平板操作，配主席或麦克风控制台（模板“Asamblea”、“Micrófonos de bancas”）。

## 检查
中央单元已连接（dcn_conectar），摄像机能转到各自位置，法定人数按真实名册计算，一位远程成员做测试投票，公开网页上有议程。

## 交付
给远程成员的凭据、公开网页、主席控制台；结束后，交付记录表决的会议纪要（evento_acta）和按议题分章节的录像（evento_capitulos）。
