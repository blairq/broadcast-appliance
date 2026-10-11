// ============================================================================
// ba-modelo (llama.cpp) — el modelo local en Windows y Linux
//
//   ba-modelo --modelo <carpeta> [--mtp no|auto|fp16|q8] [--prueba] [--cpu] [--fa]
//
// El MISMO protocolo que el de la Mac (src/modelo-mac, MLX): una línea JSON por
// pedido en stdin, una por respuesta en stdout. Así ModelosLocales no distingue
// cuál corre. Ver el encabezado de main.swift para el detalle de cada op:
//
//   {"id":1,"op":"elegir","frase":"bajale al mic 2","sesgo":0}
//   {"id":2,"op":"llamar","estado":"{…}","frase":"…","sesgo":0,"gramatica":true,"max":256}
//   {"id":3,"op":"preparar","estado":"{…}"}
//   {"id":4,"op":"generar","texto":"…","prefijo":true,"max":64}
//   {"id":5,"op":"ver","imagen":"/ruta/foto.jpg","texto":"¿cuántas personas hay?","max":96}
//   {"id":6,"op":"tokens","texto":"…"}
//   {"op":"salir"}
//
// La carpeta es la del repo GGUF de Hugging Face (el .gguf, prompt.json y, si
// la trae, la torre de visión mmproj*.gguf). El prefijo de prompt.json (las
// fichas de las herramientas, miles de tokens) se calcula UNA vez, se guarda
// su estado (llama_state_seq_get_data) y se restaura en cada pedido: Qwen3.5 es
// híbrido y sus capas de atención lineal no se pueden «recortar». También se
// guarda en disco junto al modelo para el próximo arranque.
//
// La GPU: todas las capas a la placa (Vulkan en Windows y Linux; Metal en la
// Mac, donde se usa solo para desarrollar); --cpu o BA_MODELO_GPU=0 para no.
// La cabeza MTP todavía no (el de la Mac tampoco la usa por defecto): "mtp": false.
// ============================================================================
#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using Reloj = std::chrono::steady_clock;

namespace {

// --- salida -----------------------------------------------------------------------

void emitir(const json& j) {
    const std::string s = j.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

[[noreturn]] void morir(const std::string& msg) {
    emitir({{"ev", "error"}, {"error", msg}});
    std::exit(1);
}

double ms(Reloj::time_point desde) {
    return std::round(std::chrono::duration<double, std::milli>(Reloj::now() - desde).count() * 10) / 10;
}

// llama.cpp escribe sus avisos por el log: a stderr, y solo lo que importa.
void logLlama(ggml_log_level nivel, const char* texto, void*) {
    if (nivel >= GGML_LOG_LEVEL_WARN) std::fputs(texto, stderr);
}

// --- estado global (un proceso = un modelo) -----------------------------------------

llama_model* modelo = nullptr;
llama_context* ctx = nullptr;
const llama_vocab* vocab = nullptr;
int nVocab = 0;
mtmd_context* vision = nullptr;
llama_pos pos = 0;              ///< la próxima posición en la secuencia 0
const float* ultimoLg = nullptr; ///< los logits de la última posición decodificada
bool silencioso = false;

struct Prompt {
    std::string cabeza, cola, fin = "<|im_end|>", derivar = "derivar", huella, formato, faltaDato = "falta_dato";
    std::vector<std::string> salidas;
    std::map<std::string, std::string> equivalencias;
    std::vector<std::pair<std::string, std::string>> pruebas;
} prompt;

std::vector<llama_token> ids(const std::string& s) {
    if (s.empty()) return {};
    int n = -llama_tokenize(vocab, s.c_str(), static_cast<int32_t>(s.size()), nullptr, 0, false, true);
    std::vector<llama_token> out(static_cast<size_t>(std::max(n, 0)));
    if (n > 0) llama_tokenize(vocab, s.c_str(), static_cast<int32_t>(s.size()), out.data(), n, false, true);
    return out;
}

std::string pieza(llama_token t, bool especiales) {
    char buf[256];
    int n = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, especiales);
    if (n < 0) {
        std::string s(static_cast<size_t>(-n), '\0');
        llama_token_to_piece(vocab, t, s.data(), -n, 0, especiales);
        return s;
    }
    return std::string(buf, static_cast<size_t>(n));
}

std::string texto(const std::vector<llama_token>& ts, bool especiales) {
    std::string s;
    for (llama_token t : ts) s += pieza(t, especiales);
    return s;
}

/// ¿Termina con una letra UTF-8 a medias? (no se emite hasta que llegue el resto)
bool utf8Incompleto(const std::string& s) {
    for (size_t i = 0; i < 4 && i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[s.size() - 1 - i]);
        if ((c & 0xC0) == 0x80) continue; // continuación
        const size_t largo = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        return largo > i + 1;
    }
    return false;
}

