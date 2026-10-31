# Sessão de um órgão deliberativo

## Para quê
Uma sessão formal com membros nas suas cadeiras, presidência, secretaria, ordem do dia, uso da
palavra regulamentado, quórum e votações nominais com resultado oficial. Pública e transmitida.
Leia também help {"topic": "asamblea"} e o procedimento de sessão da assembleia.

## Perguntas próprias
- Qual órgão, quantas cadeiras, quem preside, quem é o secretário/a secretária?
- O Colegiado está carregado (membros, bancadas, cadeiras)? Se não: cuerpo_importar ou cuerpo_plantilla.
- Há unidade central de conferência (microfones de cadeira, p. ex. Bosch DCN)? Câmeras PTZ com posição
  por cadeira?
- Membros remotos (cadeira virtual)? Votam a distância?
- A ordem do dia e os processos. Maiorias especiais em algum item?
- Tempos de uso da palavra? Questões de ordem, réplicas?
- Transmissão, intérprete de língua de sinais (camada DSK), legendas?
- Televisores no plenário para mostrar a votação?

## O que montar
- Colegiado e plenário (recinto_editar) com cada cadeira e seu preset de câmera (ptz_posicion_crear).
- Cenas base da assembleia (asamblea_escenas_base): presidência, cadeira com a palavra, plano
  geral, votação, intervalo; revise com asamblea_revisar.
- Ordem do dia (orden_del_dia_cargar) com documentos por item.
- Quórum (quorum_regla) e votação (votacion_abrir / votacion_resultado_oficial).
- Se houver remotos: sesion_habilitar, credenciais (sesion_credencial) e teste de cada um.
- Telas do plenário com a votação (pantalla_mostrar contenido votacion).
- Superfície de controle da presidência ou dos microfones, se for operada de um tablet (modelos “Assembleia”,
  “Microfones das cadeiras”).

## Verificar
Unidade central conectada (dcn_conectar), câmeras nas suas posições, quórum com a lista de membros real, um remoto
votando para testar, a página web pública com a ordem do dia.

## Entregar
Credenciais para os remotos, a página web pública, a superfície de controle da presidência; no final, a ata com as
votações (evento_acta) e a gravação com capítulos por item (evento_capitulos).
