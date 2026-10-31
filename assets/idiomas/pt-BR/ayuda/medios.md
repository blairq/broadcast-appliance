Pastas (dentro do sistema portátil): `Media/` (a biblioteca: logos, cartelas, fundos, música, vídeos), `Shares/` (o que os convidados compartilharam), `Recordings/`.

## Receita: "procure a imagem X e use como logo em cima da cena 4"
1. `media_find {"query": "X"}` (sem importar maiúsculas, acentos nem hifens).
2. `media_view {"path": ...}` para confirmar que é a certa (olhe para ela).
3. `scene_list` para encontrar a cena ("a 4" pode ser a quarta da lista ou uma que se chame assim: na dúvida, pergunte).
4. `medio_poner {"path": ..., "scene": ..., "name": "Logo", "layout": "pip-tr"}` (em cima à direita; `pip-tl` em cima à esquerda; ou `{x,y,w,h}`).
5. `foto_fuente {"name": escena}` e ajustar com `source_update` (posição, tamanho) até ficar bom.

Vídeos e áudios: `medio_poner` com `loop` para fundos ou música em loop; `alert_create {"type": "media_end"}` para saber quando terminam.

## Organizar o disco
`media_carpetas` diz COMO estão distribuídos (a árvore, com quantos itens cada pasta tem; as vazias aparecem também, porque são para onde as coisas são movidas). `media_ordenar {"accion": "mover"|"renombrar"|"carpeta"|"borrar"}` os organiza.

**Apagar não apaga**: manda para `Papelera/`. Um arquivo pode estar numa cena, ou ser o único que restava do evento passado; desfazer tem que ser tirá-lo de uma pasta. E se no destino já houver algo com esse nome, ele é numerado em vez de sobrescrever.

As pastas do sistema (Media, Shares, Recordings) não são movidas nem apagadas. O operador vê tudo isso na aba Mídia, e o que você mudar aqui aparece para ele sem precisar atualizar.

## Apresentações
Um `.pdf` ou um `.pptx` não é "usado" numa cena: ele é DESMONTADO em slides e vira um carrossel, que é o que permite pular para qualquer um ao vivo. Ver `help {"topic": "laminas"}`.