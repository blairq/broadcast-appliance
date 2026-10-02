// ============================================================================
// ba-modelo — el modelo local en la Mac: MLX, con la cabeza MTP si la hay
//
//   ba-modelo --modelo <carpeta> [--mtp no|auto|fp16|q8] [--prueba]
//
// La carpeta es la del repo de Hugging Face tal cual (model.safetensors,
// tokenizer, prompt.json y, si vienen, mtp.safetensors / mtp-q8.safetensors).
//
// QUE HACE. Carga el modelo, lee UNA VEZ el prefijo de prompt.json (las fichas
// de las herramientas, ~2.800 tokens) y se queda esperando pedidos por stdin,
// una línea JSON cada uno; contesta por stdout, también una línea JSON. Entre
// pedido y pedido restaura una copia del estado que quedó después del prefijo:
// Qwen3.5 es híbrido y sus capas de atención lineal guardan un estado que no se
// puede «recortar» (igual que tools/entrenar/evaluar-mlx.py).
//
//   {"id":1,"op":"elegir","frase":"bajale al mic 2","sesgo":0}
//     -> {"id":1,"herramienta":"audio_set","etiqueta":"audio_set","prob":0.97,"ms":120}
//   {"id":2,"op":"generar","texto":"...","prefijo":true,"max":64,"mtp":true}
//     -> {"id":2,"delta":"..."} ... {"id":2,"fin":true,"texto":"...","tokens":n,...}
//   {"id":3,"op":"ver","imagen":"/ruta/foto.jpg","texto":"¿cuántas personas hay?","max":96}
//     -> igual que generar (si el modelo trae la torre de visión)
//   {"op":"salir"}
//
// ELEGIR es el contrato de entrenamiento: el prompt exacto (cabeza + frase +
// cola), decodificación restringida a las salidas posibles y el sesgo sumado
// al logit de `derivar` en el primer paso. GENERAR es texto corto: ahí es donde
// paga la cabeza MTP (decodificación especulativa con el borrador del propio
// modelo); con `prefijo` el texto va a continuación de la cabeza de prompt.json.
//
// Los errores salen como {"id":..,"error":"..."}; los avisos de carga como
// {"ev":"..."}. stderr es solo para diagnóstico.
// ============================================================================
import Foundation
import MLX
import MLXLLM
import MLXLMCommon
import MLXVLM
import Tokenizers

// MARK: - salida
//
// El canal JSON es una COPIA del stdout original; el descriptor 1 pasa a
// stderr. Así lo que las bibliotecas escriben con print() (swift-transformers
// avisa así) no se mezcla con el protocolo.

let canal: FileHandle = {
    let fd = dup(STDOUT_FILENO)
    dup2(STDERR_FILENO, STDOUT_FILENO)
    return FileHandle(fileDescriptor: fd, closeOnDealloc: false)
}()

func emitir(_ obj: [String: Any]) {
    guard let d = try? JSONSerialization.data(withJSONObject: obj, options: [.withoutEscapingSlashes]),
        var s = String(data: d, encoding: .utf8)
    else { return }
    s += "\n"
    canal.write(s.data(using: .utf8)!)
}

func morir(_ msg: String) -> Never {
    emitir(["ev": "error", "error": msg])
    exit(1)
}

func ms(_ desde: Date) -> Double { (Date().timeIntervalSince(desde) * 10000).rounded() / 10 }

// MARK: - tokenizer (swift-transformers adaptado a MLXLMCommon)

struct Puente: MLXLMCommon.Tokenizer {
    let up: any Tokenizers.Tokenizer
    func encode(text: String, addSpecialTokens: Bool) -> [Int] {
        up.encode(text: text, addSpecialTokens: addSpecialTokens)
    }
    func decode(tokenIds: [Int], skipSpecialTokens: Bool) -> String {
        up.decode(tokens: tokenIds, skipSpecialTokens: skipSpecialTokens)
    }
    func convertTokenToId(_ token: String) -> Int? { up.convertTokenToId(token) }
    func convertIdToToken(_ id: Int) -> String? { up.convertIdToToken(id) }
    var bosToken: String? { up.bosToken }
    var eosToken: String? { up.eosToken }
    var unknownToken: String? { up.unknownToken }
    func applyChatTemplate(
        messages: [[String: any Sendable]], tools: [[String: any Sendable]]?,
        additionalContext: [String: any Sendable]?
    ) throws -> [Int] {
        try up.applyChatTemplate(messages: messages, tools: tools, additionalContext: additionalContext)
    }
}

