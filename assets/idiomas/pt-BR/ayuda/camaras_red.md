Para "quais câmeras existem?", "adicione a câmera do pátio", "conecte o DVR", "tenho uma Axis/Hikvision/EZVIZ…".

## O caminho (sempre este)
1. `red_escanear` (uns 10 s): cada equipamento com seu IP, MAC e fabricante, a marca (e por que a reconheceu), suas portas, as URLs RTSP típicas testadas (`se ve` / `pide usuario y clave` / `no existe`), `notas` da marca e `siguiente_paso`. **Siga o `siguiente_paso`.** Conte ao operador o que encontrou em poucas linhas (quantas câmeras, de que marca, quais já funcionam e quais pedem senha), não jogue o JSON nele.
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
2. Se pedir senha: `camara_credenciales {"ips": [...], "pedir": true}` (UM formulário para todas as que pedirem; se já houver um aberto, adiciona-as a ele. Volta na hora: quando o operador o preenche, chega a você “[OPERADOR] guardo…” no chat; enquanto isso, siga com o resto). É um formulário seguro na barra: **nunca peça a senha pelo chat**. Antes, diga a ele qual usuário vai (`usuario_de_fabrica`) e de onde sai a senha (as `notas`: EZVIZ = o código de verificação da etiqueta, e o RTSP precisa ser habilitado no app dela; Hikvision = a do gravador; Axis = a de um usuário ONVIF).
3. `camara_ficha {"ip"}`: ONVIF completo (perfis com sua URL exata, PTZ, imagem) + a análise do GStreamer do stream + `plan`. Leia o plano ao operador em duas linhas.
4. `camara_agregar {"ip", "escena", "nombre"}` com `plan.argumentos` (se já estiver conectada, `reusar: true` a adiciona a outra cena sem abrir outro stream; se a senha ONVIF chegar depois, `solo_ptz: true` + `entrada` configura o PTZ dela): a entrada fica sincronizada (buffer e relógio do estúdio), normalizada só se for preciso, e com seu PTZ ONVIF, se tiver. Pergunte em que cena ela vai se não disseram (não a coloque na que está no ar sem perguntar). Espera conectar e devolve `estado.diagnostico`: aplique o que ele propuser (p. ex. `video_entrada_red {"nombre", "ntp_sync": false}` se o relógio da câmera estiver adiantado ou atrasado; `video_entrada_red` sobre uma entrada existente a atualiza).
5. `video_estado {"entrada"}` alguns segundos depois: resolução real e diagnóstico. Se algo ficar errado, o diagnóstico diz a correção.

## Bom saber
- Um **gravador** (DVR/NVR) tem uma câmera por canal: `/Streaming/Channels/101` é o canal 1 (principal), `102` o secundário, `201` o canal 2… Cada canal é adicionado separadamente (`camara_agregar` com a `url` do canal).
- Se a marca foi reconhecida só pelo MAC, a confiança é baixa: se nenhuma URL funcionar, peça o modelo ou a URL ao operador.
- A EZVIZ usa MACs da Hikvision: vale o nome ONVIF (CS-…).
- `camara_urls {"ip"}` testa as URLs de novo (depois de salvar a senha ou de habilitar o RTSP).
- Equipamentos sem RTSP nem ONVIF (roteadores, PCs) não são câmeras: não os ofereça.
- Uma câmera de rede também pode ser adicionada manualmente com `video_entrada_red` se derem a URL a você.