// test_mcp_busqueda.cpp — el buscador de herramientas
//
// Lo que se prueba no es "que devuelva algo": es que devuelva LO QUE SIRVE
// para una consulta escrita como la escribiría una persona. Las consultas de
// abajo salieron de probar el buscador anterior y ver qué traía:
//
//   "poner musica"      -> scene_switch, guest_source_add, share_take
//   "silenciar a alguien" -> chat_wait, evento_cue, scene_protect
//
// Ninguna de esas era la respuesta. `media_use` y `audio_set` existen y no
// aparecían, porque su documentación no dice "música" ni "silenciar": la
// escribió alguien que ya sabía cómo se llamaban.
//
// Por eso cada caso de acá es "esta consulta tiene que traer ESTA herramienta
// entre las primeras", que es lo único que importa del otro lado.
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <fstream>

#include <nlohmann/json.hpp>

#include "mcp/McpEtiquetas.hpp"
#include "mcp/McpIndice.hpp"

using namespace ba::app::mcp;

int g_fallos = 0;
#define CHECK(cond, que)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("  FALLO: %s\n", (que));                                                   \
            ++g_fallos;                                                                            \
        }                                                                                          \
    } while (0)

namespace {

/// Las herramientas de verdad, con su nombre y un título parecido al real.
/// No hace falta la descripción: si el buscador necesita la descripción para
/// encontrar algo, es que falta una etiqueta, que es justo lo que se prueba.
const std::pair<const char*, const char*> kHerramientas[] = {
    {"help", "Sistema de ayuda"},
    {"help_search", "Buscar en la ayuda"},
    {"status", "Estado general"},
    {"scene_list", "Las escenas"},
    {"scene_switch", "Poner una escena al aire"},
    {"scene_create", "Crear una escena"},
    {"scene_delete", "Borrar una escena"},
    {"scene_protect", "Proteger una escena"},
    {"source_add", "Agregar una fuente"},
    {"source_remove", "Quitar una fuente"},
    {"guest_source_add", "Poner a un invitado en una escena"},
    {"audio_set", "Ajustar un canal de audio"},
    {"audio_sincro", "Sincronizar labios y voz"},
    {"audio_eq", "Ecualizador"},
    {"audio_compressor", "Compresor"},
    {"audio_measure", "Medir el nivel"},
    {"stream_start", "Empezar a transmitir"},
    {"stream_stop", "Terminar la transmision"},
    {"record_start", "Empezar a grabar"},
    {"capture_program", "Ver el programa"},
    {"capture_person", "Ver a una persona"},
    {"room_status", "Estado de la sala"},
    {"room_link", "El enlace de la sala"},
    {"participant_admit", "Admitir a un invitado"},
    {"participant_kick", "Sacar a un invitado"},
    {"chat_send", "Mandar un mensaje"},
    {"share_take", "Poner un archivo compartido en la escena"},
    {"speaker_next", "El orador que sigue"},
    {"speaker_time", "El tiempo del orador"},
    {"media_list", "Los archivos"},
    {"media_find", "Buscar un archivo"},
    {"media_use", "Usar un archivo"},
    {"grafico_mostrar", "Poner un grafico al aire"},
    {"rotulo_vincular", "Vincular un rotulo"},
    {"log_leer", "Leer el registro"},
    {"prompter_cargar", "Cargar un texto"},
    {"pantalla_listar", "Que pantallas hay"},
    {"pantalla_mostrar", "Mostrar algo en una pantalla"},
    {"pantalla_parar", "Dejar de mostrar"},
    {"superficie_listar", "Que paginas del aparato estan vivas"},
    {"superficie_mostrar", "Cambiarle la pagina a una superficie viva"},
    {"qr_generar", "Generar un codigo"},
    {"qr_exportar", "Dejar un codigo en un archivo"},
    {"evento_correr", "Correr el guion"},
    {"alert_create", "Crear una alerta"},
    {"agente_encargar", "Encargarle algo a un rol"},
    {"youtube_live_start", "Empezar el vivo"},
    {"twitch_chat", "El chat de Twitch"},
    {"ui_show_page", "Ir a una pagina"},
    {"operator_ask", "Preguntarle al operador"},
    {"transition_set", "La transicion"},
    {"ndi_sources", "Fuentes NDI"},
    {"video_input_add", "Agregar una camara"},
    {"settings_set", "Cambiar una configuracion"},
};


// --- La bateria de preguntas tipicas ----------------------------------------
//
// El corpus vive en datos/preguntas.json y lo comparten DOS bancos de prueba:
// este (el buscador, sin modelo ni red) y tools/bateria-modelo.py (el modelo).
// Un solo corpus para los dos, o se van separando y dejan de comparar lo mismo.
//
// QUE MIDE ESTE. Si la herramienta que hace falta esta entre las primeras que
// devuelve el buscador. Es lo que decide si se le pueden dar al modelo DIEZ
// herramientas en vez de 264 —medido: 2.400 tokens contra 36.500, y un acierto
// mas—. Si el buscador no la trae, el modelo no puede elegirla: no la vio.
struct Caso {
    std::string pregunta;
    std::vector<std::string> herramientas;
};

std::vector<Caso> leerCorpus(const std::string& archivo, bool& ok) {
    std::vector<Caso> out;
    std::ifstream f(archivo);
    if (!f) {
        std::printf("  FALLO: no se pudo abrir %s\n", archivo.c_str());
        ok = false;
        return out;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::printf("  FALLO: %s no es JSON valido: %s\n", archivo.c_str(), e.what());
        ok = false;
        return out;
    }
    for (const auto& c : j["casos"]) {
        Caso k;
        k.pregunta = c.value("pregunta", "");
        for (const auto& h : c["herramientas"]) k.herramientas.push_back(h.get<std::string>());
        if (!k.pregunta.empty() && !k.herramientas.empty()) out.push_back(std::move(k));
    }
    return out;
}

/// El puesto de la PRIMERA aceptable, o -1 si no esta entre `tope`.
int puestoDeAlguna(const Indice& i, const Caso& c, std::size_t tope) {
    const auto r = i.buscar(c.pregunta, tope);
    for (std::size_t n = 0; n < r.size(); ++n)
        for (const std::string& h : c.herramientas)
            if (i.doc(r[n].doc).id == h) return static_cast<int>(n);
    return -1;
}

Indice armarIndice() {
    Indice i;
    for (const auto& [nombre, titulo] : kHerramientas) {
        Documento d;
        d.id = nombre;
        d.clase = "herramienta";
        d.nombre = nombre;
        d.titulo = titulo;
        d.etiquetas = etiquetasDe(nombre);
        i.agregar(std::move(d));
    }
    i.armar();
    return i;
}

/// El mismo indice, mas las herramientas que nombra el corpus y que la tabla
/// de arriba no tiene. Sin esto, un caso sobre una herramienta que existe de
/// verdad pero no esta en la tabla fallaria por una razon que no es la que se
/// quiere medir.
Indice armarIndiceCon(const std::vector<Caso>& casos) {
    Indice i;
    std::set<std::string> puestas;
    for (const auto& [nombre, titulo] : kHerramientas) {
        Documento d;
        d.id = nombre;
        d.clase = "herramienta";
        d.nombre = nombre;
        d.titulo = titulo;
        d.etiquetas = etiquetasDe(nombre);
        i.agregar(std::move(d));
        puestas.insert(nombre);
    }
    for (const Caso& c : casos)
        for (const std::string& h : c.herramientas) {
            if (!puestas.insert(h).second) continue;
            Documento d;
            d.id = h;
            d.clase = "herramienta";
            d.nombre = h;
            d.etiquetas = etiquetasDe(h);
            i.agregar(std::move(d));
        }
    i.armar();
    return i;
}

/// La posición de `quien` en los resultados, o -1 si no está.
int puesto(const Indice& i, const std::string& consulta, const std::string& quien) {
    const std::vector<Resultado> r = i.buscar(consulta, 12);
    for (std::size_t k = 0; k < r.size(); ++k)
        if (i.doc(r[k].doc).id == quien) return static_cast<int>(k);
    return -1;
}

void exigir(const Indice& i, const std::string& consulta, const std::string& quien, int dentroDe = 3) {
    const int p = puesto(i, consulta, quien);
    if (p >= 0 && p < dentroDe) return;
    ++g_fallos;
    std::printf("  FALLO: \"%s\" no trajo %s entre los primeros %d (puesto %d)\n", consulta.c_str(),
                quien.c_str(), dentroDe, p);
    const std::vector<Resultado> r = i.buscar(consulta, 5);
    for (const Resultado& x : r) std::printf("         trajo: %s\n", i.doc(x.doc).id.c_str());
}

} // namespace

