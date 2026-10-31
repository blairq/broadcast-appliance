Uma sequência de imagens que uma cena mostra. **Não é o `carrusel_*` das telas**: aquilo gira mensagens e QR num televisor secundário. Isto é a apresentação do orador.

## Para que serve de verdade
No meio da palestra o orador diz "volte para aquela do gráfico de barras". Com vinte slides e um botão de próximo são vinte cliques e dez segundos de ar perdidos. `laminas_ir {"carrusel", "numero"}` é um salto.

## Como se usa
- `carruseles_listar`: o que existe. Começa-se por aqui; o resto pede o nome.
- `carrusel_laminas_ver {"carrusel"}`: os slides em ordem, com seu número, seu título e suas etiquetas. O **número começa em 1** e é o que `laminas_ir` pede.
- `laminas_armar {"nombre", "imagenes"}`: montar um manualmente com cartelas avulsas (logos, fundos). Se já existe, adiciona no final.
- `laminas_editar {"carrusel", "accion"}`: tirar um slide, mudá-lo de lugar, renomear, apagar ou soltá-lo da cena. `borrar` leva o carrossel, NÃO as imagens.
- `laminas_aplicar {"carrusel", "escena"}`: **é o passo que quase sempre falta**. Enquanto o carrossel não estiver NUMA cena, `laminas_ir` move o ponteiro e nada aparece.
- `laminas_desarmar {"archivo", "nombre"}`: converte um PDF ou um .pptx em slides. **Demora** (segundos por página) e não espera: avisa que começou e, quando termina, aparece em `carruseles_listar`. Um .pptx passa pelo DeckCraft (viaja com o programa); o .ppt antigo e o .odp, exportados para .pptx ou PDF.

## O que se procura e como
O operador escreve "barras" numa caixa e aparece esse slide. Isso funciona se alguém olhou as imagens antes: `laminas_sin_analizar {"carrusel"}` dá a próxima sem etiquetas, ela é olhada com `imagen_preguntar` (que é quem sabe olhar: o assistente principal NÃO recebe imagens), e o que se viu é salvo com `laminas_etiquetar {"carrusel", "numero", "etiquetas"}`. Faz-se **uma vez por slide**; se saiu vazio, fica marcado mesmo assim, para não olhar de novo para sempre.

As etiquetas são palavras com que alguém com pressa procuraria: o que se vê, o que diz, do que trata. "gráfico barras vendas trimestre", não uma descrição.