/// Pasa `tokens` por el modelo desde `pos` (de a n_batch), con los logits de la última.
const float* adelante(const std::vector<llama_token>& tokens) {
    if (tokens.empty()) return ultimoLg;
    const int lote = static_cast<int>(llama_n_batch(ctx));
    for (size_t desde = 0; desde < tokens.size(); desde += static_cast<size_t>(lote)) {
        const int n = static_cast<int>(std::min(tokens.size() - desde, static_cast<size_t>(lote)));
        llama_batch b = llama_batch_init(n, 0, 1);
        for (int i = 0; i < n; ++i) {
            b.token[i] = tokens[desde + static_cast<size_t>(i)];
            b.pos[i] = pos + i;
            b.n_seq_id[i] = 1;
            b.seq_id[i][0] = 0;
            b.logits[i] = (desde + static_cast<size_t>(i) == tokens.size() - 1) ? 1 : 0;
        }
        b.n_tokens = n;
        const int rc = llama_decode(ctx, b);
        llama_batch_free(b);
        if (rc != 0) throw std::runtime_error("llama_decode: " + std::to_string(rc));
        pos += n;
    }
    ultimoLg = llama_get_logits_ith(ctx, -1);
    return ultimoLg;
}

// --- el prefijo, una vez, y su estado ---------------------------------------------------

std::vector<llama_token> idsCabeza;
std::vector<uint8_t> estadoPrefijo;

void guardarEstado(std::vector<uint8_t>& buf) {
    buf.resize(llama_state_seq_get_size(ctx, 0));
    buf.resize(llama_state_seq_get_data(ctx, buf.data(), buf.size(), 0));
}

void restaurar(const std::vector<uint8_t>& buf, llama_pos n) {
    llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);
    if (llama_state_seq_set_data(ctx, buf.data(), buf.size(), 0) == 0) throw std::runtime_error("no se pudo restaurar el estado");
    pos = n;
    ultimoLg = nullptr;
}

void restaurarPrefijo() { restaurar(estadoPrefijo, static_cast<llama_pos>(idsCabeza.size())); }

// --- elegir: decodificación restringida a las salidas -----------------------------------

struct Arbol {
    std::map<std::vector<llama_token>, std::vector<llama_token>> siguientes;
    std::map<std::vector<llama_token>, std::set<std::string>> quedan;
    llama_token tokDerivar = -1;
};
Arbol arbol, arbolTc;
llama_token tokToolCall = -1;
std::set<llama_token> idFin;

void armar(Arbol& a, const std::string& antes, const std::string& despues) {
    for (const std::string& s : prompt.salidas) {
        const std::vector<llama_token> sq = ids(antes + s + despues);
        for (size_t k = 0; k <= sq.size(); ++k) {
            const std::vector<llama_token> p(sq.begin(), sq.begin() + static_cast<long>(k));
            a.quedan[p].insert(s);
            if (k < sq.size()) {
                auto& sig = a.siguientes[p];
                if (std::find(sig.begin(), sig.end(), sq[k]) == sig.end()) sig.push_back(sq[k]);
            }
        }
        if (s == prompt.derivar && !sq.empty()) a.tokDerivar = sq.front();
    }
}

