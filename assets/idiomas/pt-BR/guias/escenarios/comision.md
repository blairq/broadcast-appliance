# Reunião de comissão

## Para quê
A comissão analisa os processos de uma ordem do dia e emite pareceres. Membros do colegiado,
assessores e convidados (servidores, moradores) que fazem exposições. Costuma ser pública e gravada.

## Perguntas próprias
- Qual comissão, quantos membros, quem preside? Há membros remotos?
- A ordem do dia? (Peça o texto: orden_del_dia_cargar aceita colá-lo.)
- Vêm convidados fazer exposições? Quanto tempo cada um? Trazem apresentação?
- O parecer é votado? É preciso quórum? Quórum de quantos?
- Vai ser transmitida? A documentação de cada processo é publicada?

## O que montar
- Ordem do dia carregada (orden_del_dia_cargar) e a documentação de cada processo relacionada
  ao seu item (documento_agregar com `puntos`).
- Lista de membros com os membros (vota: true) e os convidados com seu papel.
- Cenas: “Presidência”, “Membro com a palavra”, “Convidado expõe” (com sua apresentação),
  “Votação”, se houver votação; GC com nome e cargo de quem fala.
- Uso da palavra com tempos (speakers_config, debate_config se houver réplicas).
- Se houver votação: quórum (quorum_regla) e votação (votacion_abrir).

## Verificar
Quórum calculado com a lista de membros; os convidados remotos testados; os documentos na página web.

## Entregar
A página web do evento com ordem do dia e documentos; os convites; no final, a ata
(evento_acta) com o que foi tratado e votado.
