Uma sessão de assembleia (câmara, conselho, diretoria, condomínio) não é um roteiro de deixas: a palavra muda sem ordem prévia e cada cadeira tem que ser vista e ouvida quando chega a sua vez. Prepara-se nesta ordem, e `asamblea_revisar` diz a cada momento o que falta e como se corrige. **Rode-o ao começar e ao terminar.**

1. **O Colegiado:** cada membro com sua cadeira (`cuerpo_importar` a partir de uma planilha, ou `integrante_editar`). Sem cadeira, ele não aparece no plenário.
2. **O plenário:** a grade de cadeiras como são vistas da cabine (`recinto_editar {filas, columnas, auto: true}`).
3. **A tomada de cada cadeira:** para cada cadeira, uma posição PTZ que a enquadre (`ptz_posicion_crear`) associada à CADEIRA (`banca_toma {banca, camara, posicion}`; fica na cadeira mesmo que mude quem se senta, e uma vazia também a tem). As cenas do orador e do debate usam a fonte “Banca con la palabra” (`asamblea_escenas_base {crear: true}`): mostra a tomada de quem fala (slot 1; num debate, também o 2). Com uma câmera digital, ela a recorta da tomada inteira sem mover nada (o plano geral continua igual e duas cadeiras são vistas ao mesmo tempo); uma motorizada vai sozinha para a posição. “Focar a cadeira 10 / o Fulano de Tal / a Presidência”: `enfocar {que}`.
4. **AS CENAS** (o que mais se esquece): ao conceder a palavra a uma cadeira, a cena que mostra a câmera dela é CARREGADA; se nenhuma cena a mostra, não aparece ninguém. É preciso:
   - uma cena para cada câmera de cadeiras (com o GC `{orador}` se quiser o nome na tela);
   - um plano geral;
   - uma com “Remoto con la palabra” (o membro que fala pelo celular);
   - as do debate: dois remotos lado a lado (“Remoto con la palabra” e “Remoto con la palabra 2”), um da sala e um remoto, dois da sala;
   - se houver votação na tela, a do resultado.
   `asamblea_escenas_base` as propõe montadas com as câmeras e entradas que existem (mostre a proposta ao operador) e com `crear: true` as cria e configura o debate. Depois dá para retocá-las (GC, fundo, logo).
5. **A unidade central de conferência** (se houver): `help {"topic": "conferencia"}`; cada cadeira com sua unidade.
6. **Os remotos:** `help {"topic": "sesion"}`; link e PIN para os que participam a distância; o que veem no celular (`sesion_vistas`).
7. **A ordem do dia** (`orden_del_dia_cargar`): aparece nos celulares e no console da presidência.
8. **O debate** (se houver): `debate_config` com a cena de cada tipo e o tempo de cada turno.
9. **O roteiro** (`evento_armar`) para o que tem ordem: abertura, hino, encerramento. O corpo da sessão é conduzido pela fila de oradores (`banca_*`), não pelas deixas.

Durante a sessão: `banca_al_aire` / `recinto_orador_terminar` (a palavra), `presidencia_callar_todos` e `presidencia_hablar`, `debate_iniciar` / `debate_turno`, votação com `dcn_votacion` ou `sesion_votacion`.

Para testar ou mostrar tudo isso sem mexer no que é de verdade, há uma demo completa (um conselho com 15 membros, plenário, unidade central simulada, cenas e debate): `configuracion_crear {"nombre": "Demo", "desde": "demo:Demo parlamentaria"}` e `configuracion_cambiar` (ver `help {"topic": "configuraciones"}`).