/// Softmax sobre las opciones (con el sesgo en una de ellas): cuál y con qué probabilidad.
std::pair<size_t, double> masProbable(const float* lg, const std::vector<llama_token>& ops, long conSesgo, float sesgo) {
    std::vector<double> v(ops.size());
    double mx = -1e30;
    for (size_t i = 0; i < ops.size(); ++i) {
        v[i] = lg[ops[i]] + (static_cast<long>(i) == conSesgo ? sesgo : 0.0f);
        mx = std::max(mx, v[i]);
    }
    double suma = 0;
    for (double& x : v) suma += (x = std::exp(x - mx));
    size_t k = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] > v[k]) k = i;
    return {k, v[k] / suma};
}

std::pair<std::string, double> elegir(const std::string& frase, float sesgo) {
    restaurarPrefijo();
    const float* lg = adelante(ids(frase + prompt.cola));
    std::vector<llama_token> pre;
    double prob = 1;
    for (int paso = 0; paso < 16; ++paso) {
        const auto q = arbol.quedan.find(pre);
        const auto s = arbol.siguientes.find(pre);
        if (q == arbol.quedan.end() || q->second.size() <= 1 || s == arbol.siguientes.end()) break;
        const auto& ops = s->second;
        long i = -1;
        if (paso == 0 && sesgo != 0)
            for (size_t j = 0; j < ops.size(); ++j)
                if (ops[j] == arbol.tokDerivar) i = static_cast<long>(j);
        const auto [k, p] = masProbable(lg, ops, i, sesgo);
        prob *= p;
        pre.push_back(ops[k]);
        if (arbol.quedan[pre].size() <= 1) break;
        lg = adelante({ops[k]});
    }
    const auto q = arbol.quedan.find(pre);
    return {q != arbol.quedan.end() && !q->second.empty() ? *q->second.begin() : prompt.derivar, prob};
}

struct Camino {
    std::string nombre;
    double prob = 1;
    std::vector<llama_token> tokens, pendientes;
};

/// Formato tc: fuerza «<tool_call>\n<function=NOMBRE>» donde hay un camino y elige donde se abre.
Camino caminarTc(const float* lg, float sesgo) {
    Camino c;
    std::vector<llama_token> pre;
    for (;;) {
        const auto s = arbolTc.siguientes.find(pre);
        if (s == arbolTc.siguientes.end() || s->second.empty()) break;
        const auto& ops = s->second;
        llama_token elegido = ops[0];
        if (ops.size() > 1) {
            if (!c.pendientes.empty()) {
                lg = adelante(c.pendientes);
                c.pendientes.clear();
            }
            long i = -1;
            if (sesgo != 0)
                for (size_t j = 0; j < ops.size(); ++j) {
                    std::vector<llama_token> p2 = pre;
                    p2.push_back(ops[j]);
                    if (arbolTc.quedan[p2].count(prompt.derivar)) {
                        i = static_cast<long>(j);
                        break;
                    }
                }
            const auto [k, p] = masProbable(lg, ops, i, sesgo);
            c.prob *= p;
            elegido = ops[k];
        }
        pre.push_back(elegido);
        c.pendientes.push_back(elegido);
        if (arbolTc.siguientes.find(pre) == arbolTc.siguientes.end()) break;
    }
    const auto q = arbolTc.quedan.find(pre);
    c.nombre = q != arbolTc.quedan.end() && !q->second.empty() ? *q->second.begin() : prompt.derivar;
    c.tokens = pre;
    return c;
}

// --- generar: texto corto, voraz ----------------------------------------------------------

llama_token argmax(const float* lg) {
    llama_token k = 0;
    for (int i = 1; i < nVocab; ++i)
        if (lg[i] > lg[k]) k = i;
    return k;
}

json generar(const json& id, const std::vector<llama_token>& entrada, int max, std::vector<llama_token> previos,
             Reloj::time_point t0) {
    std::vector<llama_token> salida = previos;
    std::string dicho = previos.empty() ? "" : texto(previos, true);
    double msPrimero = 0;
    bool primero = true;
    const float* lg = adelante(entrada);
    for (int n = 0; n < max && lg != nullptr; ++n) {
        const llama_token t = argmax(lg);
        if (primero) {
            msPrimero = ms(t0);
            primero = false;
        }
        if (idFin.count(t) || llama_vocab_is_eog(vocab, t)) break;
        salida.push_back(t);
        const std::string todo = texto(salida, true);
        if (todo.size() > dicho.size() && !utf8Incompleto(todo)) {
            if (!silencioso) emitir({{"id", id}, {"delta", todo.substr(dicho.size())}});
            dicho = todo;
        }
        lg = adelante({t});
    }
    const double total = ms(t0), decod = std::max(total - msPrimero, 0.001);
    return {{"id", id},
            {"fin", true},
            {"texto", texto(salida, false)},
            {"crudo", texto(salida, true)},
            {"tokens", salida.size()},
            {"ms_primero", msPrimero},
            {"ms", total},
            {"tok_s", salida.size() > 1 ? std::round((salida.size() - 1) / decod * 10000) / 10 : 0.0},
            {"mtp", false}};
}

