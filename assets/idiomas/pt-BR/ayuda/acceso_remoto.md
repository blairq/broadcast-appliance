Três caminhos, **um de cada vez** (Configurações → “Acesso pela internet”): **Pinggy** (o de sempre), **Cloudflare** (domínio da organização na Cloudflare) e **Túnel próprio (WireGuard)** (um servidor com IP público). Qualquer um reinicia a conferência ao ser trocado: **nunca com o evento ao vivo** (as ferramentas recusam).

## Pinggy: com a API key, tudo sozinho
A conta Pro do pinggy tem **duas chaves diferentes**: a **API key** (gerencia a conta) e o **token do túnel** (abre o túnel). **O operador só precisa passar a API key para você**; o token o app busca.
1. `acceso_remoto_clave {"tipo": "api_key", "clave": "…"}`: salva-a criptografada (nunca é devolvida nem escrita no log; não a repita no chat).
2. `acceso_remoto_configurar_con_api_key {}`: pede à conta o token (salva-o criptografado), seu **subdomínio fixo** (algo.a.pinggy.link) e sua **porta TCP fixa** (para o vídeo de quem está fora), e liga o túnel com isso. Com `{"subdominio": "salaconcejo"}` (só minúsculas e números: o pinggy não aceita hifens) primeiro **muda o subdomínio na conta** (fica salaconcejo.a.pinggy.link); se o pinggy não o der (ocupado ou inválido), ele diz: ofereça outro nome.
3. `acceso_remoto_estado`: espere `pinggy.estado` = `listo` e passe ao operador `pinggy.urlPublica` e os `enlaces` (convidados, sessão remota, público). Para escanear: `qr_mostrar`.
- `acceso_remoto_panel`: quais túneis a conta tem ativos, visto do lado do pinggy. `acceso_remoto_cortar_tuneles`: corta todos (p. ex. outro equipamento que ficou com o mesmo token: o pinggy permite **um túnel por token**).
- **Não cole a API key como token** (`tipo: "token"`): não abre o túnel (o pinggy responde “não autenticado”) e o app avisa que são iguais. O token manual (`tipo: "token"`) é só se não houver API key.
- `acceso_remoto_configurar` muda o resto (região, domínio próprio com `host` e `cname_destino`, TURN, lista branca). Domínio da organização: `acceso_remoto_dns_instrucciones` monta o texto para quem gerencia o DNS e `acceso_remoto_dns_verificar` confere o CNAME.
- Sem plano Pro (sem token): funciona igual no modo grátis, 60 minutos, endereço aleatório e com um aviso do pinggy que os convidados têm que aceitar.

## Cloudflare
É preciso o **token do túnel da Cloudflare** (Zero Trust → Networks → Tunnels): `acceso_remoto_clave {"tipo": "cloudflare", "clave": "…"}`, depois `settings_set {"values": {"gateway.tunnel": true, "gateway.tunnelHost": "sala.midominio.com"}}`. Sem token fica desligado.

## Túnel próprio (WireGuard)
Um servidor próprio com IP público: `settings_set` com `wg.enabled`, `wg.endpoint` (host:porta), `wg.serverKey`, `wg.address`, `wg.publicAddress`. É o melhor caminho para o vídeo, mas alguém tem que ter montado o servidor.