struct Cargador: TokenizerLoader {
    func load(from directory: URL) async throws -> any MLXLMCommon.Tokenizer {
        // strict: false porque swift-transformers no registra Qwen3_5Tokenizer;
        // es el BPE de Qwen2, que es a lo que cae. La cuenta de tokens del
        // prefijo contra huella-prompt.json lo controla al arrancar.
        Puente(up: try await AutoTokenizer.from(modelFolder: directory, strict: false))
    }
}

// MARK: - argumentos

var carpeta: String?
var modoMtp = "no" // la cabeza todavía no rinde (ver «si la cabeza MTP rinde»)
var prueba = false
var silencioso = false // sin los delta (la prueba solo quiere los tiempos)
var args = CommandLine.arguments.dropFirst().makeIterator()
while let a = args.next() {
    switch a {
    case "--modelo": carpeta = args.next()
    case "--mtp": modoMtp = args.next() ?? "no"
    case "--prueba": prueba = true
    default: morir("argumento desconocido: \(a)")
    }
}
guard let carpeta else { morir("uso: ba-modelo --modelo <carpeta> [--mtp no|auto|fp16|q8] [--prueba]") }
let dir = URL(fileURLWithPath: carpeta)

// MARK: - prompt.json: lo que hace falta para servir, sin reconstruir nada

struct Prompt {
    let cabeza: String
    let cola: String
    let fin: String
    let salidas: [String]
    let equivalencias: [String: String]
    let derivar: String
    let huella: String
    let pruebas: [(frase: String, herramienta: String)]
    /// "tc" (v2): tool calling nativo de Qwen3.5 (<tool_call><function=…>);
    /// "" (v1): la salida es el nombre de la herramienta, nada más.
    let formato: String
    let faltaDato: String
}

func leerPrompt() -> Prompt {
    guard let d = try? Data(contentsOf: dir.appending(component: "prompt.json")),
        let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
        let cabeza = j["cabeza"] as? String, let cola = j["cola"] as? String
    else { morir("la carpeta no tiene un prompt.json válido (cabeza, cola)") }
    let formato = j["formato"] as? String ?? ""
    // v1: «salidas»; v2 (tc): los nombres de «funciones» (con derivar y falta_dato).
    let salidas = (j["salidas"] as? [String]) ?? (j["funciones"] as? [String]) ?? []
    if salidas.isEmpty { morir("el prompt.json no trae salidas ni funciones") }
    let pruebas = (j["frases_prueba"] as? [[String: Any]] ?? []).compactMap { p -> (String, String)? in
        guard let f = p["frase"] as? String, let h = p["herramienta"] as? String else { return nil }
        return (f, h)
    }
    return Prompt(
        cabeza: cabeza, cola: cola, fin: j["fin"] as? String ?? "<|im_end|>", salidas: salidas,
        equivalencias: j["equivalencias"] as? [String: String] ?? [:],
        derivar: j["derivar"] as? String ?? "derivar", huella: j["huella"] as? String ?? "",
        pruebas: pruebas, formato: formato, faltaDato: j["falta_dato"] as? String ?? "falta_dato")
}

let prompt = leerPrompt()

// MARK: - la cabeza MTP: una carpeta de «drafter» al lado, armada desde el archivo
//
// mlx-swift-lm carga la cabeza como un modelo aparte (MTPDrafterModelFactory):
// una carpeta con config.json y los pesos mtp.*. La armamos dentro de la del
// modelo con un enlace al archivo, y la cuantización del config.json tiene
// que ser la DE LA CABEZA (fp16: ninguna; q8: 8 bits, grupos de 64), no la del
// modelo (4 bits): el cargador cuantiza según el config donde hay `.scales`.

