**É um CARTÃO do grafo de vídeo** (um filtro do GStreamer que amplia x2), em qualquer entrada: `superres_tarjeta {"entrada", "motor"}`. O que vem depois trabalha no dobro e a cena a recebe grande: sai com o dobro do que entra (uma SD de 720x576 sai em 1440x1152). Numa entrada de rede também dá para colocá-la na própria fonte (`superres_aplicar`), antes do grafo. O áudio não passa por ali; a sincronia se mantém enquanto o que ela demora couber no buffer da entrada (`latencia_ms`): se `superres.ms_por_cuadro` (video_estado) se aproximar do buffer, aumente-o (video_entrada_red `latencia_ms`).

## Motores (`superres_estado` diz quais existem)
| Motor | O que é | Custo | Quando |
|---|---|---|---|
| `apple` | A super-resolução da Apple (VideoToolbox, macOS 26), entrada até o máximo do sistema (hoje 960x960) | ~3 ms | Num Mac: a primeira opção |
| `fsr` | FSR 1 da AMD (EASU + RCAS), sem modelo, em OpenGL | <1 ms na GPU | Qualquer GPU (Windows, Linux) ou como base |
| `onnx` + modelo pequeno | Um modelo de luma treinado com A câmera (`superres_entrenar`) | ~14 ms com CoreML; ~65 ms só CPU | Quando a câmera tem compressão ou ruído próprios |
| `onnx` + SPAN | SPAN x2 (Apache), fiel | ~140 ms num Mac | Testes / GPU potente |
| `onnx` + Real-ESRGAN | Compacto x4 (BSD), limpa muito | ~290 ms num Mac | Não dá ao vivo aqui; para comparar |

## Receitas
- Uma câmera antiga num Mac: `superres_tarjeta {"entrada": "Cámara 1", "motor": "apple"}`.
- Testar FSR: `superres_tarjeta {"entrada", "motor": "fsr"}`.
- Um modelo numa câmera de tamanho incomum (o CoreML amplia do jeito comum e avisa em `error`): `superres_variante_crear {"plugin", "faltantes": true}` compila o modelo para o tamanho de cada câmera que não tem o seu (sem PyTorch; superres_estado mostra o andamento); `superres_variantes {"plugin"}` as lista com sua ficha (para quê, tamanhos, ms medidos).
- Treinar com um botão: `superres_entrenar {"entrada": "Cámara 1", "minutos": 5}` (grava ~1 minuto da câmera sem mexer no ar, treina e aplica; diz quanto melhora em dB contra a ampliação comum). Na primeira vez baixa o laboratório (PyTorch, ~800 MB, uma vez). Treinar de novo continua a partir do modelo anterior.
- SPAN ou Real-ESRGAN: `superres_modelo {"cual": "span"}` e depois `superres_tarjeta {"entrada", "motor": "onnx", "modelo": "span-x2.onnx"}`.
- Removê-la: `superres_aplicar {"entrada", "motor": ""}`.
- Depois de aplicar, após alguns segundos `video_estado`: o `tamaño` novo e `superres.ms_por_cuadro`. Se passar de ~3/4 do que dura um quadro do canvas (~25 ms a 30 fps, ~12 ms a 60), a entrada perde quadros: use um motor mais leve.