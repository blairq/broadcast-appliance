## A ideia
As entradas **sincronizadas** (classe `sala`: câmeras, placas de captura e microfones do local) saem alinhadas entre si e com o áudio: o estúdio espera a mais lenta. Por isso a latência do estúdio é definida por UMA entrada (a mais lenta das sincronizadas): para baixá-la é preciso melhorar essa. As de classe `propia` (convidados por WebRTC, sessão remota, janelas, telas compartilhadas) não arrastam as demais: se chegam atrasadas, o horário delas é deslocado, áudio e vídeo juntos.

A latência **não muda a cada corte de cena**: mudá-la ao vivo congela ou pula as saídas. Ao vivo ela só sobe (se pedirem por 2 s seguidos, uma vez, com aviso); baixa sozinha quando não há nada no ar, ou a pedido.

## Como se trabalha
1. `estudio_latencia`: a latência fixada, `la_marca` e cada entrada com sua classe, o que pede e o que demora.
2. Se quem a define é algo que não precisa se alinhar com a sala (um convidado, uma janela, uma câmera que pega outra coisa): `sincronia_clase {"entrada", "clase": "propia"}` (fora do ar: a entrada é remontada).
3. Se quem a define é uma câmera da sala: `tiempos_pipeline {"entrada"}` mostra se é a captura (teste outro modo: `video_devices {"device"}`) ou um cartão.
4. Com o novo funcionando e nada no ar, baixa sozinha; se não, `estudio_latencia {"bajar": true}` (com algo no ar há um salto: avise o operador).
5. Olhe `cuadros_tarde_60s` em estudio_latencia: tem que dar 0. Cada entrada pede sua idade p90 + o que oscila + 5 ms, e o compositor de vídeo soma só 5 ms (`video.margenComposicionMs`); se aparecerem quadros atrasados, essa entrada oscila mais do que pediu.
6. Para comprovar que duas câmeras ficaram alinhadas: um relógio (o de um telefone, com milissegundos) diante das duas e `video_sincronia {"a", "b"}`: a defasagem tem que dar menos de um quadro.

## Conte ao operador
Com números: “a latência do estúdio é 95 ms e quem a define é a Câmera 2 (captura 75 ms); o convidado vai com seu próprio atraso, 40 ms a mais”.