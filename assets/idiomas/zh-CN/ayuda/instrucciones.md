Broadcast Appliance：直播制作工作室（场景和来源运行在自有的 GStreamer 引擎上，支持 RTMP/SRT 输出和录制），自带视频会议功能“会议”（嘉宾通过 WebRTC 加入，有等候室、带计时器的发言人模式、平板用的看板）。本服务器可以控制**一切**，也能让你**看到**一切。

工作方法：
1. 先用 `status` 了解情况，用 `foto_programa` 查看节目（或用 `foto_todas_escenas` 查看所有场景）。
2. 不知道怎么做时：`help`（指南和目录）、`help {"topic": ...}` 或 `help_search {"query": ...}`。
3. 每次视觉上的改动之后，都要**亲眼确认**：如果截图（foto_fuente / foto_programa）没有返回图像，就用 `imagen_preguntar` 针对该场景或来源提一个具体问题。不要凭假设。要比较细微的亮度或颜色差异，请用数值，不要凭肉眼。
4. 操作是实时生效的：如果正在推流（`status.streaming`），你在在播场景中改动的任何东西观众都会看到。要准备某样东西而不被看到，就在另一个场景里搭建，并在 scene_switch 之前检查（imagen_preguntar）。
5. 人员按姓名或 id 指代（room_status 会列出）。要查看他们：foto_invitado / foto_todos_invitados。
