## De fora para dentro
1. `tiempos_mapa`: de cada entrada a cada saída, quanto cada nó adiciona (entrada, cartão, módulo de áudio, cena, mixagem, saída) e o acumulado. `salidas[].camino` é o caminho mais lento: começa-se por ele.
2. `tiempos_pipeline {"entrada"}`: a entrada suspeita por dentro, elemento por elemento (alguns segundos de sondas; não mexe no ar). Leia primeiro `hallazgos`: já vêm analisados, por gravidade, com o cartão culpado.
3. `tiempos_plugin {"entrada", "tarjeta"}`: o cartão culpado, como trabalha (suas memórias, propriedades, documentação) e o que faz nesta cadeia. `tiempos_plugin {"elemento"}` das alternativas para comparar (`interior.gpu`).
4. Se você mudou algo, `tiempos_pipeline` de novo e compare. Para ver ao longo do tempo: `tiempos_evento_agregar` (uma nota) e `tiempos_evento_impacto`.

## Como ler o que foi medido
- `agrega_ms` = idade do quadro na saída do elemento menos na entrada. Conta o que ele DEMORA e o que ele RETÉM (um filtro temporal que guarda quadros adiciona mesmo processando rápido). O `ms_por_cuadro` que um plugin informa é só o que ele demora.
- `memoria_*` é o que ele negociou; `puede_*` o que ele sabe usar. Uma travessia (`baja` da GPU para a RAM, `sube` da RAM para a GPU) copia o quadro inteiro: em 1080p são ~8 MB por quadro, mais CPU e largura de banda de memória. Uma **ida e volta** (desce e volta a subir) quase sempre é um plugin que só trabalha em RAM entre dois que trabalham na GPU.
- Se um plugin só aceita “sistema”: não tem versão GPU; procure outro (video_modulos, as variantes gl/d3d12) ou coloque-o no início ou no fim da cadeia, onde o vídeo já está em RAM, para não parti-la.
- Se `puede` GPU mas negociou RAM: o que está ao lado o obrigou (outro plugin só-RAM colado, ou um formato que a GPU não suporta).
- `declara_ms` é o que o elemento diz ao pipeline (com isso o estúdio compensa). `declara_menos`: demora mais do que declara; se a diferença se aproximar do buffer da entrada, os quadros chegam atrasados à mixagem → aumentar o buffer (`latencia_ms` da entrada) ou trocar o plugin.
- `conversion_cpu`: videoconvert/videoscale em RAM. Muitos ms em 1080p = convém que o anterior entregue o formato que o seguinte pede.
- Uma fila com `nivel_cola_ms` alto está guardando vídeo: algo depois está lento ou esperando.

## Resolução: que cada entrada entregue o que se vê
`resolucion_plan {"entrada"}` cruza onde a entrada é vista (seu maior tamanho nas cenas) com cada cartão e a câmera: a super-resolução que amplia algo que já cobre o que se vê, o PTZ depois da super-resolução, o zoom digital que estica poucos pixels, a câmera que entrega a mais ou a menos (com o modo que convém), os poucos fps. Os mesmos achados colorem os cartões (amarelo / laranja / vermelho) em Vídeo e em Latências. Os modos reais de cada câmera (bruto, MJPEG, H.264 e quem decodifica): `video_devices {"device"}`; tudo o que ela anuncia: `dispositivos_video`.

## Conte ao operador
Em português e com a consequência: “a dissolução por movimento retém 800 ms (24 quadros) e trabalha em RAM: obriga a descer e subir o vídeo da GPU”. Proponha a mudança e espere o OK antes de mexer num cartão no ar.