// --- preparar y llamar (formato tc) ---------------------------------------------------------

struct Preparado {
    std::string estado;
    std::vector<llama_token> ids;
    std::vector<uint8_t> buf;
    bool hay = false;
} preparado;

json preparar(const std::string& estado) {
    const auto t = Reloj::now();
    if (preparado.hay && preparado.estado == estado)
        return {{"preparado", true}, {"ms", ms(t)}, {"tokens", preparado.ids.size()}, {"ya_estaba", true}};
    restaurarPrefijo();
    preparado.ids = ids("Estado: " + estado + "\nPedido:");
    adelante(preparado.ids);
    guardarEstado(preparado.buf);
    preparado.estado = estado;
    preparado.hay = true;
    return {{"preparado", true}, {"ms", ms(t)}, {"tokens", preparado.ids.size()}, {"ya_estaba", false}};
}

json llamar(const json& id, const std::string& estado, const std::string& frase, float sesgo, bool gramatica, int max) {
    const auto t0 = Reloj::now();
    const std::string usuario = estado.empty() ? frase : "Estado: " + estado + "\nPedido: " + frase;
    const std::vector<llama_token> todos = ids(usuario + prompt.cola);
    std::vector<llama_token> faltan = todos;
    bool usoPreparado = false;
    if (preparado.hay && !estado.empty() && preparado.estado == estado && todos.size() > preparado.ids.size() &&
        std::equal(preparado.ids.begin(), preparado.ids.end(), todos.begin())) {
        restaurar(preparado.buf, static_cast<llama_pos>(idsCabeza.size() + preparado.ids.size()));
        faltan.assign(todos.begin() + static_cast<long>(preparado.ids.size()), todos.end());
        usoPreparado = true;
    } else {
        restaurarPrefijo();
    }
    const float* lg = adelante(faltan);
    const llama_token primeroLibre = argmax(lg);
    double pLlamada = 0;
    if (tokToolCall >= 0) {
        double mx = -1e30, suma = 0;
        for (int i = 0; i < nVocab; ++i) mx = std::max(mx, static_cast<double>(lg[i]));
        for (int i = 0; i < nVocab; ++i) suma += std::exp(lg[i] - mx);
        pLlamada = std::exp(lg[tokToolCall] - mx) / suma;
    }
    const bool esLlamada = primeroLibre == tokToolCall || (gramatica && pLlamada >= 0.5);
    json r;
    if (esLlamada && gramatica && prompt.formato == "tc") {
        Camino c = caminarTc(lg, sesgo);
        r = generar(id, c.pendientes, max, c.tokens, t0);
        r["herramienta"] = c.nombre;
        r["prob"] = c.prob;
    } else {
        r = generar(id, {primeroLibre}, max, {}, t0);
        r["texto"] = pieza(primeroLibre, false) + r["texto"].get<std::string>();
        r["crudo"] = pieza(primeroLibre, true) + r["crudo"].get<std::string>();
    }
    r["tipo"] = esLlamada ? "llamada" : "charla";
    r["p_llamada"] = pLlamada;
    r["preparado"] = usoPreparado;
    r["tokens_turno"] = faltan.size();
    return r;
}

// --- ver: una imagen y una pregunta -----------------------------------------------------------