/// CUANTO TARDA. La busqueda tiene que ser instantanea y no «rapida»: corre
/// ANTES de que el modelo empiece a pensar, en el camino de un operador que
/// acaba de gritar «hay acople» con el aire encendido. Si costara decenas de
/// milisegundos habria que empezar a decidir cuando vale la pena buscar, y esa
/// decision no la queremos tener.
///
/// Se mide con las frases REACTIVAS, que son las peores: varias palabras
/// comunes, sin el nombre de ninguna herramienta.
void velocidad(const Indice& i) {
    static const char* kReactivas[] = {
        "acopla el microfono",
        "hay acople",
        "se esta acoplando el audio, cortalo",
        "suena un pitido horrible",
        "esta saturando el audio",
        "el invitado no se escucha",
        "no se escucha nada al aire",
        "se escucha un zumbido de fondo",
        "el microfono queda abierto y se escucha todo",
    };
    constexpr int kVueltas = 200;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t resultados = 0;
    for (int v = 0; v < kVueltas; ++v)
        for (const char* q : kReactivas) resultados += i.buscar(q, 8).size();
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const int cuantas = kVueltas * static_cast<int>(std::size(kReactivas));
    const double porBusqueda = us / cuantas;
    std::printf("  velocidad: %.0f us por busqueda (%d busquedas reactivas, %zu resultados)\n",
                porBusqueda, cuantas, resultados);
    // Tope generoso a proposito: lo que se vigila es que no se vaya a otro
    // orden de magnitud, no el numero exacto de esta maquina.
    if (porBusqueda > 2000.0) {
        std::printf("FALLO: la busqueda dejo de ser instantanea (%.0f us)\n", porBusqueda);
        ++g_fallos;
    }
    if (resultados == 0) {
        std::printf("FALLO: ninguna frase reactiva devolvio nada\n");
        ++g_fallos;
    }
}

