**Никогда не отвечайте «это невозможно» из-за редкого устройства или запроса, который не покрывают инструменты: напишите плагин.** Плагин — это программа на Python, которая работает в собственном процессе (если она падает, эфир продолжается, а она сама перезапускается) и добавляет в систему то, что нужно. Пишете его вы; оператор не программирует.

**Где они лежат**: каждый плагин — это папка в `Datos/Plugins/<id>/` (`plugin_listar` даёт путь в `carpeta`): `plugin.json` (манифест), `plugin.py` (код), `_libs/` (его библиотеки из pip), `datos/` (то, что он сохраняет) и его журнал в `Logs/plugins/<id>.log`. Они живут в данных, а не в программе: переживают обновления. Примеры идут с программой (`plugin_listar` → `ejemplos`); в интерфейсе — страница **«Плагины»** (боковая панель) → «Новый из примера». Не пишите файлы в эту папку вручную: используйте `plugin_escribir` (проверяет id и пути).

Типичные случаи:
- USB-камера с PTZ, который «работает только с приложением производителя» (Logitech Rally, Jabra, Huddly, OBSBOT…): она видна как веб-камера (UVC), а PTZ идёт через стандартные элементы управления UVC, через собственный **блок расширения UVC** (XU) или через HID.
- Камеры 360 / «рыбий глаз» (Kandao, Insta360, Ricoh): одно или два изображения по 180°; их нужно разрезать, развернуть или сшить, а иногда они несут данные (откуда говорит человек).
- Несколько устройств, работающих вместе (три камеры зала, которые определяют, кто говорит): по плагину на устройство публикует события, а другой плагин-«режиссёр» собирает их и управляет студией.
- Любое устройство с собственным API (HTTP, WebSocket, UDP, последовательный порт): пульты, tally, счётчики, датчики.
- Обработка видео или звука на Python (numpy, OpenCV, **GStreamer**): карточка графа. Эффект, фильтр или анализ видео, которого нет в `video_modulos`: см. «Видео» ниже.
- Автоматизация («когда происходит X, сделай Y») того, что не покрывают триггеры (`alert_types`): слушать события студии и вызывать инструменты.
- Анимированная графика по заказу, которой нет в репертуаре (с логикой, внешними данными или покадровым рисованием): `@plugin.grafico` (`help {"topic": "graficos"}`, пример `ejemplo-grafico`).
- ДОКУМЕНТ, УДОСТОВЕРЯЮЩИЙ ЛИЧНОСТЬ, который не является аргентинским DNI (водительские права, документ другой страны, пропуск с QR): `@plugin.documento` (ниже, пример `documento-aamva`). Терминал регистрации предлагает его в своих полях `documento`. Удостоверение с зоной MRZ на обороте (ИКАО TD1) уже читает встроенный `mrz-td1`; плагину также приходят строки оборота (формат «MRZ», три строки через перевод строки, уже проверенные), если его `formatos` включают его или пусты.

## Путь
1. **Что есть**: `dispositivos_listar` (камеры и микрофоны с готовым для pipeline `gst`, а также USB/HID/последовательные с vid:pid). Если вы не знаете, как говорить с устройством: найдите протокол (WebSearch, если он у вас есть) и/или напишите исследовательский плагин (ниже).
2. **Начните с шаблона**: `plugin_listar` (`ejemplos` с их `categoria`) → `plugin_ver {"id": "<ejemplo>"}` и, если он похож на то, что нужно сделать, `plugin_desde_ejemplo {"ejemplo", "id"}`, и измените что нужно. Шаблоны: `ejemplo-video-gst` (эффект, фильтр или анализ видео с GStreamer), `ejemplo-automatizacion` (слушать студию и действовать), `ejemplo-equipo-red` (устройство с текстовым протоколом по TCP, с симулятором), `ejemplo-basico` (всё, что может плагин: карточка, аудиофильтр, вход, панель, элемент управления). Для устройств: `ejemplo-ptz-uvc`, `ejemplo-360`, `ejemplo-director` и готовые к использованию (VISCA, TSL, Stream Deck, MIDI, OSC, Bosch DCN).
3. **Напишите его**: `plugin_escribir {"id", "manifiesto": {"nombre", "descripcion", "dependencias": [...], "config": {...}}, "archivos": {"plugin.py": "..."}}`.
4. **Проверьте, не трогая эфир**: `plugin_probar {"id"}` → отчёт (что объявлено, карточки с мс на кадр и что они изменили, входы с кадрами в секунду, элементы управления, события, журнал с ошибками Python). Если `ok` равно false или что-то не сходится, исправьте и проверьте снова. `controles: [{"nombre", "args"}]` вызывает элементы управления; `ptz: true` на мгновение двигает устройство.
5. **Активируйте**: `plugin_activar {"id"}`. Его входы появляются сами (сцена из манифеста или «Plugins»), его PTZ становится PTZ этого входа (`ptz_*` и пульт управления работают так же), его карточки ставятся через `plugin_tarjeta` (или в `video_pipeline` с `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}`) и настраиваются через `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"parametros": {"fuerza": 0.5}}}` (или отдельными параметрами: `{"fuerza": 0.5}`). Вход, который не в эфире, рисуется только когда на него кто-то смотрит: для проверки сделайте снимок дважды (первый может быть старым кадром).
6. **Проверьте в работе**: `video_estado {"entrada"}`, `imagen_preguntar`, `plugin_registro`, `plugin_control {"id"}` (информация о его медиа), `plugin_eventos`.
7. **Расскажите оператору**, что он делает и как им пользоваться. Если потом меняете код: `plugin_escribir` + `plugin_reiniciar`.

