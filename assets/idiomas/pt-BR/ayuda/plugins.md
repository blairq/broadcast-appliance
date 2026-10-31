**Nunca responda “não dá” por causa de um equipamento incomum ou de um pedido que as ferramentas não cobrem: escreva um plugin.** Um plugin é um programa em Python que roda no seu próprio processo (se cair, a transmissão continua e ele reinicia sozinho) e adiciona ao sistema o que for preciso. Quem o escreve é você; o operador não programa.

**Onde ficam**: cada plugin é uma pasta em `Datos/Plugins/<id>/` (`plugin_listar` dá o caminho em `carpeta`): `plugin.json` (manifesto), `plugin.py` (o código), `_libs/` (suas bibliotecas do pip), `datos/` (o que ele guardar) e seu log em `Logs/plugins/<id>.log`. Vivem nos dados, não no programa: sobrevivem às atualizações. Os exemplos vêm com o programa (`plugin_listar` → `ejemplos`); na interface, página **Plugins** (trilho) → “Novo a partir de um exemplo”. Não escreva arquivos à mão nessa pasta: use `plugin_escribir` (valida o id e os caminhos).

Casos típicos:
- Uma câmera USB com um PTZ que "só funciona com o app da marca" (Logitech Rally, Jabra, Huddly, OBSBOT…): aparece como uma webcam (UVC) e o PTZ vai por controles UVC padrão, por uma **unidade de extensão UVC** (XU) própria ou por HID.
- Câmeras 360 / olho de peixe (Kandao, Insta360, Ricoh): uma ou duas imagens de 180°; é preciso parti-las, desdobrá-las ou costurá-las, e às vezes trazem dados (para onde alguém está falando).
- Vários equipamentos que trabalham juntos (três câmeras de sala que detectam quem fala): um plugin por equipamento publica eventos e outro plugin "diretor" os junta e controla o estúdio.
- Qualquer equipamento com uma API própria (HTTP, WebSocket, UDP, serial): superfícies de controle, tally, contadores, sensores.
- Processar vídeo ou áudio em Python (numpy, OpenCV, **GStreamer**): um cartão do grafo. Um efeito, um filtro ou uma análise de vídeo que não está em `video_modulos`: ver «Video» abaixo.
- Automatizar (“quando acontecer X, faça Y”) o que os gatilhos (`alert_types`) não cobrem: escutar eventos do estúdio e chamar ferramentas.
- Um gráfico animado sob encomenda que o repertório não tem (com lógica, dados de fora ou desenhado quadro a quadro): `@plugin.grafico` (`help {"topic": "graficos"}`, exemplo `ejemplo-grafico`).
- Um DOCUMENTO DE IDENTIDADE que não é o DNI argentino (uma carteira de motorista, o documento de outro país, uma credencial com QR): `@plugin.documento` (abaixo, exemplo `documento-aamva`). O totem o oferece nos seus campos `documento`. Uma cédula com zona MRZ no verso (OACI TD1) já é lida por `mrz-td1` por padrão; a um plugin chegam também as linhas de um verso (formato «MRZ», três linhas separadas por quebra de linha, já validadas) se os seus `formatos` o incluírem ou estiverem vazios.

