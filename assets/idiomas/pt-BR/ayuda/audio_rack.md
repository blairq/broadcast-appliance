## Como está montado
Cada fonte com áudio (microfones, capturas, vídeos) pode ter um **rack** tipo JACK: `in` (a fonte bruta) -> módulos LSP conectados com cabos -> `out` (o que segue para a mixagem). A pessoa o vê na aba **Áudio** (patchbay); `audio_show {"source"}` a abre nessa fonte.
- `audio_sources`: fontes com áudio e se têm rack. `audio_rack {"source"}`: nós, cabos e medidores.
- `audio_plantillas`: salvar um rack com nome e colocá-lo em outra fonte. Marcada como `porDefecto` para `ba_guest_audio`, os convidados NOVOS a recebem sozinhos ao se conectar: é a única forma de que soem bem sem montar nada para eles enquanto o evento começa.
- Cada módulo tem medidores próprios: `entradaDb`, `salidaDb` e `cambioDb` (o que baixa ou sobe AGORA: num gate/compressor é a redução de ganho).
- Um rack vazio não muda o som. `audio_module_remove {"node": "*"}` remove o rack inteiro.
- Na aba, cada cartão se acomoda numa grade (botão "Organizar" para alinhar tudo) e seu chevron abre o detalhe: nome real de cada porta do módulo e se é mono ou estéreo.

## Portas e canais (L/R)
Um módulo pode ter várias entradas e saídas: `in` (principal), `sc` (sidechain) e, se tiver mais, `in2`, `in3`... e `out`, `out2`... (`audio_rack` as informa em `entradas`/`salidas`, e em `puertosEntrada`/`puertosSalida` com seu nome real e quantos canais cada uma tem). Um mixer LSP de 16 entradas informa as 16.
- Por padrão um cabo leva a **porta inteira**: esquerda com esquerda e direita com direita.
- Dá para cabear **um canal avulso** adicionando `canalSalida` e `canalEntrada` ("L" ou "R") em `audio_patch`: p. ex. levar a direita de um módulo para a esquerda de outro, ou usar um só canal e deixar o outro em silêncio.
- Entre as mesmas duas portas não convivem um cabo de porta inteira e um de canal: o último substitui o anterior (senão o áudio seria somado duas vezes).

## Ouvir por uma saída do equipamento (fones)
Serve para revisar como ficou um ponto do rack SEM mexer no que vai ao ar nem na mixagem do programa.
1. `audio_outputs`: as saídas desta máquina, agrupadas por interface.
2. `audio_output_set {"source", "salida": <id>}`: adiciona ao grafo dessa fonte um cartão com o nome da saída.
3. `audio_patch {"source", "conectar": [{"desde": "in", "hacia": "salida-audio"}]}`: só aí se ouve (dá para ligar a saída de qualquer módulo, não só `in`).
ATENÇÃO: o motor reproduz por UMA saída de cada vez; escolher outra em outra fonte muda a de todas.

## Microfones e entradas de áudio do equipamento
Não aparecem sozinhas: é preciso adicioná-las. `audio_devices` as lista (`inputs`) e `audio_input_add {"device"}` a adiciona. Vai para um canal GLOBAL (como o áudio dos convidados): soa em TODAS as cenas, não é um item de cena e fica salva para a próxima inicialização. Depois é processada como qualquer fonte, com seu rack.

## Regra de ouro: medir, mudar, medir de novo
1. `audio_analyze {"source", "punto": "in", "segundos": 5}`: pico, RMS, LUFS, `pisoDeRuidoDbfs`, `nivelActivoDbfs`, `rangoDinamicoDb`, espectro por terços e por zonas (graves, corpo, médios, presença, sibilância, ar), clipagens e `pistas`. Se disser que não chega áudio, a fonte está muda, inativa ou não há ninguém falando: peça à pessoa que fale (`operator_ask`).
2. Mudar com as ferramentas com nome (valores em unidades reais: dB, ms, Hz).
3. Verificar: `audio_analyze {"punto": "out"}` e/ou `audio_measure` (cópias fora de linha, não mexe no ar).
Conte à pessoa o que você mediu e o que mudou, com os números.

## Calibrações típicas (voz)
- **Gate** (`audio_gate`): medir DUAS vezes, com a pessoa calada (piso de ruído) e falando (nível ativo). `umbral_db` = piso + 6 a 10 dB, e sempre bem abaixo do nível ativo. `reduccion_db` -20 a -40 (não é preciso mudo total), `ataque_ms` 1-5, `sosten_ms` 50-100, `liberacion_ms` 100-250. Verificar com `audio_measure {"tipo": "curva"}` (onde abre) e ouvindo que não corte finais de palavra. Se o piso e a voz estiverem a menos de ~15 dB, o gate não basta: sugerir aproximar o microfone ou baixar o ganho de entrada.
- **Compressor** (`audio_compressor`): preset `voz` e ajustar `umbral_db` para que `cambioDb` marque -3 a -6 dB nas partes fortes (não o tempo todo). `ratio` 2-4, `ataque_ms` 5-20, `liberacion_ms` 80-200, `ganancia_db` para recuperar o nível.
- **De-esser** (`audio_deesser`): olhar a zona `sibilancia` (5-9 kHz) da análise. `deteccion_hz` no pico dos "s" (5-7 kHz típico), `umbral_db` para que baixe 3-6 dB só nos "s". `escuchar: true` deixa ouvir o que ele detecta (só para ajustar: lembre-se de desligar).
- **EQ** (`audio_eq`, por `bandas`): passa-altas 70-100 Hz para voz; se a zona `cuerpo` (200-400 Hz) sobressair, sino -2 a -4 dB; se faltar clareza, +1 a +3 dB em 3-5 kHz. Mudanças pequenas. Verificar com `audio_measure {"tipo": "respuesta"}`.
- **Multibanda** (`audio_multiband`): quando um só compressor "bombeia" ou o problema está numa zona (graves que retumbam, agudos que disparam). `corte_hz` e limiares por banda.
- **Limitador** (`audio_limiter`, preset `seguridad`): no fim da cadeia, teto -1 dBFS. É rede de segurança, não volume.
- Ordem habitual: EQ passa-altas -> gate -> compressor -> de-esser -> EQ fino -> limitador. `audio_patch` reconecta; `audio_bypass` compara com/sem.

## Loudness
Para a transmissão, mire em -16 a -14 LUFS integrados na mixagem (YouTube/Twitch normalizam perto de -14). Uma voz sozinha: -18 a -16 LUFS em `out`. Os picos que tocam 0 dBFS (`recortes`) se corrigem no ganho de entrada, não com o limitador.

## Gerador e outros módulos
- `audio_generator {"senal": "rosa"|"seno"|"barrido"|"blanco", "nivel_db", "conectar_a"}` cria um sinal de teste AVULSO (p. ex. para um analisador de `audio_module_add {"module": "analizador"}`). Conectá-lo ao que vai ao ar exige `al_aire: true`: confirme antes com a pessoa.
- `audio_modules {"buscar"}` lista os ~200 plugins LSP; `audio_module_add` adiciona qualquer um e `audio_module_get`/`audio_module_set` leem e mudam todos os seus parâmetros.
- `audio_editor` abre a interface gráfica original do módulo LSP (para a pessoa).