json ver(const json& id, const std::string& imagen, const std::string& pregunta, int max) {
    if (vision == nullptr) throw std::runtime_error("este modelo no trae la torre de visión");
    const auto t0 = Reloj::now();
    mtmd_helper_bitmap_wrapper bm = mtmd_helper_bitmap_init_from_file(vision, imagen.c_str(), false, mtmd_helper_init_opt_default());
    if (bm.bitmap == nullptr) throw std::runtime_error("no se pudo leer la imagen " + imagen);
    const std::string conMarca = "<|im_start|>user\n" + std::string(mtmd_default_marker()) + pregunta +
                                 "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
    mtmd_input_text entrada{conMarca.c_str(), conMarca.size(), false, true};
    mtmd_input_chunks* trozos = mtmd_input_chunks_init();
    const mtmd_bitmap* bms[] = {bm.bitmap};
    int rc = mtmd_tokenize(vision, trozos, &entrada, bms, 1);
    mtmd_bitmap_free(bm.bitmap);
    if (bm.video_ctx != nullptr) mtmd_helper_video_free(bm.video_ctx);
    if (rc != 0) {
        mtmd_input_chunks_free(trozos);
        throw std::runtime_error("mtmd_tokenize: " + std::to_string(rc));
    }
    llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1); // de cero (el próximo pedido restaura el prefijo)
    llama_pos nuevo = 0;
    rc = mtmd_helper_eval_chunks(vision, ctx, trozos, 0, 0, static_cast<int32_t>(llama_n_batch(ctx)), true, &nuevo);
    mtmd_input_chunks_free(trozos);
    if (rc != 0) throw std::runtime_error("mtmd_helper_eval_chunks: " + std::to_string(rc));
    pos = nuevo;
    ultimoLg = llama_get_logits_ith(ctx, -1);
    const double msImagen = ms(t0);
    json r = generar(id, {}, max, {}, t0);
    r["ms_imagen"] = msImagen;
    return r;
}

// --- carga -------------------------------------------------------------------------------

void leerPrompt(const fs::path& dir) {
    std::ifstream f(dir / "prompt.json");
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object() || !j.contains("cabeza") || !j.contains("cola")) morir("la carpeta no tiene un prompt.json válido (cabeza, cola)");
    prompt.cabeza = j.value("cabeza", "");
    prompt.cola = j.value("cola", "");
    prompt.fin = j.value("fin", "<|im_end|>");
    prompt.derivar = j.value("derivar", "derivar");
    prompt.huella = j.value("huella", "");
    prompt.formato = j.value("formato", "");
    prompt.faltaDato = j.value("falta_dato", "falta_dato");
    for (const char* k : {"salidas", "funciones"})
        if (prompt.salidas.empty() && j.contains(k) && j[k].is_array())
            for (const auto& s : j[k]) prompt.salidas.push_back(s.get<std::string>());
    if (prompt.salidas.empty()) morir("el prompt.json no trae salidas ni funciones");
    if (j.contains("equivalencias") && j["equivalencias"].is_object())
        for (auto it = j["equivalencias"].begin(); it != j["equivalencias"].end(); ++it)
            prompt.equivalencias[it.key()] = it.value().get<std::string>();
    for (const auto& p : j.value("frases_prueba", json::array()))
        if (p.contains("frase") && p.contains("herramienta"))
            prompt.pruebas.emplace_back(p["frase"].get<std::string>(), p["herramienta"].get<std::string>());
}

std::string herramienta(const std::string& etiqueta) {
    const auto it = prompt.equivalencias.find(etiqueta);
    return it == prompt.equivalencias.end() ? etiqueta : it->second;
}

/// El .gguf del modelo (el más grande que no es la torre de visión) y la torre, si está.
std::pair<fs::path, fs::path> archivos(const fs::path& dir) {
    fs::path gguf, mmproj;
    std::uintmax_t mayor = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (e.path().extension() != ".gguf" || n[0] == '.') continue;
        if (n.rfind("mmproj", 0) == 0) {
            mmproj = e.path();
            continue;
        }
        if (e.file_size(ec) > mayor) {
            mayor = e.file_size(ec);
            gguf = e.path();
        }
    }
    return {gguf, mmproj};
}

/// La carpeta del ejecutable (ahí, o en su «llama/», están los backends de ggml).
fs::path carpetaDelEjecutable() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? fs::path(std::string(buf, static_cast<size_t>(n))).parent_path() : fs::path();
#else
    return {};
