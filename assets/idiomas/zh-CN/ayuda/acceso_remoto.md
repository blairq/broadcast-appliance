三种方式，**一次只用一种**（设置 → “从互联网访问”）：**Pinggy**（常用的那个）、**Cloudflare**（组织在 Cloudflare 上的域名）和 **自有隧道（WireGuard）**（一台有公网 IP 的服务器）。切换任何一种都会重启会议：**活动直播中绝不能切换**（工具会拒绝）。

## Pinggy：有了 API key，一切自动完成
pinggy 的 Pro 账号有**两个不同的密钥**：**API key**（管理账号）和**隧道 token**（打开隧道）。**操作员只需要把 API key 给你**；token 由程序自己获取。
1. `acceso_remoto_clave {"tipo": "api_key", "clave": "…"}`：加密保存（绝不会返回，也不会写入日志；不要在聊天中重复它）。
2. `acceso_remoto_configurar_con_api_key {}`：向账号获取 token（加密保存）、它的**固定子域名**（algo.a.pinggy.link）和**固定 TCP 端口**（用于外部人员的视频），并用这些打开隧道。用 `{"subdominio": "salaconcejo"}`（只能用小写字母和数字：pinggy 不接受连字符）会先**在账号中修改子域名**（变成 salaconcejo.a.pinggy.link）；如果 pinggy 不给（已被占用或无效），它会说明：换一个名字。
3. `acceso_remoto_estado`：等 `pinggy.estado` = `listo`，然后把 `pinggy.urlPublica` 和 `enlaces`（嘉宾、远程会议、观众）告诉操作员。要让人扫码：`qr_mostrar`。
- `acceso_remoto_panel`：从 pinggy 那边看，账号有哪些活动的隧道。`acceso_remoto_cortar_tuneles`：全部断开（例如另一台设备还在用同一个 token：pinggy **每个 token 只允许一条隧道**）。
- **不要把 API key 当作 token 粘贴**（`tipo: "token"`）：那样打不开隧道（pinggy 回答“未认证”），程序也会提示两者相同。手动填写 token（`tipo: "token"`）只用于没有 API key 的情况。
- `acceso_remoto_configurar` 修改其他设置（区域、用 `host` 和 `cname_destino` 设置自有域名、TURN、白名单）。组织的域名：`acceso_remoto_dns_instrucciones` 为管理 DNS 的人生成说明文字，`acceso_remoto_dns_verificar` 检查 CNAME。
- 没有 Pro 套餐（没有 token）：免费模式照样能用，60 分钟、随机地址，嘉宾需要接受 pinggy 的一个提示页。

## Cloudflare
需要 **Cloudflare 隧道 token**（Zero Trust → Networks → Tunnels）：`acceso_remoto_clave {"tipo": "cloudflare", "clave": "…"}`，然后 `settings_set {"values": {"gateway.tunnel": true, "gateway.tunnelHost": "sala.midominio.com"}}`。没有 token 就保持关闭。

## 自有隧道（WireGuard）
一台有公网 IP 的自有服务器：用 `settings_set` 设置 `wg.enabled`、`wg.endpoint`（host:puerto）、`wg.serverKey`、`wg.address`、`wg.publicAddress`。这是视频效果最好的方式，但需要有人事先搭好服务器。