func carpetaDrafter() -> URL? {
    let fm = FileManager.default
    let fp16 = dir.appending(component: "mtp.safetensors")
    let q8 = dir.appending(component: "mtp-q8.safetensors")
    let elegido: (URL, String)?
    switch modoMtp {
    case "no": elegido = nil
    case "fp16": elegido = fm.fileExists(atPath: fp16.path) ? (fp16, "fp16") : nil
    case "q8": elegido = fm.fileExists(atPath: q8.path) ? (q8, "q8") : nil
    default:
        // auto: q8 si está (menos memoria y ancho de banda por ronda), si no fp16
        elegido =
            fm.fileExists(atPath: q8.path) ? (q8, "q8")
            : fm.fileExists(atPath: fp16.path) ? (fp16, "fp16") : nil
    }
    guard let (archivo, variante) = elegido else { return nil }
    let d = dir.appending(component: ".mtp-\(variante)")
    try? fm.removeItem(at: d)
    do {
        try fm.createDirectory(at: d, withIntermediateDirectories: true)
        let cfgData = try Data(contentsOf: dir.appending(component: "config.json"))
        guard var cfg = try JSONSerialization.jsonObject(with: cfgData) as? [String: Any] else { return nil }
        cfg.removeValue(forKey: "quantization")
        cfg.removeValue(forKey: "quantization_config")
        if variante == "q8" {
            cfg["quantization"] = ["group_size": 64, "bits": 8, "mode": "affine"]
        }
        try JSONSerialization.data(withJSONObject: cfg).write(to: d.appending(component: "config.json"))
        try fm.createSymbolicLink(at: d.appending(component: "mtp.safetensors"), withDestinationURL: archivo)
        return d
    } catch {
        emitir(["ev": "aviso", "aviso": "no se pudo preparar la cabeza MTP: \(error)"])
        return nil
    }
}

// MARK: - carga

// Con la torre de visión (config.json con vision_config) se carga el modelo
// multimodal: el mismo modelo de lenguaje, más la visión y su procesador de
// imágenes. Sin ella, solo texto.
let conVision: Bool = {
    guard let d = try? Data(contentsOf: dir.appending(component: "config.json")),
        let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any]
    else { return false }
    return j["vision_config"] != nil
}()
let tCarga = Date()
let ctx: ModelContext
do {
    ctx =
        conVision
        ? try await VLMModelFactory.shared.load(from: dir, using: Cargador())
        : try await LLMModelFactory.shared.load(from: dir, using: Cargador())
} catch {
    morir("no se pudo cargar el modelo: \(error)")
}
let modelo = ctx.model
let tok = ctx.tokenizer

var drafter: (any MTPDrafterModel)?
var varianteMtp = "no"
if !conVision, let d = carpetaDrafter() {
    await Qwen35TextMTPRegistration.register()
    do {
        drafter = try await MTPDrafterModelFactory.shared.load(from: d, using: Cargador()).model
        varianteMtp = d.lastPathComponent.replacingOccurrences(of: ".mtp-", with: "")
    } catch {
        emitir(["ev": "aviso", "aviso": "la cabeza MTP no cargó, sigo sin ella: \(error)"])
    }
}
let msCarga = ms(tCarga)

func ids(_ s: String) -> [Int] { tok.encode(text: s, addSpecialTokens: false) }
func arr(_ v: [Int]) -> MLXArray { MLXArray(v.map { Int32($0) }) }

/// Los logits de la ÚLTIMA posición, después de pasar `tokens` por el modelo.
/// (La forma general, LMInput.Text -> LMOutput: el modelo multimodal no
/// implementa la llamada directa con un MLXArray.)
/// El `state` que devuelve cada llamada (en el multimodal: los «rope deltas»)
/// tiene que acompañar a la siguiente sobre la misma caché.
func adelante(_ tokens: [Int], _ cache: [KVCache], _ estado: inout LMOutput.State?) -> MLXArray {
    let r = modelo(LMInput.Text(tokens: arr(tokens))[text: .newAxis], cache: cache, state: estado)
    estado = r.state
    return r.logits[0, r.logits.dim(1) - 1].asType(.float32)
}

// MARK: - el prefijo, una vez

let tPrefijo = Date()
let idsCabeza = ids(prompt.cabeza)
// El prefijo ya calculado se guarda junto al modelo: calcularlo cuesta ~2,5 ms
// por token en una M1 Pro (la v2 tiene 8 mil: 22 s) y es siempre el mismo
// mientras no cambie el prompt. Se reconoce por la huella del prompt, la
// cantidad de tokens y el modo (con o sin visión, que cambian la caché).
let archivoPrefijo = dir.appending(
    component: ".prefijo-\(prompt.huella.isEmpty ? "sin-huella" : prompt.huella)-\(conVision ? "vision" : "texto").safetensors")