## SDK (`import ba_plugin as ba`)
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
Устройства (`from ba_plugin import dispositivos as d`; зависимости `pyusb`, `libusb-package`, `hidapi`, `pyserial`):
- `d.usb_listar()` → `[{"vid": "046d", "pid": "0881", "fabricante", "producto", "serie", "bus", "direccion", "uvc": true}]` (vid/pid в hex, текстом); `d.hid_listar()` → `[{vid, pid, producto, fabricante, uso, ruta}]`; `d.serie_listar()` → `[{puerto, descripcion, vid, pid, serie}]`.
- `uvc = d.UVC.abrir("046d", "0881")`: `uvc.info()` (терминал камеры, блоки расширения с их GUID, диапазоны), `uvc.mover(pan, tilt, zoom)` / `uvc.parar()` (стандартный UVC), `uvc.ir_a(pan=, tilt=, zoom=)` и собственное производителя: `uvc.unidades()`, `uvc.xu_explorar(unidad)` (какие селекторы отвечают), `uvc.xu_leer(unidad, selector)`, `uvc.xu_escribir(unidad, selector, bytes)`. Не забирает изображение у системы: двигается, пока изображение идёт.
- `h = d.hid_abrir(vid, pid)`: `h.write(bytes)`, `h.read(64, 100)`.

## Видео: эффект, фильтр или анализ, которого нет в графе
Сначала `video_modulos {"buscar": ...}`: если в GStreamer это уже есть, это карточка `gst`, и всё. Если нет (собственный эффект, необычная коррекция, подсчёт людей, обнаружение движения, чтение табло), **запрограммируйте это сами как карточку плагина**: это карточка «plugin» видеографа (модуль `baplugin`), она работает в цепочке входа, как любая другая, а код выполняется в процессе плагина (если он зависнет, изображение всё равно проходит, и эфир продолжается).

Внутри карточки можно использовать numpy и **весь GStreamer из Python** (`from ba_plugin import gst as bagst`; программа несёт привязки): цепочки модулей и **собственные элементы GStreamer, написанные на Python**. Минимум:
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
Без GStreamer тоже можно: `@plugin.tarjeta` и numpy поверх `cuadro.array()`. Анализ НЕ меняет изображение: измеряйте по `cuadro.array()[::8, ::8]` раз в несколько кадров, `plugin.mostrar` на его панели и `plugin.evento("movimiento", {...})` (с гистерезисом; никогда не событие на каждый кадр).

