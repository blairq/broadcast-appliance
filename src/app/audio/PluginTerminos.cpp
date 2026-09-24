// ============================================================================
// src/app/audio/PluginTerminos.cpp — ver PluginTerminos.hpp
// ============================================================================
#include "audio/PluginTerminos.hpp"

#include <algorithm>
#include <map>

namespace ba::app::audio {
namespace {

struct Fila {
    const char* familia;
    const char* paraQue;  ///< una linea, en castellano
    const char* terminos; ///< como lo pediria alguien en cabina
};

/// Lo comun a varias familias, por prefijo del nombre de familia.
///
/// `sc_` es CADENA LATERAL y es el caso mas util de todos en una cabina: es lo
/// que hace que la musica baje sola cuando alguien habla. Sin esta palabra,
/// nadie lo encuentra pidiendo eso.
const Fila kPrefijos[] = {
    {"sc_", "Reacciona a OTRA fuente, no a la suya (cadena lateral).",
     "cadena lateral sidechain que reaccione a otra fuente que baje cuando hablen "
     "bajar la musica cuando hablan ducking atenuar automaticamente la musica locutor"},
    {"mb_", "Trabaja por bandas de frecuencia, no sobre todo junto.",
     "multibanda por bandas por frecuencias separado graves medios agudos"},
};

/// La tabla. Una fila por familia; los sufijos de canales no cuentan.
const Fila kFamilias[] = {
    // --- Dinamica: lo que se toca todos los dias ---------------------------
    {"gate", "Cierra el canal cuando nadie habla, para que no se cuele el ruido de fondo.",
     "compuerta gate ruido de fondo se escucha todo lo de alrededor microfono abierto "
     "respiracion aire silencio entre frases cerrar el microfono solo"},
    {"compressor", "Empareja lo muy fuerte con lo muy bajo: la voz queda pareja.",
     "compresor emparejar nivelar que no varie tanto se escucha fuerte y despues bajo "
     "voz despareja controlar picos apretar"},
    {"expander", "Al reves del compresor: agranda la diferencia entre fuerte y bajo.",
     "expansor agrandar la dinamica separar del ruido"},
    {"dyna_processor", "Dinamica a medida: se dibuja la curva entera.",
     "procesador de dinamica curva a medida dinamica avanzada"},
    {"gott_compressor", "Compresion multibanda agresiva, para que suene fuerte y parejo.",
     "compresion agresiva que suene fuerte y parejo pegar la voz al frente"},
    {"beat_breather", "Le devuelve pegada a la percusion.",
     "percusion bateria pegada golpe dinamica de la bateria"},
    {"autogain", "Deja el volumen parejo solo, sin que nadie toque nada.",
     "nivel parejo automatico volumen automatico loudness que quede parejo solo agc"},
    {"loud_comp", "Corrige como se oye a distinto volumen (curvas de sonoridad).",
     "sonoridad se escucha mal a bajo volumen curva de loudness"},

    // --- Techo y saturacion ------------------------------------------------
    {"limiter", "Pone un techo: no deja pasar de cierto nivel, pase lo que pase.",
     "limitador techo que no pase de no saturar proteger la salida tope de volumen"},
    {"clipper", "Recorta los picos para poder subir el volumen general.",
     "recortar picos subir el volumen general maximizar mas fuerte"},
    {"surge_filter", "Saca el golpe de arranque cuando una fuente se enciende.",
     "golpe al arrancar plop chasquido al prender ruido de encendido"},

    // --- Tono ---------------------------------------------------------------
    {"para_equalizer", "Ecualizador de precision: se elige cada frecuencia a mano.",
     "ecualizador eq graves medios agudos tono suena metalico suena apagado retumba "
     "sacar frecuencia molesta brillo cuerpo"},
    {"graph_equalizer", "Ecualizador de barras, mas rapido de tocar.",
     "ecualizador de barras graves medios agudos rapido"},
    {"filter", "Un filtro solo: saca lo de abajo o lo de arriba.",
     "filtro pasa altos pasa bajos sacar los graves sacar el retumbe zumbido de red "
     "ruido de electricidad 50 hz cortar frecuencias"},
    {"crossover", "Parte la señal en bandas para tratarlas por separado.",
     "separar en bandas dividir por frecuencia"},
    {"deesser", "Baja el siseo de las eses sin apagar la voz.",
     "siseo eses silba sibilancia ese que molesta chistido"},

    // --- Tiempo y espacio ---------------------------------------------------
    {"comp_delay", "Retrasa el audio unos milisegundos para que coincida con la imagen.",
     "sincronizar con la imagen labios desfasado audio adelantado compensar latencia "
     "retardo de compensacion alinear audio y video"},
    {"art_delay", "Eco a gusto: repeticiones, tiempos, filtros.",
     "eco delay repeticiones efecto de eco"},
    {"slap_delay", "Un eco corto, tipo estudio.",
     "eco corto slapback"},
    {"impulse_reverb", "Reverberacion de una sala real, grabada.",
     "reverberacion reverb ambiente sala iglesia que suene espacioso"},
    {"room_builder", "Arma la reverberacion de una sala imaginaria.",
     "simular una sala ambiente a medida acustica"},
    {"impulse_responses", "Aplica la respuesta de un equipo o sala grabada.",
     "respuesta al impulso simular un equipo un parlante"},

    // --- Modulacion ---------------------------------------------------------
    {"chorus", "Engrosa el sonido, como si fueran varias voces.",
     "coro engrosar ensanchar chorus"},
    {"flanger", "Efecto de barrido metalico.", "flanger barrido efecto"},
    {"phaser", "Efecto de fase, ondulante.", "phaser fase ondulante efecto"},
    {"ringmod_sc", "Modulacion en anillo con cadena lateral.", "modulacion en anillo efecto raro"},

    // --- Medir y comparar ---------------------------------------------------
    {"spectrum_analyzer", "Muestra el espectro: que frecuencias tiene el sonido.",
     "analizador de espectro ver las frecuencias que frecuencia molesta molestan "
     "medir las frecuencias grafico del audio donde esta el problema"},
    {"oscilloscope", "Muestra la onda en el tiempo.", "osciloscopio ver la onda forma de onda"},
    {"phase_detector", "Dice si dos fuentes estan en fase.",
     "fase invertida se cancelan dos microfonos fuera de fase"},
    {"latency_meter", "Mide cuanto retrasa una cadena.", "medir latencia cuanto retrasa demora"},
    {"profiler", "Mide como responde un sistema de audio.", "medir el sistema perfilar respuesta"},
    {"matcher", "Iguala el espectro al de una referencia.",
     "igualar a una referencia que suene como parecerse a otro audio"},
    {"referencer", "Compara contra un tema de referencia.",
     "comparar con una referencia escuchar una referencia"},
    {"ab_tester", "Compara dos versiones a ciegas.", "comparar a ciegas prueba ab cual suena mejor"},

    // --- Utilidades ---------------------------------------------------------
    {"mixer", "Junta varios canales en uno.", "mezclar varios canales sumar entradas"},
    {"send", "Manda audio a otro lado por memoria compartida.", "enviar audio bus auxiliar"},
    {"return", "Recibe audio mandado por otro.", "recibir audio bus auxiliar retorno"},
    {"noise_generator", "Genera ruido de prueba.", "ruido de prueba ruido rosa ruido blanco probar"},
    {"oscillator", "Genera un tono de prueba.", "tono de prueba pitido de referencia mil hertz"},
    {"trigger", "Dispara algo cuando la señal pasa un umbral.", "disparar por umbral gatillo"},

    // --- Instrumentos (no son de cabina, pero estan) ------------------------
    {"sampler", "Reproduce muestras con MIDI.", "reproducir muestras sampler midi"},
    {"multisampler", "Varias muestras con MIDI a la vez.", "multi muestras sampler midi bateria"},
};

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Los sufijos de canales que LSP le agrega a la misma familia.
const char* const kSufijos[] = {"_mono", "_stereo", "_lr",  "_ms",  "_x1", "_x2",
                                "_x4",   "_x8",     "_x12", "_x16", "_x24", "_x32",
                                "_x48",  "_do"};

const Fila* filaDe(const std::string& familia) {
    for (const Fila& f : kFamilias)
        if (familia == f.familia) return &f;
    // Las variantes con prefijo (`sc_gate`, `mb_compressor`) heredan de su base.
    for (const Fila& p : kPrefijos) {
        const std::string pre = p.familia;
        if (familia.rfind(pre, 0) == 0) {
            const Fila* base = filaDe(familia.substr(pre.size()));
            if (base != nullptr) return base;
        }
    }
    return nullptr;
}

} // namespace

std::string familiaDe(const std::string& pluginId) {
    const std::size_t punto = pluginId.rfind('.');
    std::string f = minusculas(punto == std::string::npos ? pluginId : pluginId.substr(punto + 1));
    // Puede traer mas de uno: `multisampler_x12_do`.
    for (bool corto = true; corto;) {
        corto = false;
        for (const char* s : kSufijos) {
            const std::string suf = s;
            if (f.size() > suf.size() && f.compare(f.size() - suf.size(), suf.size(), suf) == 0) {
                f = f.substr(0, f.size() - suf.size());
                corto = true;
                break;
            }
        }
    }
    return f;
}

std::string canalesDe(const std::string& pluginId) {
    const std::string id = minusculas(pluginId);
    static const std::map<std::string, std::string> kComo{
        {"_mono", "mono"},   {"_stereo", "estereo"}, {"_lr", "izquierda/derecha"},
        {"_ms", "medio/lado"}};
    for (const auto& [suf, como] : kComo)
        if (id.size() > suf.size() && id.compare(id.size() - suf.size(), suf.size(), suf) == 0)
            return como;
    // `_x8`, `_x16`: cuantas bandas o canales tiene.
    const std::size_t x = id.rfind("_x");
    if (x != std::string::npos && x + 2 < id.size() &&
        id.find_first_not_of("0123456789", x + 2) == std::string::npos)
        return id.substr(x + 2) + " canales/bandas";
    return {};
}

std::vector<std::string> terminosDePlugin(const std::string& pluginId) {
    const std::string fam = familiaDe(pluginId);
    std::vector<std::string> out;
    const auto sumar = [&out](const char* texto) {
        std::string cur;
        for (const char* p = texto;; ++p) {
            if (*p == ' ' || *p == 0) {
                if (cur.size() >= 2) out.push_back(cur);
                cur.clear();
                if (*p == 0) break;
            } else {
                cur.push_back(*p);
            }
        }
    };
    if (const Fila* f = filaDe(fam)) sumar(f->terminos);
    // El prefijo suma LO SUYO ademas de lo de la base: un `sc_gate` es una
    // compuerta Y es cadena lateral, y hay que encontrarlo por las dos cosas.
    for (const Fila& p : kPrefijos)
        if (fam.rfind(p.familia, 0) == 0) sumar(p.terminos);
    return out;
}

std::string paraQueSirve(const std::string& pluginId) {
    const std::string fam = familiaDe(pluginId);
    const Fila* f = filaDe(fam);
    if (f == nullptr) return {};
    std::string s = f->paraQue;
    for (const Fila& p : kPrefijos)
        if (fam.rfind(p.familia, 0) == 0) s += std::string(" ") + p.paraQue;
    return s;
}

} // namespace ba::app::audio