## O caminho
1. **O que existe**: `dispositivos_listar` (câmeras e microfones com seu `gst` pronto para um pipeline, e USB/HID/serial com vid:pid). Se você não sabe como se fala com o equipamento: procure o protocolo (WebSearch, se tiver) e/ou escreva um plugin de exploração (abaixo).
2. **Comece de um molde**: `plugin_listar` (os `ejemplos` com sua `categoria`) → `plugin_ver {"id": "<ejemplo>"}` e, se for parecido com o que é preciso fazer, `plugin_desde_ejemplo {"ejemplo", "id"}` e mude o que for preciso. Os moldes: `ejemplo-video-gst` (efeito, filtro ou análise de vídeo com GStreamer), `ejemplo-automatizacion` (escutar o estúdio e agir), `ejemplo-equipo-red` (um aparelho com protocolo de texto por TCP, com simulador), `ejemplo-basico` (tudo o que um plugin pode: cartão, filtro de áudio, entrada, painel, controle). Para equipamentos: `ejemplo-ptz-uvc`, `ejemplo-360`, `ejemplo-director`, e os prontos para usar (VISCA, TSL, Stream Deck, MIDI, OSC, Bosch DCN).
3. **Escreva-o**: `plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`.
4. **Teste sem mexer no ar**: `plugin_probar {"id"}` → relatório (o declarado, cartões com ms por quadro e o que mudaram, entradas com quadros por segundo, controles, eventos, log com os erros de Python). Se `ok` for false ou algo não fechar, corrija e teste de novo. `controles: [{"nombre", "args"}]` chama controles; `ptz: true` move o equipamento por um instante.
5. **Ative-o**: `plugin_activar {"id"}`. Suas entradas aparecem sozinhas (cena do manifesto ou “Plugins”), seu PTZ fica como o dessa entrada (`ptz_*` e a superfície de controle funcionam igual), seus cartões são colocados com `plugin_tarjeta` (ou em `video_pipeline` com `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`) e são ajustados com `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"parametros": {"fuerza": 0.5}}}` (ou os parâmetros avulsos: `{"fuerza": 0.5}`). Uma entrada que não está no ar só é desenhada quando alguém olha para ela: para verificar, capture duas vezes (a primeira pode ser um quadro antigo).
6. **Verifique ao vivo**: `video_estado {"entrada"}`, `imagen_preguntar`, `plugin_registro`, `plugin_control {"id"}` (info das suas mídias), `plugin_eventos`.
7. **Conte ao operador** o que ele faz e como se usa. Se depois você mudar o código: `plugin_escribir` + `plugin_reiniciar`.