Полный путь: `plugin_desde_ejemplo {"ejemplo": "ejemplo-video-gst", "id": "mi-efecto"}` (или `plugin_escribir`) → `plugin_probar {"id"}` (мс на кадр каждой карточки при 320x180: в 1080p это примерно в 30 раз больше; должно оставаться заметно меньше одного кадра) → `plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (остаётся в графе как «Plugin · <tarjeta>»; на странице «Видео» она видна, двигается и настраивается, как остальные) → `video_tarjeta_ajustar {"id": "<id de la tarjeta>", "params": {"nivel": 90}}` → `foto_fuente`, чтобы посмотреть. Затраты в 1080p: проход через цепочку ~1 мс, `videobalance` ~8, `gaussianblur` ~35 (слишком много: уменьшите или выберите другой), numpy в uint16 и непрерывной памяти 1–3 мс (во float или с `a[..., :3]` по всему кадру — 20).

## Рецепты
- **Необычный PTZ**: вход `gst` с камерой (`dispositivos_listar` даёт `avfvideosrc device-index=N`) + `@plugin.ptz`, который говорит по UVC/XU/HID. Сначала попробуйте стандартный UVC (`uvc.mover`); если не двигается, исследуйте XU (элемент управления «explorar», который возвращает `uvc.info()` и `xu_explorar`) и найдите протокол этой модели.
- **Разрезать изображение на несколько входов** (два «рыбьих глаза» в одном кадре, 2x2 с регистратора): один вход и в его графе карточки `recorte` + `salida` (каждый выход — источник с именем). Не открывайте одно устройство дважды.
- **Развернуть «рыбий глаз» / сшить 360**: карточка плагина с OpenCV (`opencv-python-headless`): посчитайте карты ОДИН раз (`cv2.fisheye.initUndistortRectifyMap` или `cv2.remap` с собственными картами) и применяйте `cv2.remap` к каждому кадру. Если это тяжело, пусть вход приходит меньшего размера.
- **Несколько одинаковых устройств**: по плагину на устройство (скопируйте через `plugin_desde_ejemplo` или `plugin_escribir`) со своим `config` (серийный номер, IP) и своей `escena`; или один, который объявляет N входов.
- **Кто говорит / режиссёр**: каждый плагин устройства публикует `plugin.evento("hablando", {...})` (внимание: если запущен режиссёр, который слушает `*/hablando`, он тоже это получит; в тестах используйте другой канал или его `config.de`); плагин-режиссёр `@plugin.escuchar("*/hablando")` решает (минимальное время на каждом плане, не резать посреди фразы) и использует `plugin.llamar(...)`: `scene_switch`, композитор через `video_tarjeta_ajustar` (слои), `ptz_*`, чтобы навести другую камеру. Спросите оператора, хочет ли он автоматически или с подтверждением (`operator_ask`).
- **Звук устройства**: вход с `gst_audio` или `enviar_audio`; аудиокарточки на любом источнике через `plugin_tarjeta {"fuente"}`.
- **Автоматизация** (шаблон `ejemplo-automatizacion`): `@plugin.escuchar("aire/escena")` (темы: `eventos_temas`) и `plugin.llamar(...)`; укажите в plugin.json `"herramientas": [...]` — только то, что он может вызывать. Элемент управления «simular», который перебирает случаи, позволяет проверить его через `plugin_probar {"controles": [...]}` (вызовы системы выводятся в отчёт без выполнения).
- **Устройство по сети** (шаблон `ejemplo-equipo-red`): подключение в потоке `al_iniciar` с таймаутом и переподключением с растущей паузой, команды с ответом, то, что устройство шлёт само, — как `plugin.evento`, и встроенный симулятор устройства (`modo: simulador`), чтобы проверять без оборудования.

## Правила
- Ничто не блокирует навсегда: всё с таймаутом; циклы — в потоках `al_iniciar` и с проверкой `plugin.andando`.
- Карточки должны быть быстрыми (кадр длится 1000/fps мс холста: 33 мс при 30 fps, 16 мс при 60; и это последовательно с видео): векторизованный numpy; если она не успевает, выходит предыдущий кадр. Если плагину нужно только ИЗМЕРЯТЬ (цвет, яркость, кто есть), пусть измеряет раз в несколько кадров по уменьшенной версии (`a[::8, ::8]`) и применяет рассчитанное через карточку gst (`plugin.llamar("video_tarjeta_ajustar", ...)` на `videobalance`): обработка всего кадра на Python на каждой камере нагружает машину и подвешивает интерфейс.
- `print` идёт в журнал (`plugin_registro`); используйте `plugin.estado(...)` для того, что должен видеть оператор.
- **У любого плагина, которым должен управлять оператор, есть `plugin.panel`** (что настраивается: ползунки, переключатели, варианты; что делается: кнопки; что происходит: индикаторы и измерители). Люди пользуются системой так же, как вы: никаких «попросите меня в чате». `@plugin.control` — для того, что ассистент вызывает с аргументами (и он тоже появляется как кнопка).
- Библиотеки указываются в `dependencias` (pip, скачиваются в папку плагина: портативно, переживают обновления). **numpy уже идёт с программой** (объявить его ничего не стоит); при первой загрузке чего-то большого (opencv) это может занять пару минут.
- Ключи устройств (логин, пароль, токен API производителя): никогда через чат и не в `config`. Объявите их в plugin.json (`"secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]`), запросите через `plugin_credenciales {"id"}` (защищённая форма на панели), а в плагине — `plugin.secreto("clave")` (None, если их ещё нет: пусть ждёт и сообщает об этом через `plugin.estado`).