/// Los pesos: tamaño y fecha de los .safetensors del modelo. Un reentrenamiento
/// con el mismo prompt deja la misma huella; sin esto se cargaría un prefijo
/// calculado con los pesos viejos.
let firmaPesos: String = {
    let fm = FileManager.default
    let archivos = ((try? fm.contentsOfDirectory(atPath: dir.path)) ?? [])
        .filter { $0.hasSuffix(".safetensors") && !$0.hasPrefix(".") && !$0.hasPrefix("mtp") }.sorted()
    return archivos.map { nombre -> String in
        let a = try? fm.attributesOfItem(atPath: dir.appending(component: nombre).resolvingSymlinksInPath().path)
        let tam = (a?[.size] as? NSNumber)?.int64Value ?? 0
        let fecha = Int((a?[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0)
        return "\(nombre):\(tam):\(fecha)"
    }.joined(separator: ";")
}()
let datosPrefijo: [String: String] = [
    "huella": prompt.huella, "tokens": String(idsCabeza.count), "modo": conVision ? "vision" : "texto",
    "pesos": firmaPesos,
]
var prefijoDeDisco = false
var cachePrefijo: [KVCache] = []
var estadoPrefijo: LMOutput.State? = nil
if ProcessInfo.processInfo.environment["BA_PREFIJO_SIN_DISCO"] == nil,
    FileManager.default.fileExists(atPath: archivoPrefijo.path),
    let foto = try? loadPromptCacheSnapshot(url: archivoPrefijo),
    datosPrefijo.allSatisfy({ foto.metadata[$0.key] == $0.value }), !foto.cache.isEmpty
{
    cachePrefijo = foto.cache
    estadoPrefijo = foto.state
    eval(cachePrefijo)
    prefijoDeDisco = true
}
if !prefijoDeDisco {
    do {
        cachePrefijo = try modelo.newCache(parameters: nil)
    } catch {
        morir("no se pudo crear la caché: \(error)")
    }
    // De a pedazos (como mlx-lm): de una sola vez, las capas de atención
    // completa arman la máscara y las cuentas de N×N con N = toda la cabeza.
    // Medido (v2, 8203 tokens): de una vez 26,7 s; de a 512, 21,8 s.
    // BA_PREFIJO_PASO lo cambia (0 = de una vez) para medir.
    let pasoPrefijo: Int = Int(ProcessInfo.processInfo.environment["BA_PREFIJO_PASO"] ?? "") ?? 512
    if pasoPrefijo <= 0 {
        _ = adelante(idsCabeza, cachePrefijo, &estadoPrefijo)
        eval(cachePrefijo)
    } else {
        var desde = 0
        while desde < idsCabeza.count {
            let hasta = Swift.min(desde + pasoPrefijo, idsCabeza.count)
            _ = adelante(Array(idsCabeza[desde..<hasta]), cachePrefijo, &estadoPrefijo)
            eval(cachePrefijo)
            desde = hasta
        }
    }
    // Para el próximo arranque (si no se puede escribir, se recalcula y listo).
    do {
        try savePromptCache(url: archivoPrefijo, cache: cachePrefijo, metadata: datosPrefijo, state: estadoPrefijo)
    } catch {
        emitir(["ev": "aviso", "aviso": "no se pudo guardar el prefijo calculado: \(error)"])
    }
}
let msPrefijo = ms(tPrefijo)

func copiaDelPrefijo() -> [KVCache] { cachePrefijo.map { $0.copy() } }

// MARK: - elegir: decodificación restringida a las salidas, con el dial de derivar

/// Árbol de prefijos de tokens: qué token puede seguir y qué salidas quedan.
struct Arbol {
    var siguientes: [[Int]: [Int]] = [:]
    var quedan: [[Int]: Set<String>] = [:]
    var tokDerivar = -1
}

let arbol: Arbol = {
    var a = Arbol()
    for s in prompt.salidas {
        let sq = ids(s)
        for k in 0...sq.count {
            let p = Array(sq[0..<k])
            a.quedan[p, default: []].insert(s)
            if k < sq.count, !(a.siguientes[p]?.contains(sq[k]) ?? false) {
                a.siguientes[p, default: []].append(sq[k])
            }
        }
        if s == prompt.derivar { a.tokDerivar = sq.first ?? -1 }
    }
    return a
}()

func elegir(_ frase: String, sesgo: Float) -> (etiqueta: String, prob: Double) {
    let c = copiaDelPrefijo()
    var estado = estadoPrefijo
    var lg = adelante(ids(frase + prompt.cola), c, &estado)
    var pre: [Int] = []
    var prob = 1.0
    for paso in 0..<16 {
        guard let quedan = arbol.quedan[pre], quedan.count > 1, let ops = arbol.siguientes[pre] else { break }
        var val = lg[arr(ops)]
        if paso == 0, sesgo != 0, let i = ops.firstIndex(of: arbol.tokDerivar) {
            var bump = [Float](repeating: 0, count: ops.count)
            bump[i] = sesgo
            val = val + MLXArray(bump)
        }
        let p = softmax(val)
        let k = argMax(p).item(Int.self)
        prob *= Double(p[k].item(Float.self))
        pre.append(ops[k])
        if (arbol.quedan[pre]?.count ?? 0) <= 1 { break }
        lg = adelante([ops[k]], c, &estado)
    }
    return (arbol.quedan[pre]?.first ?? prompt.derivar, prob)
}

// MARK: - formato tc (v2): la llamada nativa de Qwen3.5
//
// La respuesta de un modelo v2 es una llamada <tool_call>\n<function=NOMBRE>
// con sus <parameter=…>, o texto (charla). El primer token dice cuál: si es
// <tool_call>, el comienzo se fuerza hasta un NOMBRE válido (el árbol de
// abajo, con el dial de derivar donde se separa) y el resto (los
// parámetros) se genera libre. Así se midió el banco de la v2 (91,6 %).

/// Las secuencias completas «<tool_call>\n<function=NOMBRE>» de cada salida.
let arbolTc: Arbol = {
    var a = Arbol()
    guard prompt.formato == "tc" else { return a }
    for s in prompt.salidas {
        let sq = ids("<tool_call>\n<function=" + s + ">")
        for k in 0...sq.count {
            let p = Array(sq[0..<k])
            a.quedan[p, default: []].insert(s)
            if k < sq.count, !(a.siguientes[p]?.contains(sq[k]) ?? false) {
                a.siguientes[p, default: []].append(sq[k])
            }
        }
    }
    return a
}()
let tokToolCall: Int = tok.convertTokenToId("<tool_call>") ?? -1

/// Recorre el árbol desde `lg` (los logits del próximo token): fuerza donde
/// hay un solo camino y elige el más probable donde se abre. Devuelve el
/// nombre, la probabilidad de las elecciones y los tokens (el último sin
/// pasar todavía por el modelo).
func caminarTc(_ lg0: MLXArray, _ c: [KVCache], _ estado: inout LMOutput.State?, sesgo: Float)
    -> (nombre: String, prob: Double, tokens: [Int])
{
    var lg = lg0
    var pre: [Int] = []
    var prob = 1.0
    while let ops = arbolTc.siguientes[pre], !ops.isEmpty {
        var elegido = ops[0]
        if ops.count > 1 {
            var val = lg[arr(ops)]
            if sesgo != 0, let i = ops.firstIndex(where: { (arbolTc.quedan[pre + [$0]] ?? []).contains(prompt.derivar) }) {
                var bump = [Float](repeating: 0, count: ops.count)
                bump[i] = sesgo
                val = val + MLXArray(bump)
            }
            let p = softmax(val)
            let k = argMax(p).item(Int.self)
            prob *= Double(p[k].item(Float.self))
            elegido = ops[k]
        }
        pre.append(elegido)
        if arbolTc.siguientes[pre] == nil { break } // nombre completo (el «>» incluido)
        lg = adelante([elegido], c, &estado)
    }
    let nombre = (arbolTc.quedan[pre] ?? []).first ?? prompt.derivar
    return (nombre, prob, pre)
}

/// Un pedido al modelo v2: estado (JSON compacto, o nada para charla) y frase.
/// Devuelve el texto entero (la llamada o la charla) y el tipo.
func llamar(id: Any, estadoJson: String?, frase: String, sesgo: Float, gramatica: Bool, max: Int, usarMtp: Bool)
    throws -> [String: Any]
{
    let t0 = Date()
    let usuario = (estadoJson ?? "").isEmpty ? frase : "Estado: \(estadoJson!)\nPedido: \(frase)"
    let c = copiaDelPrefijo()
    var est = estadoPrefijo
    let lg = adelante(ids(usuario + prompt.cola), c, &est)
    let primeroLibre = argMax(lg).item(Int.self)
    let pLlamada = tokToolCall >= 0 ? Double(softmax(lg)[tokToolCall].item(Float.self)) : 0
    // ¿Llamada o charla? Lo dice el primer token (con gramática, alcanza con
    // que <tool_call> sea lo más probable o pase de 0,5).
    let esLlamada = primeroLibre == tokToolCall || (gramatica && pLlamada >= 0.5)
    var r: [String: Any]
    if esLlamada && gramatica && prompt.formato == "tc" {
        let (nombre, prob, toks) = caminarTc(lg, c, &est, sesgo: sesgo)
        r = try generar(
            id: id, entrada: LMInput(tokens: arr([toks.last!])), cache: c, estado: est, max: max,
            usarMtp: usarMtp, previos: toks, t0: t0) // el último entra al modelo y también es salida
        r["herramienta"] = nombre
        r["prob"] = prob
    } else {
        // Libre (charla, o sin gramática): desde el token más probable.
        r = try generar(
            id: id, entrada: LMInput(tokens: arr([primeroLibre])), cache: c, estado: est, max: max,
            usarMtp: usarMtp, previos: [], t0: t0)
        // generar no incluye el token de entrada: se le antepone.
        r["texto"] = tok.decode(tokenIds: [primeroLibre], skipSpecialTokens: true) + (r["texto"] as? String ?? "")
        r["crudo"] = tok.decode(tokenIds: [primeroLibre], skipSpecialTokens: false) + (r["crudo"] as? String ?? "")
    }
    r["tipo"] = esLlamada ? "llamada" : "charla"
    r["p_llamada"] = pLlamada
    return r
}

// MARK: - generar: texto corto, con MTP si hay cabeza

let idFin: Set<Int> = {
    var s = Set<Int>()
    if let i = tok.convertTokenToId(prompt.fin) { s.insert(i) }
    if let e = tok.eosTokenId { s.insert(e) }
    return s
}()

func generar(id: Any, texto: String, prefijo: Bool, max: Int, usarMtp: Bool) throws -> [String: Any] {
    let cache = prefijo ? copiaDelPrefijo() : try modelo.newCache(parameters: nil)
    return try generar(
        id: id, entrada: LMInput(tokens: arr(ids(texto))), cache: cache, estado: prefijo ? estadoPrefijo : nil,
        max: max, usarMtp: usarMtp)
}

/// Una imagen y una pregunta, con la plantilla de chat del modelo (sin pensar).
func ver(id: Any, imagen: String, texto: String, max: Int) async throws -> [String: Any] {
    guard conVision else { throw NSError(domain: "ba-modelo", code: 1, userInfo: [NSLocalizedDescriptionKey: "este modelo no trae la torre de visión"]) }
    let t0 = Date()
    let entrada = UserInput(
        chat: [.user(texto, images: [.init(source: .url(URL(fileURLWithPath: imagen)))])],
        additionalContext: ["enable_thinking": false])
    let lm = try await ctx.processor.prepare(input: entrada)
    var r = try generar(
        id: id, entrada: lm, cache: try modelo.newCache(parameters: nil), estado: nil, max: max, usarMtp: false)
    r["ms_imagen"] = ms(t0)
    return r
}

func generar(
    id: Any, entrada: LMInput, cache: [KVCache], estado: LMOutput.State?, max: Int, usarMtp: Bool,
    previos: [Int] = [], t0: Date = Date()
) throws -> [String: Any] {
    var p = GenerateParameters(maxTokens: max, temperature: 0)
    p.prefill.stepSize = 512
    // `previos`: lo que ya se eligió antes de generar (el comienzo forzado de
    // una llamada, en formato tc); es parte de la salida.
    var salida: [Int] = previos
    var dicho = previos.isEmpty ? "" : tok.decode(tokenIds: previos, skipSpecialTokens: false)
    var msPrimero = 0.0
    var primero = true
    var aceptados = 0, propuestos = 0
    let especulativo = usarMtp && drafter != nil

    func consumir<I: TokenIteratorProtocol>(_ it: inout I) {
        while let t = it.next() {
            if primero {
                msPrimero = ms(t0)
                primero = false
            }
            if idFin.contains(t) { break }
            salida.append(t)
            let todo = tok.decode(tokenIds: salida, skipSpecialTokens: false)
            if todo.count > dicho.count, !todo.hasSuffix("\u{FFFD}") {
                if !silencioso { emitir(["id": id, "delta": String(todo.dropFirst(dicho.count))]) }
                dicho = todo
            }
        }
    }
    if especulativo, let drafter {
        var it = try MTPSpeculativeTokenIterator(
            input: entrada, mainModel: modelo, drafter: drafter, mainCache: cache,
            parameters: p, blockSize: profundidadMtp + 1)
        consumir(&it)
        aceptados = it.acceptedCount
        propuestos = it.proposedCount
    } else {
        var it = try TokenIterator(input: entrada, model: modelo, cache: cache, state: estado, parameters: p)
        consumir(&it)
    }
    let total = ms(t0)
    let decod = Swift.max(total - msPrimero, 0.001)
    var r: [String: Any] = [
        "id": id, "fin": true, "texto": tok.decode(tokenIds: salida, skipSpecialTokens: true),
        // Con los especiales (<tool_call>…): lo que hay que leer en formato tc.
        "crudo": tok.decode(tokenIds: salida, skipSpecialTokens: false),
        "tokens": salida.count, "ms_primero": msPrimero, "ms": total,
        "tok_s": salida.count > 1 ? (Double(salida.count - 1) / decod * 10000).rounded() / 10 : 0,
        "mtp": especulativo,
    ]
    if especulativo, propuestos > 0 {
        let a = Double(aceptados) / Double(propuestos)
        r["aceptacion"] = (a * 1000).rounded() / 1000
        registrarAceptacion(a)
    }
    return r
}

// MARK: - si la cabeza MTP rinde
//
// La cabeza propone `profundidadMtp` tokens por ronda y el modelo los verifica
// juntos: con poca aceptación la verificación cuesta más de lo que ahorra
// (medido en una M1 Pro con UN borrador y 0,4 de aceptación: 26 tok/s contra
// 50 sin MTP). Se lleva un promedio móvil y, si
// queda abajo del umbral, los pedidos que no dicen nada van sin MTP. Un pedido
// con "mtp": true lo fuerza igual (para seguir midiendo).

/// Borradores por ronda: la cabeza se encadena sobre su propia salida. Con el
/// parche de extern/mlx-swift-lm el modelo verifica y deshace hasta 3.
let profundidadMtp = 3
let umbralMtp = 0.55
var aceptacionMedia: Double?
var mtpRinde = true

func registrarAceptacion(_ a: Double) {
    let m = aceptacionMedia.map { 0.7 * $0 + 0.3 * a } ?? a
    aceptacionMedia = m
    let rinde = m >= umbralMtp
    if rinde != mtpRinde {
        mtpRinde = rinde
        emitir([
            "ev": "aviso",
            "aviso": rinde
                ? "la cabeza MTP rinde otra vez (aceptación \(m)): se usa por defecto"
                : "la cabeza MTP acepta poco (\(m)): por defecto se genera sin MTP",
        ])
    }
}

// MARK: - listo

emitir([
    "ev": "listo", "ms_carga": msCarga, "ms_prefijo": msPrefijo, "tokens_prefijo": idsCabeza.count,
    "prefijo_de_disco": prefijoDeDisco,
    "formato": prompt.formato.isEmpty ? "v1" : prompt.formato,
    "mtp": varianteMtp, "huella": prompt.huella, "salidas": prompt.salidas.count,
    "dispositivo": "\(Device.defaultDevice())", "vision": conVision,
    "pruebas": prompt.pruebas.map { ["frase": $0.frase, "herramienta": $0.herramienta] },
])

func herramienta(_ etiqueta: String) -> String { prompt.equivalencias[etiqueta] ?? etiqueta }

if prueba {
    // Mide lo que importa en la máquina: elegir con las frases de prueba y
    // generar texto corto con y sin la cabeza MTP.
    var bien = 0
    var tiempos: [Double] = []
    for (frase, esperada) in prompt.pruebas {
        let t = Date()
        let (e, pr) = elegir(frase, sesgo: 0)
        tiempos.append(ms(t))
        let h = herramienta(e)
        if h == esperada { bien += 1 }
        emitir(["frase": frase, "esperada": esperada, "elegida": h, "prob": pr, "ms": tiempos.last!])
    }
    tiempos.sort()
    emitir([
        "elegir": "\(bien)/\(prompt.pruebas.count)",
        "ms_mediana": tiempos.isEmpty ? 0 : tiempos[tiempos.count / 2],
        "ms_max": tiempos.last ?? 0,
    ])
    let texto =
        "<|im_start|>user\nExplicá en dos oraciones qué es un take en una transmisión en vivo.<|im_end|>\n"
        + "<|im_start|>assistant\n<think>\n\n</think>\n\n"
    // Tres vueltas alternadas: la primera de cada modo compila kernels de Metal.
    silencioso = true
    for vuelta in 1...3 {
        for usar in drafter != nil ? [false, true] : [false] {
            do {
                var r = try generar(
                    id: usar ? "con-mtp" : "sin-mtp", texto: texto, prefijo: false, max: 96, usarMtp: usar)
                r["vuelta"] = vuelta
                r.removeValue(forKey: "texto")
                emitir(r)
            } catch {
                emitir(["id": "generar", "error": "\(error)"])
            }
        }
    }
    exit(0)
}

while let linea = readLine(strippingNewline: true) {
    guard !linea.isEmpty else { continue }
    guard let d = linea.data(using: .utf8),
        let pedido = try? JSONSerialization.jsonObject(with: d) as? [String: Any]
    else {
        emitir(["error": "no es JSON: \(linea.prefix(80))"])
        continue
    }
    let id = pedido["id"] ?? NSNull()
    switch pedido["op"] as? String ?? "" {
    case "elegir":
        guard let frase = pedido["frase"] as? String else {
            emitir(["id": id, "error": "falta frase"])
            continue
        }
        let t = Date()
        let sesgo = (pedido["sesgo"] as? NSNumber)?.floatValue ?? 0
        // v2 (tc): el nombre sale del comienzo forzado de la llamada.
        let (e, pr): (String, Double) = {
            guard prompt.formato == "tc" else { return elegir(frase, sesgo: sesgo) }
            let c = copiaDelPrefijo()
            var est = estadoPrefijo
            let lg = adelante(ids(frase + prompt.cola), c, &est)
            let r = caminarTc(lg, c, &est, sesgo: sesgo)
            return (r.nombre, r.prob)
        }()
        emitir([
            "id": id, "herramienta": herramienta(e), "etiqueta": e,
            "derivar": e == prompt.derivar, "prob": pr, "ms": ms(t),
        ])
    case "llamar":
        guard let frase = pedido["frase"] as? String else {
            emitir(["id": id, "error": "falta frase"])
            continue
        }
        do {
            emitir(
                try llamar(
                    id: id, estadoJson: pedido["estado"] as? String, frase: frase,
                    sesgo: (pedido["sesgo"] as? NSNumber)?.floatValue ?? 0,
                    gramatica: pedido["gramatica"] as? Bool ?? true,
                    max: (pedido["max"] as? NSNumber)?.intValue ?? 256,
                    usarMtp: pedido["mtp"] as? Bool ?? mtpRinde))
        } catch {
            emitir(["id": id, "error": "\(error)"])
        }
    case "generar":
        guard let texto = pedido["texto"] as? String else {
            emitir(["id": id, "error": "falta texto"])
            continue
        }
        do {
            emitir(
                try generar(
                    id: id, texto: texto, prefijo: pedido["prefijo"] as? Bool ?? false,
                    max: (pedido["max"] as? NSNumber)?.intValue ?? 128,
                    usarMtp: pedido["mtp"] as? Bool ?? mtpRinde))
        } catch {
            emitir(["id": id, "error": "\(error)"])
        }
    case "ver":
        guard let imagen = pedido["imagen"] as? String else {
            emitir(["id": id, "error": "falta imagen"])
            continue
        }
        do {
            emitir(
                try await ver(
                    id: id, imagen: imagen, texto: pedido["texto"] as? String ?? "Describí la imagen.",
                    max: (pedido["max"] as? NSNumber)?.intValue ?? 128))
        } catch {
            emitir(["id": id, "error": "\(error)"])
        }
    case "tokens":
        // Diagnóstico: cómo tokeniza este binario (comparar con el de Python).
        emitir(["id": id, "ids": ids(pedido["texto"] as? String ?? "")])
    case "salir":
        exit(0)
    default:
        emitir(["id": id, "error": "op desconocida"])
    }
}