## O SDK (`import ba_plugin as ba`)
```python
import ba_plugin as ba
plugin = ba.Plugin()                     # plugin.config (dict de plugin.json), plugin.datos (carpeta propia)

# ENTRADAS (video y/o audio). a) un pipeline de GStreamer que corre la app (lo más barato, sin copias):
plugin.entrada("Rally", gst="avfvideosrc device-index=1", escena="Sala")          # +gst_audio="osxaudiosrc ..."
# b) cuadros hechos en Python (numpy RGBA (alto, ancho, 4) uint8; audio float32 (n, canales)):
e = plugin.entrada("Pelota", video=True, audio=True)
e.enviar_video(arr, fps=30); e.enviar_audio(muestras, tasa=48000, canales=2)    # en un hilo de @plugin.al_iniciar

# TARJETAS: se cambia el cuadro EN EL LUGAR (memoria compartida); rápido (numpy vectorizado, nada de bucles por píxel)
@plugin.tarjeta("negativo", descripcion="...", parametros={"fuerza": 1.0})
def negativo(cuadro, params):            # cuadro.array() -> (alto, ancho, 4) RGBA del tamaño del lienzo
    a = cuadro.array(); a[..., :3] = 255 - a[..., :3]
@plugin.tarjeta("ganancia", medio="audio", parametros={"db": -6})
def ganancia(bloque, params): bloque.array()[:] *= 10 ** (params["db"] / 20)   # (muestras, canales) float32

# PTZ de una entrada (de este plugin o una que ya exista): velocidades -1..1
@plugin.ptz("Rally")
class Ptz:
    capacidades = {"panTilt": True, "zoom": True, "home": True, "presets": 8}
    def mover(self, pan, tilt, zoom): ...
    def parar(self): ...
    def home(self): ...
    def ir_a_preset(self, n): ...; def guardar_preset(self, n): ...   # también enfocar(v), auto_foco(b)

@plugin.control("modo", descripcion="...", parametros={"modo": "auto|manual"})   # plugin_control
def modo(args): return {"ok": True}

# GRÁFICOS ANIMADOS (help {"topic": "graficos"}): aparecen en Gráficos y Rótulos como plugin-<id>-<nombre>
@plugin.grafico("marcador", ancho=1000, alto=420, parametros=[{"clave": "puntos", "tipo": "datos", "porDefecto": "Sí: 3"}])
def marcador(valores): return {"declarativo": {...}}          # o {"qml": "..."}; o def f(lienzo, ms, valores) con numpy

# PANEL: la interfaz del operador (la dibuja la app; plugin_panel la maneja igual). Los valores se guardan solos.
plugin.panel([
    ba.seccion("Seguimiento"),
    ba.interruptor("seguir", "Seguir al que habla", defecto=True),
    ba.deslizador("suavidad", "Suavidad", 0, 1, defecto=0.5),
    ba.opcion("modo", "Modo", ["auto", "manual"]),
    ba.boton("calibrar", "Calibrar", al_tocar=calibrar),        # también texto(), numero()
    ba.indicador("caras", "Caras vistas"), ba.medidor("confianza", "Confianza", 0, 1),
])
plugin.valores["suavidad"]                                      # lo que puso el operador
@plugin.al_cambiar("modo")
def al_modo(valor): ...
plugin.mostrar("caras", 3)                                      # indicadores en vivo (se puede llamar en cada cuadro)

plugin.evento("hablando", {"angulo": 37, "persona": 2})        # canal de datos (con hora)
@plugin.escuchar("*/hablando")                                  # "kandao-1/hablando", "*/x", "id/*" (los propios también llegan)
def al_hablar(datos, de): ...

plugin.llamar("scene_switch", {"name": "Exterior"})             # CUALQUIER herramienta del sistema (las tuyas)

# DOCUMENTOS DE IDENTIDAD (docs/funciones/DOCUMENTOS.md): la app lee el código de barras de la cámara (zxing) y,
# si no es un tipo de fábrica (dni-ar), te pasa {formato, texto, bytes}. Devolvés los datos normalizados o None
# (no es tuyo). Queda como el tipo «plugin:<id>/licencia» (documento_tipos; se elige en el campo del tótem).
@plugin.documento("licencia", titulo="Licencia de conducir (EE.UU.)", formatos=["PDF417"], pais="US",
                  como_se_dice="tu licencia", ejemplos=[{"formato": "PDF417", "texto": "@\n...ANSI 636000..."}])
def licencia(codigo):                    # codigo["texto"] (str), codigo["bytes"] (bytes), codigo["formato"]
    if "ANSI " not in codigo["texto"]: return None
    return {"numero": "...", "apellido": "...", "nombres": "...", "nacimiento": "1990-03-15",   # fechas en ISO
            "vencimiento": "...", "sexo": "F", "extra": {"estado": "CA"}}   # sin `numero` = no lo leyó
# plugin_probar corre los `ejemplos` (SINTÉTICOS: nunca datos de una persona real) y muestra qué devolvió.
# Los datos son sensibles: no los mandes a ningún servicio de afuera ni los imprimas enteros en el registro.
plugin.aviso("texto", "warning"); plugin.estado("conectado a la Rally"); plugin.secreto("clave")  # claves cifradas
@plugin.al_iniciar / @plugin.al_terminar / @plugin.cada(segundos) / @plugin.al_cambiar_config
plugin.correr()                                                 # al final
```
Equipamentos (`from ba_plugin import dispositivos as d`; dependências `pyusb`, `libusb-package`, `hidapi`, `pyserial`):
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]` (vid/pid em hex, texto); `d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]`; `d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`.
- `uvc = d.UVC.abrir("046d", "0881")`: `uvc.info()` (terminal de câmera, unidades de extensão com seu GUID, faixas), `uvc.mover(pan, tilt, zoom)` / `uvc.parar()` (UVC padrão), `uvc.ir_a(pan=, tilt=, zoom=)`, e o que é próprio da marca: `uvc.unidades()`, `uvc.xu_explorar(unidad)` (quais seletores respondem), `uvc.xu_leer(unidad, selector)`, `uvc.xu_escribir(unidad, selector, bytes)`. Não tira a imagem do sistema: move-se enquanto é vista.
- `h = d.hid_abrir(vid, pid)`: `h.write(bytes)`, `h.read(64, 100)`.

## Vídeo: um efeito, um filtro ou uma análise que o grafo não traz
Primeiro `video_modulos {"buscar": ...}`: se o GStreamer já o tem, é um cartão `gst` e pronto. Se não (um efeito próprio, uma correção incomum, contar pessoas, detectar movimento, ler um placar), **programe-o você mesmo como um cartão de plugin**: é o cartão “plugin” do grafo de vídeo (o módulo `baplugin`), roda na cadeia da entrada como qualquer outro, e o código roda no processo do plugin (se travar, a imagem passa igual e a transmissão continua).

Dentro do cartão você pode usar numpy e **o GStreamer inteiro a partir do Python** (`from ba_plugin import gst as bagst`; o programa traz os bindings): cadeias de módulos e **elementos do GStreamer próprios escritos em Python**. Mínimo:
```python
import ba_plugin as ba
from ba_plugin import gst as bagst
plugin = ba.Plugin()

