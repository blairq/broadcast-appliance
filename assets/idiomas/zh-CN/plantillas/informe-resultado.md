# 会议结果 — {{cuerpo}}

**{{evento}}** · {{fecha}} {{hora}}

## 法定人数

**{{quorum.estado}}**：出席 {{quorum.presentes}} 人，需要 {{quorum.necesarios}} 人（共 {{quorum.miembros}} 名成员）。

## 议程

{{#puntos.0}}
| 序号 | 议案 | 事由 | 状态 | 结果 |
|---|---|---|---|---|
{{/puntos.0}}
{{#puntos}}
| {{n}} | {{expediente}} | {{titulo}} | {{estado}} | {{resultado}} |
{{/puntos}}
{{^puntos}}
未载入议程。
{{/puntos}}

## 表决

{{#votaciones}}
### {{asunto}}

赞成 **{{afirmativos}}**，反对 **{{negativos}}**，弃权 **{{abstenciones}}**（出席 {{presentes}}；{{mayoria}}）：**{{resultado}}**。

{{/votaciones}}
{{^votaciones}}
没有已载入的正式表决。
{{/votaciones}}

## 发言记录

{{#palabra}}
- {{nombre}}：{{tiempo}}
{{/palabra}}
{{^palabra}}
没有发言记录。
{{/palabra}}

---

_生成于 {{generado}}。_