#endif
}

std::string firmaDe(const fs::path& p) {
    std::error_code ec;
    return p.filename().string() + ":" + std::to_string(fs::file_size(p, ec)) + ":" +
           std::to_string(std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(p, ec).time_since_epoch()).count());
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // sin \r\n: el protocolo es una línea por mensaje
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::string carpeta, modoMtp = "no";
    bool prueba = false, cpu = std::getenv("BA_MODELO_GPU") != nullptr && std::string(std::getenv("BA_MODELO_GPU")) == "0";
    // FLASH ATTENTION, APAGADA. llama.cpp la prende sola («auto») y con este
    // modelo (híbrido, qwen35) el estado se lee DESPUÉS de un prefijo de ~8000
    // fichas: medido con llama-bench b11255 en una RX 5700 XT (Vulkan), leer
    // a esa profundidad va a 585 fichas/s sin ella y a 100 con ella —el estado
    // de cada frase tardaba 4,7 s en vez de menos de uno—. Generar pierde poco
    // (67 contra 77 fichas/s). En placas donde sí conviene: --fa o BA_MODELO_FA=1.
    bool fa = std::getenv("BA_MODELO_FA") != nullptr && std::string(std::getenv("BA_MODELO_FA")) == "1";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--modelo" && i + 1 < argc) carpeta = argv[++i];
        else if (a == "--mtp" && i + 1 < argc) modoMtp = argv[++i];
        else if (a == "--prueba") prueba = true;
        else if (a == "--cpu") cpu = true;
        else if (a == "--fa") fa = true;
        else morir("argumento desconocido: " + a);
    }
    if (carpeta.empty()) morir("uso: ba-modelo --modelo <carpeta> [--mtp no|auto|fp16|q8] [--prueba] [--cpu] [--fa]");
    const fs::path dir = fs::u8path(carpeta);
    leerPrompt(dir);
    const auto [gguf, mmproj] = archivos(dir);
    if (gguf.empty()) morir("la carpeta no tiene un .gguf: " + carpeta);

    llama_log_set(logLlama, nullptr);
    // Los backends (el de CPU según el procesador, Vulkan): en Linux en bin/64bit/llama
    // (con sus bibliotecas), en Windows al lado del ejecutable.
    if (const fs::path aparte = carpetaDelEjecutable() / "llama"; fs::is_directory(aparte))
        ggml_backend_load_all_from_path(aparte.string().c_str());
    else
        ggml_backend_load_all();
    llama_backend_init();
    const auto tCarga = Reloj::now();
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = cpu ? 0 : -1;
    modelo = llama_model_load_from_file(gguf.string().c_str(), mp);
    if (modelo == nullptr) morir("no se pudo cargar el modelo: " + gguf.string());
    vocab = llama_model_get_vocab(modelo);
    nVocab = llama_vocab_n_tokens(vocab);

    idsCabeza = ids(prompt.cabeza);
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = static_cast<uint32_t>(idsCabeza.size() + 4096);
    cp.n_batch = 2048;
    cp.n_ubatch = 512;
    cp.n_seq_max = 1;
    cp.flash_attn_type = fa ? LLAMA_FLASH_ATTN_TYPE_ENABLED : LLAMA_FLASH_ATTN_TYPE_DISABLED;
    const int hilos = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency() / 2)));
    cp.n_threads = hilos;
    cp.n_threads_batch = hilos;
    ctx = llama_init_from_model(modelo, cp);
    if (ctx == nullptr) morir("no se pudo crear el contexto");
    if (!mmproj.empty()) {
        mtmd_context_params vp = mtmd_context_params_default();
        vp.use_gpu = !cpu;
        vp.print_timings = false;
        vp.n_threads = hilos;
        vision = mtmd_init_from_file(mmproj.string().c_str(), modelo, vp);
        if (vision == nullptr) emitir({{"ev", "aviso"}, {"aviso", "la torre de visión no cargó: sigo sin visión"}});
    }
    const double msCarga = ms(tCarga);

    // El prefijo: del disco si está (mismo prompt, mismos pesos), si no se calcula.
    const auto tPrefijo = Reloj::now();
    const fs::path archivoPrefijo =
        dir / (".prefijo-" + (prompt.huella.empty() ? std::string("sin-huella") : prompt.huella) +
              (fa ? "-llama-fa.bin" : "-llama-sinfa.bin")); // el KV se guarda distinto con y sin FA
    const fs::path firmaPrefijo = fs::path(archivoPrefijo).concat(".json");
    {
        // El de antes (sin la marca de FA, calculado con FA): ya no se usa, son cientos de MB.
        std::error_code e;
        const fs::path viejo = dir / (".prefijo-" + (prompt.huella.empty() ? std::string("sin-huella") : prompt.huella) + "-llama.bin");
        fs::remove(viejo, e);
        fs::remove(fs::path(viejo).concat(".json"), e);
    }
    const json datosPrefijo{{"huella", prompt.huella}, {"tokens", idsCabeza.size()}, {"pesos", firmaDe(gguf)}};
    bool deDisco = false;
    if (std::getenv("BA_PREFIJO_SIN_DISCO") == nullptr && fs::exists(archivoPrefijo) && fs::exists(firmaPrefijo)) {
        std::ifstream ff(firmaPrefijo);
        const json guardado = json::parse(ff, nullptr, false);
        std::vector<llama_token> leidos(idsCabeza.size() + 8);
        size_t n = 0;
        if (guardado == datosPrefijo &&
            llama_state_seq_load_file(ctx, archivoPrefijo.string().c_str(), 0, leidos.data(), leidos.size(), &n) > 0 &&
            n == idsCabeza.size()) {
            pos = static_cast<llama_pos>(n);
            deDisco = true;
        } else {
            llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);
        }
    }
    try {
        if (!deDisco) {
            pos = 0;
            adelante(idsCabeza);
            if (llama_state_seq_save_file(ctx, archivoPrefijo.string().c_str(), 0, idsCabeza.data(), idsCabeza.size()) > 0)
                std::ofstream(firmaPrefijo) << datosPrefijo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            else
                emitir({{"ev", "aviso"}, {"aviso", "no se pudo guardar el prefijo calculado"}});
        }
    } catch (const std::exception& e) {
        morir(std::string("no se pudo calcular el prefijo: ") + e.what());
    }
    guardarEstado(estadoPrefijo);
    const double msPrefijo = ms(tPrefijo);

    armar(arbol, "", "");
    if (prompt.formato == "tc") armar(arbolTc, "<tool_call>\n<function=", ">");
    if (const auto t = ids("<tool_call>"); t.size() == 1) tokToolCall = t[0];
    if (const auto t = ids(prompt.fin); t.size() == 1) idFin.insert(t[0]);
    idFin.insert(llama_vocab_eos(vocab));

    std::string dispositivo;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t d = ggml_backend_dev_get(i);
        dispositivo += (dispositivo.empty() ? "" : ", ") + std::string(ggml_backend_dev_description(d));
    }
    json pruebas = json::array();
    for (const auto& [f, h] : prompt.pruebas) pruebas.push_back({{"frase", f}, {"herramienta", h}});
    emitir({{"ev", "listo"},
            {"ms_carga", msCarga},
            {"ms_prefijo", msPrefijo},
            {"tokens_prefijo", idsCabeza.size()},
            {"prefijo_de_disco", deDisco},
            {"formato", prompt.formato.empty() ? "v1" : prompt.formato},
            {"mtp", "no"},
            {"huella", prompt.huella},
            {"salidas", prompt.salidas.size()},
            {"dispositivo", cpu ? "CPU" : dispositivo},
            {"vision", vision != nullptr},
            {"motor", "llama.cpp"},
            {"pruebas", pruebas}});

    if (prueba) {
        int bien = 0;
        std::vector<double> tiempos;
        for (const auto& [frase, esperada] : prompt.pruebas) {
            const auto t = Reloj::now();
            const auto [e, p] = elegir(frase, 0);
            tiempos.push_back(ms(t));
            if (herramienta(e) == esperada) ++bien;
            emitir({{"frase", frase}, {"esperada", esperada}, {"elegida", herramienta(e)}, {"prob", p}, {"ms", tiempos.back()}});
        }
        std::sort(tiempos.begin(), tiempos.end());
        emitir({{"elegir", std::to_string(bien) + "/" + std::to_string(prompt.pruebas.size())},
                {"ms_mediana", tiempos.empty() ? 0.0 : tiempos[tiempos.size() / 2]},
                {"ms_max", tiempos.empty() ? 0.0 : tiempos.back()}});
        silencioso = true;
        const std::string t = "<|im_start|>user\nExplicá en dos oraciones qué es un take en una transmisión en vivo.<|im_end|>\n"
                              "<|im_start|>assistant\n<think>\n\n</think>\n\n";
        for (int vuelta = 1; vuelta <= 2; ++vuelta) {
            llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);
            pos = 0;
            json r = generar("sin-mtp", ids(t), 96, {}, Reloj::now());
            r.erase("texto");
            r["vuelta"] = vuelta;
            emitir(r);
        }
    }
    // (Liberar antes de salir: con lo de la GPU vivo, la limpieza final de ggml se caía.)
    if (prueba) {
        if (vision != nullptr) mtmd_free(vision);
        llama_free(ctx);
        llama_model_free(modelo);
        llama_backend_free();
        return 0;
    }

    std::string linea;
    while (std::getline(std::cin, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) continue;
        const json pedido = json::parse(linea, nullptr, false);
        if (!pedido.is_object()) {
            emitir({{"error", "no es JSON: " + linea.substr(0, 80)}});
            continue;
        }
        const json id = pedido.value("id", json());
        const std::string op = pedido.value("op", "");
        try {
            if (op == "elegir") {
                if (!pedido.contains("frase")) throw std::runtime_error("falta frase");
                const auto t = Reloj::now();
                const float sesgo = pedido.value("sesgo", 0.0f);
                std::pair<std::string, double> r;
                if (prompt.formato == "tc") {
                    restaurarPrefijo();
                    const Camino c = caminarTc(adelante(ids(pedido["frase"].get<std::string>() + prompt.cola)), sesgo);
                    r = {c.nombre, c.prob};
                } else {
                    r = elegir(pedido["frase"].get<std::string>(), sesgo);
                }
                emitir({{"id", id}, {"herramienta", herramienta(r.first)}, {"etiqueta", r.first},
                        {"derivar", r.first == prompt.derivar}, {"prob", r.second}, {"ms", ms(t)}});
            } else if (op == "llamar") {
                if (!pedido.contains("frase")) throw std::runtime_error("falta frase");
                emitir(llamar(id, pedido.value("estado", ""), pedido["frase"].get<std::string>(), pedido.value("sesgo", 0.0f),
                              pedido.value("gramatica", true), pedido.value("max", 256)));
            } else if (op == "preparar") {
                const std::string estado = pedido.value("estado", "");
                if (prompt.formato != "tc" || estado.empty()) throw std::runtime_error("preparar es para el formato tc, con estado");
                json r = preparar(estado);
                r["id"] = id;
                emitir(r);
            } else if (op == "generar") {
                if (!pedido.contains("texto")) throw std::runtime_error("falta texto");
                if (pedido.value("prefijo", false)) {
                    restaurarPrefijo();
                } else {
                    llama_memory_seq_rm(llama_get_memory(ctx), 0, -1, -1);
                    pos = 0;
                }
                emitir(generar(id, ids(pedido["texto"].get<std::string>()), pedido.value("max", 128), {}, Reloj::now()));
            } else if (op == "ver") {
                if (!pedido.contains("imagen")) throw std::runtime_error("falta imagen");
                emitir(ver(id, pedido["imagen"].get<std::string>(), pedido.value("texto", "Describí la imagen."), pedido.value("max", 128)));
            } else if (op == "tokens") {
                emitir({{"id", id}, {"ids", ids(pedido.value("texto", ""))}});
            } else if (op == "salir") {
                break;
            } else {
                emitir({{"id", id}, {"error", "op desconocida"}});
            }
        } catch (const std::exception& e) {
            emitir({{"id", id}, {"error", e.what()}});
        }
    }
    if (vision != nullptr) mtmd_free(vision);
    llama_free(ctx);
    llama_model_free(modelo);
    llama_backend_free();
    return 0;
}
