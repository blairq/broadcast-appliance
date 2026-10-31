## 基础
- `scene_list`：场景及其来源（从下到上）。`live` 标记在播的那个。
- `scene_switch {"name"}`：切到在播（使用 `transition_set` 设定的转场：cut、fade、swipe、slide、fade_to_color）。
- `scene_create` / `scene_rename` / `scene_delete`。

## 一次定义一个场景：`scene_define`
```json
{"name": "Entrevista", "replace": true, "switch": false, "sources": [
  {"type": "color_source_v3", "name": "Fondo", "settings": {"color": 4280295456, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "ba_guest_video", "name": "Invitado 1", "settings": {"slot": 1}, "layout": "left"},
  {"type": "ba_guest_video", "name": "Invitado 2", "settings": {"slot": 2}, "layout": "right"},
  {"type": "text_ft2_source_v2", "name": "Zocalo", "settings": {"text": "Entrevista en vivo"}, "layout": "lower-third"}
]}
```
- 顺序是**从下到上**（后面的覆盖前面的）。
- 已存在的 `name` 会**复用**该来源（同一台摄像机用在多个场景中）。
- `scene_export` 返回的就是这个格式：导出、修改，再重新定义。

## 好的做法
- 在**未在播**的场景中搭建，并在 `scene_switch` 之前用 `foto_fuente {"name": "<escena>"}` 检查。
- 用 `foto_todas_escenas` 一眼看全所有场景。