int main() {
    const Indice i = armarIndice();
    velocidad(i);

    // --- Las que fallaban antes ---------------------------------------------
    exigir(i, "poner musica", "media_use");
    exigir(i, "silenciar a alguien", "audio_set");
    exigir(i, "mutear un microfono", "audio_set");

    // --- Con las palabras de una persona, no las del codigo -----------------
    exigir(i, "echar a un invitado de la sala", "participant_kick");
    exigir(i, "dejar entrar a alguien que esta esperando", "participant_admit");
    exigir(i, "salir al aire", "stream_start");
    exigir(i, "bajar del aire", "stream_stop");
    exigir(i, "empezar a grabar", "record_start");
    exigir(i, "que se esta viendo ahora", "capture_program");
    exigir(i, "cortar a otra camara", "scene_switch");
    exigir(i, "poner el zocalo con el nombre", "rotulo_vincular");
    exigir(i, "cuanto tiempo le queda al que habla", "speaker_time");
    exigir(i, "avisame cuando entre alguien", "alert_create");
    exigir(i, "mandar el qr a la tele", "pantalla_mostrar");
    exigir(i, "el link para invitar gente", "room_link");
    exigir(i, "ver que paso, hubo un error", "log_leer");
    exigir(i, "los graves suenan mal", "audio_eq");
    exigir(i, "se escucha desfasado de la imagen", "audio_sincro");
    exigir(i, "delegar una tarea", "agente_encargar");
    exigir(i, "preguntarle algo a la persona de cabina", "operator_ask");
    exigir(i, "fundido entre escenas", "transition_set");
    exigir(i, "camara por red", "ndi_sources", 5);
    exigir(i, "imprimir un codigo qr", "qr_exportar");
    exigir(i, "que televisores hay", "pantalla_listar");
    exigir(i, "el televisor esta realmente mostrando algo", "superficie_listar", 4);

    // --- Como se queja un operador, que no nombra la herramienta ------------
    // Una persona en cabina no dice «audio_set»: dice que algo no anda. Estas
    // son de esa clase, y son las que se le escapaban al buscador.
    exigir(i, "el invitado no se escucha", "audio_set");
    exigir(i, "no tengo sonido", "audio_set");
    exigir(i, "cuanta gente esta esperando para entrar", "room_status");
    exigir(i, "grabar lo que estamos haciendo", "record_start");
    exigir(i, "poner el nombre de alguien en pantalla", "rotulo_vincular");
    exigir(i, "se ve todo negro", "capture_program");

    // --- La bateria del corpus compartido -----------------------------------
    {
        bool ok = true;
        const std::vector<Caso> casos = leerCorpus(BA_CORPUS_PREGUNTAS, ok);
        if (!ok) ++g_fallos;
        const Indice ic = armarIndiceCon(casos);
        int en1 = 0;
        int en3 = 0;
        int en8 = 0;
        std::vector<std::string> perdidas;
        for (const Caso& c : casos) {
            const int p = puestoDeAlguna(ic, c, 8);
            if (p == 0) ++en1;
            if (p >= 0 && p < 3) ++en3;
            if (p >= 0) ++en8;
            else perdidas.push_back(c.pregunta);
        }
        const int n = static_cast<int>(casos.size());
        std::printf("  bateria (%d preguntas): primera %d/%d | primeras 3 %d/%d | primeras 8 %d/%d\n",
                    n, en1, n, en3, n, en8, n);
        for (const std::string& q : perdidas) std::printf("    no la encuentra: \"%s\"\n", q.c_str());
        // Las que llegan pero NO primeras: no son un fallo —el modelo recibe
        // las ocho— pero cuanto mas arriba, mejor elige. Es la lista de trabajo
        // para las etiquetas.
        for (const Caso& c : casos) {
            const int p = puestoDeAlguna(ic, c, 8);
            if (p > 0) std::printf("    puesto %d: \"%s\"\n", p + 1, c.pregunta.c_str());
        }
        // El piso es «entre las primeras 8», porque ESO es lo que se le manda
        // al modelo. Si no esta ahi, el modelo no puede elegirla: no la vio.
        CHECK(n >= 50, "el corpus tiene suficientes preguntas como para medir algo");
        CHECK(en8 == n, "todas las preguntas encuentran su herramienta entre las primeras 8");
    }

    // --- El nombre exacto siempre gana --------------------------------------
    for (const auto& [nombre, titulo] : kHerramientas) {
        (void)titulo;
        const int p = puesto(i, nombre, nombre);
        if (p != 0) {
            ++g_fallos;
            std::printf("  FALLO: buscar \"%s\" no la pone primera (puesto %d)\n", nombre, p);
        }
    }

    // --- Prefijos: quien busca no sabe como conjugamos ----------------------
    CHECK(puesto(i, "silenci", "audio_set") >= 0, "un prefijo encuentra la palabra entera");
    CHECK(puesto(i, "grabaci", "record_start") >= 0, "prefijo de grabacion");
    // Pero uno muy corto no puede enganchar con todo.
    CHECK(i.buscar("es", 12).empty(), "un prefijo de dos letras no trae media app");

    // --- Todas etiquetadas ---------------------------------------------------
    // Sin esto la tabla se despega del codigo sin que nadie se entere: la
    // herramienta nueva simplemente no aparece nunca en una busqueda, que es
    // el tipo de falla que no se nota.
    for (const auto& [nombre, titulo] : kHerramientas) {
        (void)titulo;
        if (etiquetasDe(nombre).empty()) {
            ++g_fallos;
            std::printf("  FALLO: %s no tiene etiquetas (ver McpEtiquetas.cpp)\n", nombre);
        }
    }

    // --- Las palabras que estan en todos lados no cuentan --------------------
    // "de", "el", "una" salian en el `porque` de casi todo y ademas inflaban
    // el bono por cubrir varias palabras de la consulta: cubrir "de" no es
    // cubrir nada. Se descartan por FRECUENCIA y no con una lista escrita a
    // mano, asi que una palabra que hoy distingue y mañana esta en todos lados
    // deja de contar sola.
    //
    // Ojo con como se prueba esto: en un corpus chico un articulo es RARO, asi
    // que el filtro no tiene por que tocarlo, y esta bien que no lo toque. Por
    // eso el umbral se prueba aparte, con un corpus donde el termino de verdad
    // esta en todos lados. Aca solo se exige lo que importa del otro lado: que
    // los articulos no den vuelta el orden.
    exigir(i, "sacar a una persona de la sala", "participant_kick");
    {
        Indice t;
        for (int k = 0; k < 10; ++k) {
            Documento d;
            d.id = "doc" + std::to_string(k);
            d.nombre = d.id;
            // "comun" esta en los diez; "raro" en uno solo.
            d.cuerpo = std::string("comun comun ") + (k == 3 ? "raro" : "otro");
            t.agregar(std::move(d));
        }
        t.armar();
        CHECK(t.buscar("comun", 10).empty(), "un termino que esta en todos no trae nada");
        const std::vector<Resultado> r = t.buscar("comun raro", 10);
        CHECK(r.size() == 1 && t.doc(r[0].doc).id == "doc3", "manda el termino que distingue");
        CHECK(r.size() == 1 && r[0].porque.size() == 1 && r[0].porque[0] == "raro",
              "y el motivo es ese, no el que esta en todos lados");
    }

    // --- Una consulta sin sentido no inventa ---------------------------------
    CHECK(i.buscar("xyzzy plugh", 12).empty(), "lo que no existe no trae nada");
    CHECK(i.buscar("", 12).empty(), "una consulta vacia no trae nada");

    if (g_fallos == 0) {
        std::printf("test_mcp_busqueda: OK\n");
        return 0;
    }
    std::printf("test_mcp_busqueda: %d fallo(s)\n", g_fallos);
    return 1;
}
