## O que é
Um estúdio de transmissão completo num só app:
- **Programa**: o que vai ao ar. É a **cena no ar** (`scene_switch` a troca, com transição).
- **Cenas**: composições de **fontes** (câmeras, imagens, textos, vídeos, convidados, cores...). Uma fonte pode estar em várias cenas (é a mesma).
- **Saídas**: transmissão RTMP (`stream_start`) e gravação (`record_start`).
- **Conferência**: os convidados entram pelo navegador (WebRTC). Primeiro esperam numa **sala de espera** privada (só veem você); ao serem admitidos ocupam um **lugar** (slot 1..N) e a câmera deles é a fonte `Invitado N`. Com o **modo oradores**, em vez de entrar direto eles fazem fila e entram no ar por turno, com um relógio.

## Primeira olhada (recomendado)
1. `status` — o que está no ar, se está transmitindo ou gravando, saúde.
2. `foto_programa` — ver o programa.
3. `scene_list` / `foto_todas_escenas` — todas as cenas.
4. `room_status` — quem está na conferência.

## Convenções
- Posições em **frações do canvas** (0..1), ou presets (`full`, `pip-br`, `grid-4-2`...). Ver o tema `fuentes`.
- Volume em **dB** (0 = sem mudança). Tempos de oradores em segundos ou `m:ss`.
- As ferramentas salvam a configuração sozinhas.
- Os erros explicam o que aconteceu e como seguir: leia-os e corrija.