@bagst.elemento("baumbral", propiedades={"nivel": 128})     # un GstBase.BaseTransform de verdad
def umbral(a, props):                                          # a: (alto, ancho, 4) RGBA, sin copiar
    gris = a[..., :3].mean(axis=2) > props["nivel"]
    a[..., :3] = (gris * 255).astype("uint8")[..., None]

@plugin.tarjeta("umbral", parametros={"nivel": 128})
def tarjeta(cuadro, params):
    c = bagst.cadena("baumbral name=u ! videobalance contrast=1.2")   # una por tarjeta puesta; se arma una vez
    c.procesar(cuadro)                                                 # el resultado queda en el cuadro
    c.poner("u", nivel=int(params["nivel"]))                           # cambiar en vivo sin rearmar

plugin.correr()
```
Sem GStreamer também vale: `@plugin.tarjeta` e numpy sobre `cuadro.array()`. Uma análise NÃO muda a imagem: mede sobre `cuadro.array()[::8, ::8]` a cada poucos quadros, `plugin.mostrar` no seu painel e `plugin.evento("movimiento", {...})` (com histerese; nunca um evento por quadro).

O caminho completo: `plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}` (ou `plugin_escribir`) → `plugin_probar {"id"}` (ms por quadro de cada cartão, a 320x180: em 1080p são ~30 vezes mais; tem que ficar bem abaixo de um quadro) → `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (fica no grafo como «Plugin · <tarjeta>»; na página Vídeo aparece, é movido e ajustado como os demais) → `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"nivel": 90}}` → `foto_fuente` para ver. Custos em 1080p: passar por uma cadeia ~1 ms, `videobalance` ~8, `gaussianblur` ~35 (demais: reduza ou escolha outro), numpy em uint16 e contíguo 1-3 ms (em float ou com `a[..., :3]` sobre o quadro inteiro, 20).

