// ============================================================================
// src/app/audio/PluginTerminos.cpp — ver PluginTerminos.hpp
// ============================================================================
#include "audio/PluginTerminos.hpp"

#include <algorithm>
#include <map>

#include "i18n/I18n.hpp"

// Sin Qt acá: la marca es para lupdate; se traduce al usarla (ba::i18n::traducir).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::app::audio {
namespace {

// Se marcan acá y se traducen al usarlas: para qué sirve (lo lee el asistente) y las
// palabras con que lo buscaría alguien en cabina, en el idioma del operador.
struct Fila {
    const char* familia;
    const char* paraQue;  ///< una linea: QT_TRANSLATE_NOOP("asistente", …)
    const char* terminos; ///< como lo pediria alguien en cabina: QT_TRANSLATE_NOOP("mcp-etiquetas", …)
};

/// Lo comun a varias familias, por prefijo del nombre de familia.
///
/// `sc_` es CADENA LATERAL y es el caso mas util de todos en una cabina: es lo
/// que hace que la musica baje sola cuando alguien habla. Sin esta palabra,
/// nadie lo encuentra pidiendo eso.
const Fila kPrefijos[] = {
    {"sc_", QT_TRANSLATE_NOOP("asistente", "Reacciona a OTRA fuente, no a la suya (cadena lateral)."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "cadena lateral sidechain que reaccione a otra fuente que baje cuando hablen "
                                   "bajar la musica cuando hablan ducking atenuar automaticamente la musica locutor")},
    {"mb_", QT_TRANSLATE_NOOP("asistente", "Trabaja por bandas de frecuencia, no sobre todo junto."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "multibanda por bandas por frecuencias separado graves medios agudos")},
};

/// La tabla. Una fila por familia; los sufijos de canales no cuentan.
const Fila kFamilias[] = {
    // --- Dinamica: lo que se toca todos los dias ---------------------------
    {"gate", QT_TRANSLATE_NOOP("asistente", "Cierra el canal cuando nadie habla, para que no se cuele el ruido de fondo."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "compuerta gate ruido de fondo se escucha todo lo de alrededor microfono abierto "
                                   "respiracion aire silencio entre frases cerrar el microfono solo")},
    {"compressor", QT_TRANSLATE_NOOP("asistente", "Empareja lo muy fuerte con lo muy bajo: la voz queda pareja."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "compresor emparejar nivelar que no varie tanto se escucha fuerte y despues bajo "
                                   "voz despareja controlar picos apretar")},
    {"expander", QT_TRANSLATE_NOOP("asistente", "Al reves del compresor: agranda la diferencia entre fuerte y bajo."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "expansor agrandar la dinamica separar del ruido")},
    {"dyna_processor", QT_TRANSLATE_NOOP("asistente", "Dinamica a medida: se dibuja la curva entera."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "procesador de dinamica curva a medida dinamica avanzada")},
    {"gott_compressor", QT_TRANSLATE_NOOP("asistente", "Compresion multibanda agresiva, para que suene fuerte y parejo."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "compresion agresiva que suene fuerte y parejo pegar la voz al frente")},
    {"beat_breather", QT_TRANSLATE_NOOP("asistente", "Le devuelve pegada a la percusion."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "percusion bateria pegada golpe dinamica de la bateria")},
    {"autogain", QT_TRANSLATE_NOOP("asistente", "Deja el volumen parejo solo, sin que nadie toque nada."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "nivel parejo automatico volumen automatico loudness que quede parejo solo agc")},
    {"loud_comp", QT_TRANSLATE_NOOP("asistente", "Corrige como se oye a distinto volumen (curvas de sonoridad)."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "sonoridad se escucha mal a bajo volumen curva de loudness")},

    // --- Techo y saturacion ------------------------------------------------
    {"limiter", QT_TRANSLATE_NOOP("asistente", "Pone un techo: no deja pasar de cierto nivel, pase lo que pase."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "limitador techo que no pase de no saturar proteger la salida tope de volumen")},
    {"clipper", QT_TRANSLATE_NOOP("asistente", "Recorta los picos para poder subir el volumen general."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "recortar picos subir el volumen general maximizar mas fuerte")},
    {"surge_filter", QT_TRANSLATE_NOOP("asistente", "Saca el golpe de arranque cuando una fuente se enciende."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "golpe al arrancar plop chasquido al prender ruido de encendido")},

    // --- Tono ---------------------------------------------------------------
    {"para_equalizer", QT_TRANSLATE_NOOP("asistente", "Ecualizador de precision: se elige cada frecuencia a mano."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "ecualizador eq graves medios agudos tono suena metalico suena apagado retumba "
                                   "sacar frecuencia molesta brillo cuerpo")},
    {"graph_equalizer", QT_TRANSLATE_NOOP("asistente", "Ecualizador de barras, mas rapido de tocar."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "ecualizador de barras graves medios agudos rapido")},
    {"filter", QT_TRANSLATE_NOOP("asistente", "Un filtro solo: saca lo de abajo o lo de arriba."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "filtro pasa altos pasa bajos sacar los graves sacar el retumbe zumbido de red "
                                   "ruido de electricidad 50 hz cortar frecuencias")},
    {"crossover", QT_TRANSLATE_NOOP("asistente", "Parte la señal en bandas para tratarlas por separado."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "separar en bandas dividir por frecuencia")},
    {"deesser", QT_TRANSLATE_NOOP("asistente", "Baja el siseo de las eses sin apagar la voz."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "siseo eses silba sibilancia ese que molesta chistido")},

    // --- Tiempo y espacio ---------------------------------------------------
    {"comp_delay", QT_TRANSLATE_NOOP("asistente", "Retrasa el audio unos milisegundos para que coincida con la imagen."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "sincronizar con la imagen labios desfasado audio adelantado compensar latencia "
                                   "retardo de compensacion alinear audio y video")},
    {"art_delay", QT_TRANSLATE_NOOP("asistente", "Eco a gusto: repeticiones, tiempos, filtros."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "eco delay repeticiones efecto de eco")},
    {"slap_delay", QT_TRANSLATE_NOOP("asistente", "Un eco corto, tipo estudio."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "eco corto slapback")},
    {"impulse_reverb", QT_TRANSLATE_NOOP("asistente", "Reverberacion de una sala real, grabada."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "reverberacion reverb ambiente sala iglesia que suene espacioso")},
    {"room_builder", QT_TRANSLATE_NOOP("asistente", "Arma la reverberacion de una sala imaginaria."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "simular una sala ambiente a medida acustica")},
    {"impulse_responses", QT_TRANSLATE_NOOP("asistente", "Aplica la respuesta de un equipo o sala grabada."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "respuesta al impulso simular un equipo un parlante")},

    // --- Modulacion ---------------------------------------------------------
    {"chorus", QT_TRANSLATE_NOOP("asistente", "Engrosa el sonido, como si fueran varias voces."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "coro engrosar ensanchar chorus")},
    {"flanger", QT_TRANSLATE_NOOP("asistente", "Efecto de barrido metalico."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "flanger barrido efecto")},
    {"phaser", QT_TRANSLATE_NOOP("asistente", "Efecto de fase, ondulante."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "phaser fase ondulante efecto")},
    {"ringmod_sc", QT_TRANSLATE_NOOP("asistente", "Modulacion en anillo con cadena lateral."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "modulacion en anillo efecto raro")},

    // --- Medir y comparar ---------------------------------------------------
    {"spectrum_analyzer", QT_TRANSLATE_NOOP("asistente", "Muestra el espectro: que frecuencias tiene el sonido."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "analizador de espectro ver las frecuencias que frecuencia molesta molestan "
                                   "medir las frecuencias grafico del audio donde esta el problema")},
    {"oscilloscope", QT_TRANSLATE_NOOP("asistente", "Muestra la onda en el tiempo."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "osciloscopio ver la onda forma de onda")},
    {"phase_detector", QT_TRANSLATE_NOOP("asistente", "Dice si dos fuentes estan en fase."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "fase invertida se cancelan dos microfonos fuera de fase")},
    {"latency_meter", QT_TRANSLATE_NOOP("asistente", "Mide cuanto retrasa una cadena."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "medir latencia cuanto retrasa demora")},
    {"profiler", QT_TRANSLATE_NOOP("asistente", "Mide como responde un sistema de audio."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "medir el sistema perfilar respuesta")},
    {"matcher", QT_TRANSLATE_NOOP("asistente", "Iguala el espectro al de una referencia."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "igualar a una referencia que suene como parecerse a otro audio")},
    {"referencer", QT_TRANSLATE_NOOP("asistente", "Compara contra un tema de referencia."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "comparar con una referencia escuchar una referencia")},
    {"ab_tester", QT_TRANSLATE_NOOP("asistente", "Compara dos versiones a ciegas."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "comparar a ciegas prueba ab cual suena mejor")},

    // --- Utilidades ---------------------------------------------------------
    {"mixer", QT_TRANSLATE_NOOP("asistente", "Junta varios canales en uno."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "mezclar varios canales sumar entradas")},
    {"send", QT_TRANSLATE_NOOP("asistente", "Manda audio a otro lado por memoria compartida."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "enviar audio bus auxiliar")},
    {"return", QT_TRANSLATE_NOOP("asistente", "Recibe audio mandado por otro."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "recibir audio bus auxiliar retorno")},
    {"noise_generator", QT_TRANSLATE_NOOP("asistente", "Genera ruido de prueba."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "ruido de prueba ruido rosa ruido blanco probar")},
    {"oscillator", QT_TRANSLATE_NOOP("asistente", "Genera un tono de prueba."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "tono de prueba pitido de referencia mil hertz")},
    {"trigger", QT_TRANSLATE_NOOP("asistente", "Dispara algo cuando la señal pasa un umbral."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "disparar por umbral gatillo")},

    // --- Instrumentos (no son de cabina, pero estan) ------------------------
    {"sampler", QT_TRANSLATE_NOOP("asistente", "Reproduce muestras con MIDI."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "reproducir muestras sampler midi")},
    {"multisampler", QT_TRANSLATE_NOOP("asistente", "Varias muestras con MIDI a la vez."),
     //: palabras de búsqueda separadas por espacios, en el idioma: sinónimos, no una traducción palabra por palabra
     QT_TRANSLATE_NOOP("mcp-etiquetas", "multi muestras sampler midi bateria")},
};

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Los sufijos de canales que LSP le agrega a la misma familia.
const char* const kSufijos[] = {"_mono", "_stereo", "_lr",  "_ms",  "_x1",  "_x2",  "_x4",
                                "_x8",   "_x12",    "_x16", "_x24", "_x32", "_x48", "_do"};

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
        {"_mono", QT_TRANSLATE_NOOP("asistente", "mono")},
        {"_stereo", QT_TRANSLATE_NOOP("asistente", "estereo")},
        {"_lr", QT_TRANSLATE_NOOP("asistente", "izquierda/derecha")},
        {"_ms", QT_TRANSLATE_NOOP("asistente", "medio/lado")}};
    for (const auto& [suf, como] : kComo)
        if (id.size() > suf.size() && id.compare(id.size() - suf.size(), suf.size(), suf) == 0)
            return ba::i18n::traducir("asistente", como.c_str());
    // `_x8`, `_x16`: cuantas bandas o canales tiene.
    const std::size_t x = id.rfind("_x");
    if (x != std::string::npos && x + 2 < id.size() &&
        id.find_first_not_of("0123456789", x + 2) == std::string::npos)
        return ba::i18n::llenar(ba::i18n::traducir("asistente", "%1 canales/bandas"), {id.substr(x + 2)});
    return {};
}

std::vector<std::string> terminosDePlugin(const std::string& pluginId) {
    const std::string fam = familiaDe(pluginId);
    std::vector<std::string> out;
    const auto sumar = [&out](const char* original) {
        const std::string traducido = ba::i18n::traducir("mcp-etiquetas", original);
        const char* texto = traducido.c_str();
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
    std::string s = ba::i18n::traducir("asistente", f->paraQue);
    for (const Fila& p : kPrefijos)
        if (fam.rfind(p.familia, 0) == 0) s += " " + ba::i18n::traducir("asistente", p.paraQue);
    return s;
}

} // namespace ba::app::audio
