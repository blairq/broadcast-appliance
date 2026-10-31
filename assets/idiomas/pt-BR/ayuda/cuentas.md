## O que dá para automatizar e o que não
Criar o app/credencial no Google Cloud, na Twitch ou na Meta tem que ser feito pela PESSOA (com a conta dela, aceitando os termos de cada plataforma): você a guia passo a passo. Todo o resto você faz.

## Fluxo
1. `accounts_status`: o que falta em cada plataforma e o próximo passo (`next`).
2. `account_setup_guide {"platform"}`: os passos, os VALORES EXATOS para colar (`paste`) e os campos do formulário. Explique UM passo de cada vez, de forma simples e curta.
3. `account_open {"platform", "step"}` abre a página do passo no navegador. Espere a pessoa dizer que terminou (ou pergunte com `operator_ask`) antes do próximo.
4. Credencial:
   - YouTube: se ela já baixou o JSON do Google, `youtube_setup {"fromDownloads": true}` o importa sozinho da pasta Downloads.
   - Se não (ou na Twitch/Facebook): `account_credentials_request {"platform"}` mostra um formulário na barra; a pessoa cola os dados no app e eles são salvos criptografados. **Nunca peça que ela cole um segredo no chat**: o chat passa pelo provedor de IA.
5. Login: `youtube_login`, `twitch_login` (mostra um código: diga a ela que o confirme em twitch.tv/activate) ou `facebook_login` (depois `facebook_pages` para escolher a página).
6. `account_check {"platform"}`: teste real (canal, live habilitada, permissões, página). Se houver `problem`, explique e ajude a resolver.

## Problemas comuns
- YouTube "não tem as transmissões ao vivo habilitadas": habilita-se em youtube.com/features e na primeira vez demora até 24 h.
- O YouTube pede para reconectar a cada 7 dias: o app do Google está em modo de teste; em "Público" -> "Publicar app" deixa de expirar.
- YouTube "app não verificado" ao conectar: é normal (o app é da própria pessoa): "Avançado" -> "Ir para ...".
- A Twitch não deixa registrar o app: falta a verificação em duas etapas.
- Facebook "URL bloqueada"/redirect: a URI de redirecionamento do app da Meta tem que ser EXATAMENTE a de `paste.uriDeRedireccion`.
- Facebook sem páginas: a API transmite para PÁGINAS que a pessoa administra, não para o perfil pessoal.