## Receitas
- **PTZ incomum**: entrada `gst` com a câmera (`dispositivos_listar` dá o `avfvideosrc device-index=N`) + `@plugin.ptz` que fala UVC/XU/HID. Primeiro teste UVC padrão (`uvc.mover`); se não se mexer, explore as XU (um controle "explorar" que devolva `uvc.info()` e `xu_explorar`) e procure o protocolo desse modelo.
- **Partir uma imagem em várias entradas** (duas olho de peixe num quadro, 2x2 de um gravador): uma só entrada e no grafo dela cartões `recorte` + `salida` (cada saída é uma fonte com nome). Não abra o mesmo dispositivo duas vezes.
- **Desdobrar um olho de peixe / costurar 360**: cartão de plugin com OpenCV (`opencv-python-headless`): calcule os mapas UMA vez (`cv2.fisheye.initUndistortRectifyMap` ou `cv2.remap` com mapas próprios) e aplique `cv2.remap` por quadro. Se ficar pesado, que a entrada venha menor.
- **Vários equipamentos iguais**: um plugin por equipamento (copie com `plugin_desde_ejemplo` ou `plugin_escribir`) com sua `config` (número de série, IP) e sua `escena`; ou um que declare N entradas.
- **Quem fala / diretor**: cada plugin de equipamento publica `plugin.evento("hablando", {...})` (atenção: se houver um diretor rodando que escuta `*/hablando`, ele também recebe; em testes use outro canal ou o `config.de` dele); um plugin diretor `@plugin.escuchar("*/hablando")` decide (tempo mínimo em cada plano, não cortar no meio da frase) e usa `plugin.llamar(...)`: `scene_switch`, o compositor com `video_tarjeta_ajustar` (camadas), `ptz_*` para apontar outra câmera. Pergunte ao operador se ele quer automático ou com confirmação (`operator_ask`).
- **Áudio de um equipamento**: entrada com `gst_audio` ou `enviar_audio`; cartões de áudio sobre qualquer fonte com `plugin_tarjeta {"fuente"}`.
- **Automação** (molde `ejemplo-automatizacion`): `@plugin.escuchar("aire/escena")` (os temas: `eventos_temas`) e `plugin.llamar(...)`; coloque `"herramientas": [...]` no plugin.json com o único que ele pode chamar. Um controle “simular” que percorra os casos permite testá-lo com `plugin_probar {"controles": [...]}` (as chamadas ao sistema saem no relatório sem serem executadas).
- **Um aparelho pela rede** (molde `ejemplo-equipo-red`): conexão numa thread de `al_iniciar` com timeout e reconexão com espera crescente, ordens com resposta, o que ele manda sozinho como `plugin.evento`, e um simulador do aparelho dentro (`modo: simulador`) para testá-lo sem o equipamento.

## Regras
- Nada bloqueia para sempre: tudo com timeout; os loops em threads de `al_iniciar` e olhando `plugin.andando`.
- Os cartões têm que ser rápidos (um quadro dura 1000/fps ms do canvas: 33 ms a 30 fps, 16 ms a 60; e é em série com o vídeo): numpy vetorizado; se demorar, sai o quadro anterior. Se o plugin só precisa MEDIR (cor, brilho, quem está), que meça a cada poucos quadros sobre uma versão reduzida (`a[::8, ::8]`) e aplique o que calcula com um cartão gst (`plugin.llamar("video_tarjeta_ajustar", ...)` sobre um `videobalance`): processar o quadro inteiro em Python em cada câmera carrega a máquina e trava a interface.
- `print` vai para o log (`plugin_registro`); use `plugin.estado(...)` para o que o operador tem que ver.
- **Todo plugin que o operador tenha que controlar leva `plugin.panel`** (o que se ajusta: sliders, interruptores, opções; o que se faz: botões; o que acontece: indicadores e medidores). Os humanos usam o sistema igual a você: nada de "me peça pelo chat". `@plugin.control` fica para o que o assistente pede com argumentos (e também aparece como botão).
- As bibliotecas vão em `dependencias` (pip, são baixadas para a pasta do plugin: portáteis, sobrevivem às atualizações). **O numpy já vem com o programa** (declará-lo não custa nada); na primeira vez que algo grande é baixado (opencv) pode demorar uns minutos.
- Senhas de equipamentos (usuário, senha, token da API da marca): nunca pelo chat nem em `config`. Declare-as no plugin.json (`"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]`), peça-as com `plugin_credenciales {"id"}` (formulário seguro na barra) e no plugin `plugin.secreto("clave")` (None se ainda não estiverem: que espere e diga isso com `plugin.estado`).