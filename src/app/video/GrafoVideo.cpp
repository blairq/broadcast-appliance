// src/app/video/GrafoVideo.cpp — ver GrafoVideo.hpp

#include "video/GrafoVideo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace ba::app::video {

using json = nlohmann::json;

namespace {

/// Grilla del editor: columnas de 220 (tarjetas de 180 y lugar para el cable), paso 20.
constexpr int kColumna = 220;

bool esFiltro(const std::string& t) {
    return t == "color" || t == "croma" || t == "recorte" || t == "obs" || t == "gst" || t == "retardo" ||
           t == "rostros" || t == "normalizar" || t == "sincro" || t == "superres" || t == "ptz" || t == "seguir";
}

json fabrica(const std::string& tipo) {
    if (tipo == "color") return {{"brillo", 0.0}, {"contraste", 1.0}, {"saturacion", 1.0}, {"gamma", 1.0}};
    if (tipo == "croma")
        return {{"clave", 0x00B140}, {"similitud", 0.4}, {"suavidad", 0.08}, {"derrame", 0.1}};
    if (tipo == "recorte")
        return {{"izquierda", 0.0}, {"arriba", 0.0},    {"derecha", 0.0},
                {"abajo", 0.0},     {"espejoH", false}, {"espejoV", false}};
    if (tipo == "salida") return {{"nombre", ""}};
    // `ms` (lo que se mide) manda sobre `cuadros` si es > 0.
    if (tipo == "retardo") return {{"cuadros", 0}, {"ms", 0}};
    // Una fuente que declaró mal su color: cómo la decodificó libobs (lo
    // declarado) y cómo es en verdad. `desentrelazar` se aplica a la fuente
    // (en libobs, que tiene los campos): no | descartar | retro | mezclar |
    // lineal | yadif | yadif2x; `campo`: arriba | abajo.
    if (tipo == "normalizar")
        return {{"matriz_declarada", "709"}, {"rango_declarado", "limitado"}, {"matriz_real", "709"},
                {"rango_real", "limitado"},  {"desentrelazar", "no"},         {"campo", "arriba"}};
    // La huella para medir desfases (la pone video_sincronia y la saca al terminar).
    if (tipo == "sincro") return {{"auto", false}};
    // El elemento barostros (src/gst-plugins/rostros). `uso`: para qué la usa
    // Personas (mirar | autoalta | puerta | seguir; "" = nadie, la puso el
    // usuario); `auto`: la puso Personas sola (y la saca cuando no la necesita).
    // `fps`: análisis por segundo; `reconocer`: el vector de cada cara (Personas
    // lo pide siempre que la usa); `detector`: auto | vision | yunet. EL OVERLAY:
    // `marcar`, `estilo` (recuadro | esquinas | relleno | tapar | pixelar),
    // `color` (ARGB), `grosor`, `texto` (plantilla: {nombre} {cargo} {id}
    // {confianza} {n}), `texto_posicion` (arriba | abajo), `texto_tamano`.
    if (tipo == "rostros")
        return {{"uso", ""},           {"auto", false},          {"activo", true},       {"fps", 5.0},
                {"reconocer", false},  {"detector", "auto"},     {"confianza", 0.7},     {"marcar", false},
                {"estilo", "recuadro"}, {"color", 0xFFFFC83CLL}, {"grosor", 4},          {"texto", ""},
                {"texto_posicion", "arriba"}, {"texto_tamano", 0.035}};
    // Superresolución x2 (el plugin de GStreamer basuperres): `motor` auto | apple |
    // fsr | onnx (con `modelo`, la ruta del .onnx), `nitidez` (FSR), `proveedor`
    // (onnx: auto | cpu | coreml | dml), `activo`. Lo que la sigue trabaja al doble.
    // `plugin`: el id de un plugin de superresolución (video/SuperRes: el catálogo y
    // los instalados); la app lo traduce al motor y el modelo de ESTA máquina al
    // mandar el grafo (CoreML en la Mac, ONNX/DirectML o TensorRT en Windows…).
    if (tipo == "superres")
        return {{"plugin", ""},        {"motor", "auto"}, {"modelo", ""},   {"escala_red", 2},
                {"nitidez", 0.2},      {"proveedor", "auto"}, {"activo", true}, {"esperar", 0},
                {"unidades", "auto"}}; // CoreML: auto (Neural Engine), ane, gpu, cpu
    // El PTZ digital (baptz): uno por entrada, se crea solo y no se quita (se
    // apaga con `activo`); va donde se lo ponga. Lo maneja Cámaras PTZ.
    if (tipo == "ptz") return {{"activo", true}};
    // El seguimiento (baseguir): lee las caras de una tarjeta Rostros que esté
    // antes y decide el encuadre. `objetivo`: "" (nadie) | "id:N" | "persona:<id>"
    // | "x,y" (la cara más cercana a ese punto) | "mayor" | "centro".
    // `zoom`: 0 = no lo toca; >0 = el zoom que busca; `aire` (lugar arriba de la
    // cabeza), `suavidad` (s), `zona_muerta`, `perdida_s` (cuánto espera).
    if (tipo == "seguir")
        return {{"activo", true}, {"auto", false}, {"objetivo", ""},     {"zoom", 0.0},       {"tamano_cara", 0.0},
                {"aire", 0.12},   {"suavidad", 0.6},   {"zona_muerta", 0.04}, {"perdida_s", 3.0}};
    if (tipo == "obs") return {{"filtro", ""}, {"ajustes", json::object()}};
    // Un módulo de GStreamer: el elemento y sus propiedades (lo que cambia de fábrica).
    // `esperar` (ms): 0 entrega el cuadro anterior (1 cuadro de latencia, nunca
    // frena el video); más espera el de ahora (sin latencia, puede frenar).
    if (tipo == "gst") return {{"elemento", ""}, {"propiedades", json::object()}, {"esperar", 0}};
    // Un controlador: el molde de una tarjeta (`tarjeta` = su tipo, `params` =
    // sus parámetros) que manejan todas las tarjetas conectadas a su `ctl`.
    if (tipo == "control") return {{"tarjeta", ""}, {"params", json::object()}};
    // Por puerto: {"in_0": {x, y, ancho, alto, opacidad, orden}} (lo ejecuta el estudio).
    // `nombre`: la fuente con la que se pone en las escenas. Sin capas: mosaico.
    if (tipo == "compositor")
        return {{"ancho", 0}, // 0: el tamaño del lienzo del estudio
                {"alto", 0},
                {"fondo", 0xFF000000LL},
                {"nombre", ""},
                {"capas", json::object()}};
    if (tipo == "entrada" || tipo == "escena") return {{"fuente", ""}};
    return json::object();
}

std::string titulo(const std::string& tipo) {
    if (tipo == "entrada") return "Entrada";
    if (tipo == "escena") return "A la escena";
    if (tipo == "color") return "Color";
    if (tipo == "croma") return "Croma";
    if (tipo == "recorte") return "Recorte y espejo";
    if (tipo == "salida") return "Salida";
    if (tipo == "retardo") return "Retardo";
    if (tipo == "normalizar") return "Normalizar";
    if (tipo == "sincro") return "Sincronía";
    if (tipo == "rostros") return "Rostros";
    if (tipo == "ptz") return "PTZ";
    if (tipo == "seguir") return "Seguimiento";
    if (tipo == "obs") return "Filtro";
    if (tipo == "gst") return "Módulo";
    if (tipo == "superres") return "Superresolución";
    if (tipo == "control") return "Controlador";
    if (tipo == "compositor") return "Compositor";
    return tipo;
}

/// Un id legible y estable a partir del nombre de la entrada (sin acentos ni espacios).
std::string slug(const std::string& original) {
    // Las letras con tilde, a su letra (UTF-8: «Cámara» -> «camara»).
    static const std::pair<const char*, char> kTildes[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string s = original;
    for (const auto& [con, sin] : kTildes)
        for (size_t p = s.find(con); p != std::string::npos; p = s.find(con, p))
            s.replace(p, std::strlen(con), 1, sin);
    std::string out;
    bool guion = false;
    for (unsigned char c : s) {
        // Los bytes de un acento (UTF-8) se saltean ANTES de preguntar nada:
        // con el locale de la app, isalnum/tolower los toman por Latin-1 y
        // los cambian, y el id queda con UTF-8 roto (el json se cae al guardar).
        if (c >= 0x80) continue;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            out += static_cast<char>(c);
            guion = false;
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
            guion = false;
        } else if (!guion && !out.empty()) {
            out += '-';
            guion = true;
        }
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "entrada" : out;
}

/// Mezcla `b` en `a`: los objetos clave por clave (a cualquier profundidad),
/// lo demás se reemplaza, y un null saca la clave (como JSON merge patch).
void mezclar(json& a, const json& b) {
    if (!a.is_object() || !b.is_object()) {
        a = b;
        return;
    }
    for (auto it = b.begin(); it != b.end(); ++it) {
        if (it.value().is_null())
            a.erase(it.key());
        else
            mezclar(a[it.key()], it.value());
    }
}

int indicePedido(const std::string& puerto) {
    if (puerto.rfind("in_", 0) != 0 || puerto.size() < 4) return -1;
    for (size_t i = 3; i < puerto.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(puerto[i]))) return -1;
    return std::stoi(puerto.substr(3));
}

} // namespace

namespace {

/// Un parámetro documentado: clave, tipo (numero | entero | texto | si_no |
/// opcion | color), qué hace y, según el tipo, rango u opciones.
json param(const char* clave, const char* tipo, const char* descripcion, json extra = json::object()) {
    json p{{"clave", clave}, {"tipo", tipo}, {"descripcion", descripcion}};
    for (auto it = extra.begin(); it != extra.end(); ++it) p[it.key()] = it.value();
    return p;
}

} // namespace

std::string GrafoVideo::ayudaTextoOverlay() {
    return "EL TEXTO DEL OVERLAY (tarjeta Rostros, parámetro `texto`) es una plantilla: se escribe lo que se quiera y "
           "estas marcas se reemplazan por lo de cada cara:\n"
           "  {nombre}     quién es (lo pone Personas cuando la reconoce; vacío si no)\n"
           "  {cargo}      su cargo, de su ficha en Personas\n"
           "  {id}         el número de la cara (se conserva mientras se mueve)\n"
           "  {confianza}  qué tan segura está la detección («91 %»)\n"
           "  {n}          cuántas caras hay en el cuadro\n"
           "Lo que queda vacío no deja separadores colgando: «{nombre} — {cargo}» sin cargo muestra solo el nombre, y "
           "si no queda nada no hay cartel. Ejemplos: «{nombre}», «{nombre} — {cargo}», «Cara {id}», «{confianza}», "
           "«{nombre} #{id}». `texto_posicion`: arriba | abajo de la cara (si arriba no entra, va abajo); "
           "`texto_tamano`: fracción del alto del video (0,035 = 3,5 %); el color es el de la marca (`color`). Vacío = sin texto.";
}

json GrafoVideo::parametrosDe(const std::string& tipo) {
    json l = json::array();
    if (tipo == "rostros") {
        l.push_back(param("activo", "si_no", "Analizar; si no, la imagen solo pasa."));
        l.push_back(param("fps", "numero", "Análisis por segundo (la imagen pasa siempre a su ritmo).", {{"min", 0.5}, {"max", 30}}));
        l.push_back(param("reconocer", "si_no", "Sacar el vector de cada cara para la libreta de Personas (lo prende sola Personas cuando usa la cámara)."));
        l.push_back(param("detector", "opcion", "Quién encuentra las caras: auto (Apple Vision en la Mac si no hace falta reconocer; si no, YuNet), vision o yunet.", {{"opciones", {"auto", "vision", "yunet"}}}));
        l.push_back(param("confianza", "numero", "Descarta las detecciones dudosas.", {{"min", 0.1}, {"max", 1}}));
        l.push_back(param("marcar", "si_no", "Dibujar el overlay sobre cada cara."));
        l.push_back(param("estilo", "opcion", "Cómo se marca: recuadro, esquinas, relleno (translúcido), tapar (color sólido) o pixelar; tapar y pixelar sirven para no mostrar una cara.", {{"opciones", {"recuadro", "esquinas", "relleno", "tapar", "pixelar"}}}));
        l.push_back(param("color", "color", "Color de la marca y del texto (ARGB, 0xFFFFC83C de fábrica)."));
        l.push_back(param("grosor", "entero", "Grosor del borde en píxeles del video.", {{"min", 1}, {"max", 40}}));
        l.push_back(param("texto", "texto", "Plantilla del texto sobre cada cara: {nombre} {cargo} {id} {confianza} {n} (ver la ayuda del texto)."));
        l.push_back(param("texto_posicion", "opcion", "Dónde va el texto.", {{"opciones", {"arriba", "abajo"}}}));
        l.push_back(param("texto_tamano", "numero", "Alto del texto, en fracción del alto del video.", {{"min", 0.01}, {"max", 0.2}}));
        l.push_back(param("uso", "texto", "Para qué la usa Personas (mirar, autoalta, puerta, seguir); vacío = la puso el usuario. Lo maneja la app."));
    } else if (tipo == "seguir") {
        l.push_back(param("activo", "si_no", "Seguir; si no, pasa sin hacer nada."));
        l.push_back(param("objetivo", "texto", "A quién: vacío (nadie) | id:N (la cara número N) | persona:X (id de su ficha o su nombre) | x,y (la cara más cercana a ese punto de la toma, 0..1) | mayor (la más grande) | centro (la más cercana al centro)."));
        l.push_back(param("zoom", "numero", "El zoom que busca; 0 = no tocarlo.", {{"min", 0}, {"max", 8}}));
        l.push_back(param("tamano_cara", "numero", "Qué parte del alto ocupa la cara (calcula el zoom); 0 = no ajustarlo.", {{"min", 0}, {"max", 1}}));
        l.push_back(param("aire", "numero", "Cuánto queda la cara arriba del centro, en fracción del alto (deja aire arriba de la cabeza).", {{"min", -0.5}, {"max", 0.5}}));
        l.push_back(param("suavidad", "numero", "Constante de tiempo del movimiento, en segundos.", {{"min", 0.05}, {"max", 10}}));
        l.push_back(param("zona_muerta", "numero", "Lo que se puede correr la cara sin mover nada (que no tiemble).", {{"min", 0}, {"max", 0.5}}));
        l.push_back(param("perdida_s", "numero", "Segundos que la busca donde estaba antes de soltarla.", {{"min", 0}, {"max", 60}}));
    } else if (tipo == "ptz") {
        l.push_back(param("activo", "si_no", "Encender o apagar el PTZ digital de esta entrada (también en Cámaras PTZ)."));
    } else if (tipo == "color") {
        l.push_back(param("brillo", "numero", "Suma luz (-1..1).", {{"min", -1}, {"max", 1}}));
        l.push_back(param("contraste", "numero", "1 = sin cambio.", {{"min", 0}, {"max", 2}}));
        l.push_back(param("saturacion", "numero", "0 = blanco y negro, 1 = sin cambio.", {{"min", 0}, {"max", 2}}));
        l.push_back(param("gamma", "numero", "1 = sin cambio.", {{"min", 0.2}, {"max", 3}}));
    } else if (tipo == "croma") {
        l.push_back(param("clave", "color", "El color de fondo a sacar (RGB, 0x00B140 = verde)."));
        l.push_back(param("similitud", "numero", "Cuánto se parece un color para irse.", {{"min", 0}, {"max", 1}}));
        l.push_back(param("suavidad", "numero", "Suavidad del borde.", {{"min", 0}, {"max", 1}}));
        l.push_back(param("derrame", "numero", "Quita el reflejo del color en la persona.", {{"min", 0}, {"max", 1}}));
    } else if (tipo == "recorte") {
        for (const char* k : {"izquierda", "arriba", "derecha", "abajo"})
            l.push_back(param(k, "numero", "Cuánto se recorta de ese lado (fracción del cuadro; queda transparente).", {{"min", 0}, {"max", 0.5}}));
        l.push_back(param("espejoH", "si_no", "Espejo horizontal."));
        l.push_back(param("espejoV", "si_no", "Espejo vertical."));
    } else if (tipo == "retardo") {
        l.push_back(param("cuadros", "entero", "Cuadros de atraso.", {{"min", 0}, {"max", 120}}));
        l.push_back(param("ms", "entero", "Milisegundos de atraso (manda sobre `cuadros` si es > 0)."));
    } else if (tipo == "superres") {
        l.push_back(param("plugin", "texto", "El id de un plugin de superresolución (video_superres_plugins)."));
        l.push_back(param("motor", "opcion", "auto, apple, fsr u onnx.", {{"opciones", {"auto", "apple", "fsr", "onnx"}}}));
        l.push_back(param("modelo", "texto", "Con motor onnx: la ruta del .onnx."));
        l.push_back(param("escala_red", "entero", "Cuánto agranda el modelo (2 = x2)."));
        l.push_back(param("nitidez", "numero", "Nitidez (FSR).", {{"min", 0}, {"max", 2}}));
        l.push_back(param("proveedor", "opcion", "ONNX: dónde corre.", {{"opciones", {"auto", "cpu", "coreml", "dml"}}}));
        l.push_back(param("unidades", "opcion", "CoreML: dónde corre (auto = Neural Engine).", {{"opciones", {"auto", "ane", "gpu", "cpu"}}}));
        l.push_back(param("esperar", "entero", "ms: 0 entrega el cuadro anterior (nunca frena); más espera el de ahora."));
        l.push_back(param("activo", "si_no", "Encender o apagar."));
    } else if (tipo == "salida") {
        l.push_back(param("nombre", "texto", "El nombre de la fuente que se publica para las escenas."));
    }
    else if (tipo == "compositor") {
        l.push_back(param("ancho", "entero", "Ancho del lienzo; 0 = el del estudio."));
        l.push_back(param("alto", "entero", "Alto del lienzo; 0 = el del estudio."));
        l.push_back(param("fondo", "color", "Color de fondo (ARGB, 0xFF000000 = negro)."));
        l.push_back(param("nombre", "texto", "La fuente con la que se pone en las escenas."));
        l.push_back(param("capas", "texto", "Por puerto, {\"in_N\": {x, y, ancho, alto (0..1 del lienzo), opacidad, orden, ajuste encajar|llenar}}; sin capas, mosaico."));
    } else if (tipo == "gst") {
        l.push_back(param("elemento", "texto", "El módulo de GStreamer (video_modulos)."));
        l.push_back(param("propiedades", "texto", "Sus propiedades, con los nombres de GStreamer (video_filtros las lista)."));
        l.push_back(param("esperar", "entero", "ms: 0 entrega el cuadro anterior; más espera el de ahora."));
    }
    return l;
}

json GrafoVideo::eventosDe(const std::string& tipo) {
    // Lo que cada tarjeta manda y escucha, todo con mecanismos estándar de
    // GStreamer (eventos propios, mensajes al bus, metas). Se ve en Racks.
    if (tipo == "rostros")
        return {{"manda",
                 {"evento «ba/rostros» hacia abajo y hacia arriba (json con las caras: id, x, y, ancho, alto 0..1, "
                  "confianza, nombre, cargo, persona, toma; ventana del PTZ, video, detector, ms, pts)",
                  "meta GstVideoRegionOfInterestMeta «face» en cada cuadro (parámetro «ba-rostro»)",
                  "mensaje de elemento «ba-rostros» al bus (el mismo json y el cuadro analizado)"}},
                {"escucha",
                 {"«ba/ptz» (la ventana del PTZ: para dar cada cara también en la toma)",
                  "«ba/control» con rostros_etiquetas (quién es cada cara, lo manda Personas)"}}};
    if (tipo == "seguir")
        return {{"manda",
                 {"evento «ba/encuadre» hacia arriba y hacia abajo (x, y, zoom, suavidad, id; o soltar=true)",
                  "mensaje de elemento «ba-seguir» al bus (estado, a quién sigue, el encuadre)"}},
                {"escucha",
                 {"«ba/rostros» venga de antes o de después", "«ba/ptz» (dónde está la ventana)",
                  "«ba/control» con seguir_objetivo"}}};
    if (tipo == "ptz")
        return {{"manda", {"evento «ba/ptz» hacia abajo cuando cambia la ventana (x, y, zoom, moviendo)"}},
                {"escucha", {"«ba/encuadre» venga de antes o de después (lo sigue 1,5 s; si no llega más, suelta)"}}};
    return json::object();
}

json GrafoVideo::catalogo() {
    json out = json::array();
    const std::tuple<const char*, const char*, const char*> tipos[] = {
        {"color", "Brillo, contraste, saturación y gamma.", "entrada"},
        {"croma", "Saca un fondo de color (verde de fábrica): queda transparente.", "entrada"},
        {"recorte", "Recorta los bordes (quedan transparentes) y da vuelta la imagen.", "entrada"},
        {"rostros",
         "Encuentra, sigue y reconoce caras (elemento barostros): mira la toma entera de la cámara, sin demorar "
         "la imagen. Con «marcar», un overlay sobre cada cara (recuadro, esquinas, relleno, tapar o pixelar) con "
         "un texto ({nombre}, {cargo}, {id}, {confianza}). Publica las caras en cada cuadro (metas de región de "
         "GStreamer) y en eventos «ba/rostros» para el resto de la cadena; Personas y el seguimiento del PTZ la "
         "usan.",
         "entrada"},
        {"ptz",
         "Pan, tilt y zoom digital (elemento baptz): uno por entrada, va donde se lo ponga en la cadena. Lo manejan "
         "Cámaras PTZ, el joystick y los presets; sigue el encuadre que le pida una tarjeta Seguimiento (evento "
         "«ba/encuadre», venga de antes o de después) y publica su ventana («ba/ptz»).",
         "entrada"},
        {"seguir",
         "Sigue a una cara (elemento baseguir, no toca la imagen): lee las caras de una tarjeta Rostros (antes o "
         "después), elige a quién (`objetivo`) y le manda el encuadre al PTZ (antes o después) con eventos de "
         "GStreamer. Con una cámara motorizada, la app la mueve con pulsos.",
         "entrada"},
        {"normalizar",
         "Corrige una fuente que declara mal su color (BT.601/709, rango limitado/completo) y la "
         "desentrelaza (en la fuente, donde están los campos).",
         "entrada"},
        {"retardo",
         "Atrasa la imagen (`ms`, o `cuadros`): para sincronizar una cámara con otra o con el audio.",
         "entrada"},
        {"superres",
         "Duplica la resolución (Apple en macOS 26, FSR 1 en cualquier GPU, o una red ONNX entrenada con la "
         "cámara): lo que la sigue trabaja al doble y la escena la recibe grande. Compensa el audio solo.",
         "entrada"},
        {"salida", "Publica la imagen de ese punto como una fuente con nombre, para cualquier escena.",
         "entrada"},
        {"compositor", "Junta varias entradas en una imagen (mosaico, imagen en imagen). Es del estudio.",
         "estudio"},
        {"control",
         "Maneja con UN ajuste las tarjetas iguales de varias entradas (la misma corrección de color "
         "en todas las cámaras): un cable de control a cada una.",
         "estudio"},
    };
    for (const auto& [t, d, a] : tipos)
        out.push_back(
            {{"tipo", t}, {"titulo", titulo(t)}, {"descripcion", d}, {"params", fabrica(t)}, {"parametros", parametrosDe(t)},
             {"eventos", eventosDe(t)}, {"ambito", a}});
    for (json& e : out)
        if (e.value("tipo", std::string()) == "rostros") e["texto_overlay"] = ayudaTextoOverlay();
    return out;
}

bool GrafoVideo::tipoValido(const std::string& t) {
    return esFiltro(t) || t == "salida" || t == "compositor" || t == "control";
}

std::string GrafoVideo::idEntrada(const std::string& fuente) { return slug(fuente) + ".entrada"; }
std::string GrafoVideo::idEscena(const std::string& fuente) { return slug(fuente) + ".escena"; }

const GrafoVideo::Nodo* GrafoVideo::nodo(const std::string& id) const {
    for (const auto& n : nodos_)
        if (n.id == id) return &n;
    return nullptr;
}

std::vector<GrafoVideo::Cable> GrafoVideo::entrantes(const std::string& id) const {
    std::vector<Cable> out;
    for (const auto& c : cables_)
        if (c.hacia == id) out.push_back(c);
    return out;
}

std::vector<GrafoVideo::Cable> GrafoVideo::salientes(const std::string& id) const {
    std::vector<Cable> out;
    for (const auto& c : cables_)
        if (c.desde == id) out.push_back(c);
    return out;
}

std::vector<GrafoVideo::Puerto> GrafoVideo::puertosEntrada(const Nodo& n) const {
    if (n.tipo == "entrada" || n.tipo == "control") return {};
    // Un filtro tiene además su entrada de control (de un controlador).
    if (esFiltro(n.tipo)) return {{"in", "video", false}, {"ctl", "control", false}};
    if (n.tipo != "compositor") return {{"in", "video", false}};
    // A pedido: los conectados, en orden, y uno libre al final.
    std::vector<int> usados;
    for (const auto& c : entrantes(n.id))
        if (indicePedido(c.entrada) >= 0) usados.push_back(indicePedido(c.entrada));
    std::sort(usados.begin(), usados.end());
    std::vector<Puerto> out;
    for (int i : usados)
        out.push_back({"in_" + std::to_string(i), "video", false});
    out.push_back({"in_" + std::to_string(usados.empty() ? 0 : usados.back() + 1), "video", true});
    return out;
}

std::vector<GrafoVideo::Puerto> GrafoVideo::puertosSalida(const Nodo& n) const {
    if (n.tipo == "escena" || n.tipo == "salida") return {};
    if (n.tipo == "control") return {{"ctl", "control", false}};
    return {{"out", "video", false}};
}

std::string GrafoVideo::idNuevo(const std::string& grupo, const std::string& tipo) const {
    const std::string base = (grupo.empty() ? std::string("estudio") : slug(grupo)) + "." + tipo;
    if (nodo(base) == nullptr) return base;
    for (int i = 2;; ++i) {
        const std::string id = base + "-" + std::to_string(i);
        if (nodo(id) == nullptr) return id;
    }
}

bool GrafoVideo::llega(const std::string& desde, const std::string& hasta) const {
    std::vector<std::string> pila{desde};
    std::set<std::string> visto;
    while (!pila.empty()) {
        const std::string n = pila.back();
        pila.pop_back();
        if (n == hasta) return true;
        if (!visto.insert(n).second) continue;
        for (const auto& c : cables_)
            if (c.desde == n) pila.push_back(c.hacia);
    }
    return false;
}

bool GrafoVideo::asegurarPtz(const std::string& fuente) {
    if (nodo(idEntrada(fuente)) == nullptr) return false;
    for (const auto& n : nodos_)
        if (n.grupo == fuente && n.tipo == "ptz") return false;
    std::string e;
    return !agregar("ptz", fuente, idEntrada(fuente), json::object(), &e).empty();
}

bool GrafoVideo::reubicar(const std::string& id, const std::string& despues, std::string* error) {
    Nodo* n = nullptr;
    for (auto& x : nodos_)
        if (x.id == id) n = &x;
    if (n == nullptr || !esFiltro(n->tipo)) {
        if (error) *error = "«" + id + "» no es una tarjeta que se pueda mover en la cadena";
        return false;
    }
    if (despues == id) return true;
    const std::string grupo = n->grupo;
    // Se saca de donde está (lo de antes sigue a lo de después)...
    std::vector<Cable> antes = entrantes(id);
    antes.erase(std::remove_if(antes.begin(), antes.end(), [](const Cable& c) { return c.entrada == "ctl"; }),
                antes.end());
    const std::vector<Cable> sale = salientes(id);
    if (antes.size() != 1) {
        if (error) *error = "«" + id + "» no está en una cadena simple (tiene " + std::to_string(antes.size()) + " entradas)";
        return false;
    }
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return (c.desde == id || c.hacia == id) && c.entrada != "ctl"; }),
                  cables_.end());
    for (const auto& c : sale) cables_.push_back({antes[0].desde, antes[0].salida, c.hacia, c.entrada});
    // ...y se intercala después de `despues` (vacío = justo antes de la escena).
    std::string desde = despues;
    if (desde.empty()) {
        desde = idEntrada(grupo);
        for (const auto& c : entrantes(idEscena(grupo))) desde = c.desde;
    }
    const Nodo* d = nodo(desde);
    if (d == nullptr || d->grupo != grupo || puertosSalida(*d).empty()) {
        // No se pudo: vuelve a donde estaba.
        cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                     [&](const Cable& c) { return c.desde == antes[0].desde && c.salida == antes[0].salida; }),
                      cables_.end());
        cables_.push_back(antes[0]);
        for (const auto& c : sale) cables_.push_back(c);
        if (error) *error = "no hay una tarjeta «" + desde + "» de esta entrada después de la cual ponerla";
        return false;
    }
    for (auto& c : cables_)
        if (c.desde == desde && c.entrada != "ctl") c.desde = id;
    cables_.push_back({desde, "out", id, "in"});
    n = nullptr;
    for (auto& x : nodos_)
        if (x.id == id) n = &x;
    n->x = d->x + kColumna;
    n->y = d->y;
    return true;
}

bool GrafoVideo::asegurarEntrada(const std::string& fuente) {
    const std::string ie = idEntrada(fuente), is = idEscena(fuente);
    bool creo = false;
    if (nodo(ie) == nullptr) {
        // Debajo de las que ya hay: cada entrada en su franja.
        int y = 0;
        for (const auto& n : nodos_)
            if (n.tipo == "entrada") y = std::max(y, n.y + 200);
        nodos_.push_back({ie, "entrada", fuente, fuente, 0, y, json{{"fuente", fuente}}});
        creo = true;
    }
    if (nodo(is) == nullptr) {
        const int y = nodo(ie)->y;
        nodos_.push_back({is, "escena", "A la escena", fuente, kColumna * 2, y, json{{"fuente", fuente}}});
        cables_.push_back({ie, "out", is, "in"});
        creo = true;
    }
    return creo;
}

std::vector<std::string> GrafoVideo::entradas() const {
    std::vector<std::string> out;
    for (const auto& n : nodos_)
        if (n.tipo == "entrada") out.push_back(n.grupo);
    return out;
}

std::string GrafoVideo::agregar(const std::string& tipo, const std::string& grupo, const std::string& despues,
                                const json& params, std::string* error) {
    if (!tipoValido(tipo)) {
        if (error) *error = "no existe la tarjeta «" + tipo + "»";
        return {};
    }
    if (tipo == "obs" && (!params.is_object() || params.value("filtro", std::string()).empty())) {
        if (error) *error = "falta qué filtro de OBS (`filtro`)";
        return {};
    }
    if (tipo == "gst" && (!params.is_object() || params.value("elemento", std::string()).empty())) {
        if (error) *error = "falta qué módulo de GStreamer (`elemento`)";
        return {};
    }
    if (tipo == "ptz")
        for (const auto& m : nodos_)
            if (m.grupo == grupo && m.tipo == "ptz") {
                if (error) *error = "«" + grupo + "» ya tiene su PTZ (" + m.id + "): se mueve con video_tarjeta_mover";
                return {};
            }
    Nodo n;
    n.tipo = tipo;
    n.etiqueta = titulo(tipo);
    n.params = fabrica(tipo);
    if (params.is_object())
        for (auto it = params.begin(); it != params.end(); ++it)
            n.params[it.key()] = it.value();
    // Un compositor o un controlador es del estudio: suelto, a la derecha de todo.
    if (tipo == "compositor" || tipo == "control") {
        n.grupo = "";
        n.id = idNuevo("", tipo);
        int x = 0;
        for (const auto& m : nodos_)
            x = std::max(x, m.x + kColumna);
        n.x = x;
        n.y = 0;
        nodos_.push_back(std::move(n));
        return nodos_.back().id;
    }
    if (grupo.empty() || nodo(idEntrada(grupo)) == nullptr) {
        if (error) *error = "«" + grupo + "» no es una entrada del grafo";
        return {};
    }
    std::string desde = despues;
    if (desde.empty()) {
        desde = idEntrada(grupo);
        for (const auto& c : entrantes(idEscena(grupo)))
            desde = c.desde;
    }
    const Nodo* d = nodo(desde);
    if (d == nullptr || puertosSalida(*d).empty()) {
        if (error) *error = "no hay una tarjeta «" + desde + "» de la que colgarla";
        return {};
    }
    n.grupo = grupo;
    n.id = idNuevo(grupo, tipo);
    n.x = d->x + kColumna;
    n.y = d->y + (tipo == "salida" ? 140 : 0);
    if (tipo != "salida") {
        // Intercalado: lo que salía de `desde` sale ahora de la nueva, y lo
        // que venía después en la misma fila se corre una columna.
        for (auto& c : cables_)
            if (c.desde == desde) c.desde = n.id;
        for (auto& m : nodos_)
            if (m.grupo == grupo && m.x >= n.x && m.y == n.y && m.id != desde) m.x += kColumna;
    }
    cables_.push_back({desde, "out", n.id, "in"});
    nodos_.push_back(std::move(n));
    return nodos_.back().id;
}

bool GrafoVideo::quitar(const std::string& id, std::string* error) {
    const Nodo* n = nodo(id);
    if (n == nullptr) {
        if (error) *error = "no existe la tarjeta «" + id + "»";
        return false;
    }
    if (n->tipo == "entrada" || n->tipo == "escena") {
        if (error) *error = "la entrada y la escena no se quitan";
        return false;
    }
    if (n->tipo == "ptz") {
        if (error) *error = "el PTZ de la entrada no se quita: se apaga (activo) o se mueve de lugar";
        return false;
    }
    // Un filtro (una entrada, una salida) se puentea: lo de antes sigue a lo de después.
    std::vector<Cable> antes = entrantes(id);
    const std::vector<Cable> despues = salientes(id);
    antes.erase(std::remove_if(antes.begin(), antes.end(), [](const Cable& c) { return c.entrada == "ctl"; }),
                antes.end());
    const bool puentear = esFiltro(n->tipo) && antes.size() == 1;
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return c.desde == id || c.hacia == id; }),
                  cables_.end());
    if (puentear)
        for (const auto& c : despues)
            cables_.push_back({antes[0].desde, antes[0].salida, c.hacia, c.entrada});
    nodos_.erase(std::remove_if(nodos_.begin(), nodos_.end(), [&](const Nodo& x) { return x.id == id; }),
                 nodos_.end());
    return true;
}

std::string GrafoVideo::conectar(const std::string& desde, const std::string& salida,
                                 const std::string& hacia, const std::string& entrada, std::string* error) {
    const Nodo* d = nodo(desde);
    const Nodo* h = nodo(hacia);
    if (d == nullptr || h == nullptr) {
        if (error) *error = "no existe «" + (d == nullptr ? desde : hacia) + "»";
        return {};
    }
    const auto ps = puertosSalida(*d);
    const std::string sal = salida.empty() ? (ps.empty() ? std::string() : ps[0].id) : salida;
    if (std::none_of(ps.begin(), ps.end(), [&](const Puerto& p) { return p.id == sal; })) {
        if (error)
            *error = "«" + d->etiqueta + "» no tiene salida" + (salida.empty() ? "" : " «" + salida + "»");
        return {};
    }
    const std::string tipoSal =
        std::find_if(ps.begin(), ps.end(), [&](const Puerto& p) { return p.id == sal; })->tipo;
    const auto pe = puertosEntrada(*h);
    std::string ent = entrada;
    if (ent.empty()) {
        // El primer puerto del mismo tipo (en un compositor, el libre).
        if (h->tipo == "compositor" && tipoSal == "video") ent = pe.back().id;
        for (const auto& p : pe)
            if (ent.empty() && p.tipo == tipoSal) ent = p.id;
    }
    const auto puerto = std::find_if(pe.begin(), pe.end(), [&](const Puerto& p) { return p.id == ent; });
    const bool valida = puerto != pe.end() || (h->tipo == "compositor" && indicePedido(ent) >= 0);
    if (!valida) {
        if (error)
            *error = "«" + h->etiqueta + "» no tiene entrada" +
                     (entrada.empty() ? (tipoSal == "control" ? " de control" : "") : " «" + entrada + "»");
        return {};
    }
    const std::string tipoEnt = puerto != pe.end() ? puerto->tipo : "video";
    if (tipoEnt != tipoSal) {
        if (error) *error = "un cable de " + tipoSal + " no va a una entrada de " + tipoEnt;
        return {};
    }
    // Un controlador maneja tarjetas de SU tipo (y del mismo filtro o módulo).
    if (tipoSal == "control") {
        const json& m = d->params;
        const std::string t = m.value("tarjeta", std::string());
        const bool igual = t == h->tipo &&
                           (t != "obs" || m["params"].value("filtro", std::string()) ==
                                              h->params.value("filtro", std::string())) &&
                           (t != "gst" || m["params"].value("elemento", std::string()) ==
                                              h->params.value("elemento", std::string()));
        if (!igual) {
            if (error)
                *error = "«" + d->etiqueta + "» maneja tarjetas de otro tipo que «" + h->etiqueta + "»";
            return {};
        }
    }
    if (desde == hacia || llega(hacia, desde)) {
        if (error) *error = "ese cable cerraría un círculo";
        return {};
    }
    // Una sola cosa por puerto de entrada: el cable nuevo reemplaza al que llegaba.
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return c.hacia == hacia && c.entrada == ent; }),
                  cables_.end());
    cables_.push_back({desde, sal, hacia, ent});
    return ent;
}

bool GrafoVideo::desconectar(const std::string& desde, const std::string& salida, const std::string& hacia,
                             const std::string& entrada) {
    const auto antes = cables_.size();
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) {
                                     return c.desde == desde && c.hacia == hacia &&
                                            (salida.empty() || c.salida == salida) &&
                                            (entrada.empty() || c.entrada == entrada);
                                 }),
                  cables_.end());
    return cables_.size() != antes;
}

bool GrafoVideo::ajustar(const std::string& id, const json& params, std::string* error) {
    for (auto& n : nodos_) {
        if (n.id != id) continue;
        if (!params.is_object()) return true;
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.key() == "etiqueta" && it.value().is_string()) {
                n.etiqueta = it.value().get<std::string>();
                continue;
            }
            // `local`: lo propio de una tarjeta manejada por un controlador.
            if (it.key() == "local" && it.value().is_object() && esFiltro(n.tipo)) {
                mezclar(n.params["local"], it.value());
                continue;
            }
            if (!n.params.contains(it.key())) {
                if (error) *error = "«" + n.etiqueta + "» no tiene «" + it.key() + "»";
                return false;
            }
            json& viejo = n.params[it.key()];
            // Los ajustes de un filtro de OBS y las capas de un compositor se
            // mezclan clave por clave (no se pisan enteros).
            if (viejo.is_object() && it.value().is_object()) {
                mezclar(viejo, it.value());
                continue;
            }
            if ((viejo.is_number() && !it.value().is_number()) ||
                (viejo.is_boolean() && !it.value().is_boolean()) ||
                (viejo.is_string() && !it.value().is_string())) {
                if (error) *error = "«" + it.key() + "» no es de ese tipo";
                return false;
            }
            viejo = it.value();
        }
        return true;
    }
    if (error) *error = "no existe la tarjeta «" + id + "»";
    return false;
}

void GrafoVideo::quitarEntrada(const std::string& fuente) {
    std::set<std::string> ids;
    for (const auto& n : nodos_)
        if (n.grupo == fuente) ids.insert(n.id);
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return ids.count(c.desde) || ids.count(c.hacia); }),
                  cables_.end());
    nodos_.erase(
        std::remove_if(nodos_.begin(), nodos_.end(), [&](const Nodo& n) { return ids.count(n.id) > 0; }),
        nodos_.end());
}

std::string GrafoVideo::renombrar(const std::string& id, const std::string& nombre) {
    const Nodo* n = nodo(id);
    if (n == nullptr || !n->grupo.empty() || nombre.empty()) return id;
    std::string nuevo = "estudio." + slug(nombre);
    if (nuevo == id) return id;
    for (int i = 2; nodo(nuevo) != nullptr; ++i)
        nuevo = "estudio." + slug(nombre) + "-" + std::to_string(i);
    for (auto& x : nodos_)
        if (x.id == id) x.id = nuevo;
    for (auto& c : cables_) {
        if (c.desde == id) c.desde = nuevo;
        if (c.hacia == id) c.hacia = nuevo;
    }
    return nuevo;
}

bool GrafoVideo::mover(const std::string& id, int x, int y) {
    for (auto& n : nodos_)
        if (n.id == id) {
            n.x = x;
            n.y = y;
            return true;
        }
    return false;
}

void GrafoVideo::limpiar(const std::string& grupo) {
    std::vector<std::string> sacar;
    for (const auto& n : nodos_)
        if (n.grupo == grupo && n.tipo != "entrada" && n.tipo != "escena") sacar.push_back(n.id);
    const std::string ie = idEntrada(grupo), is = idEscena(grupo);
    // También lo que entraba a su escena desde afuera.
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) {
                                     return c.hacia == is ||
                                            std::find(sacar.begin(), sacar.end(), c.desde) != sacar.end() ||
                                            std::find(sacar.begin(), sacar.end(), c.hacia) != sacar.end();
                                 }),
                  cables_.end());
    nodos_.erase(std::remove_if(nodos_.begin(), nodos_.end(),
                                [&](const Nodo& x) {
                                    return std::find(sacar.begin(), sacar.end(), x.id) != sacar.end();
                                }),
                 nodos_.end());
    if (nodo(ie) != nullptr && nodo(is) != nullptr) cables_.push_back({ie, "out", is, "in"});
}

bool GrafoVideo::trivial(const std::string& grupo) const {
    for (const auto& n : nodos_)
        if (n.grupo == grupo && n.tipo != "entrada" && n.tipo != "escena") return false;
    const std::string ie = idEntrada(grupo), is = idEscena(grupo);
    // Ni cables de su entrada hacia afuera (un compositor) ni de afuera a su escena.
    for (const auto& c : cables_)
        if ((c.desde == ie || c.hacia == is) && !(c.desde == ie && c.hacia == is)) return false;
    const auto e = entrantes(is);
    return e.size() == 1 && e[0].desde == ie;
}

std::vector<std::string> GrafoVideo::salidas() const {
    std::vector<std::string> out;
    for (const auto& n : nodos_)
        if (n.tipo == "salida" && !n.params.value("nombre", std::string()).empty())
            out.push_back(n.params.value("nombre", ""));
    return out;
}

json GrafoVideo::aJson() const {
    json ns = json::array(), cs = json::array();
    for (const auto& n : nodos_)
        ns.push_back({{"id", n.id},
                      {"tipo", n.tipo},
                      {"etiqueta", n.etiqueta},
                      {"grupo", n.grupo},
                      {"x", n.x},
                      {"y", n.y},
                      {"params", n.params}});
    for (const auto& c : cables_)
        cs.push_back({{"desde", c.desde}, {"salida", c.salida}, {"hacia", c.hacia}, {"entrada", c.entrada}});
    return {{"version", 2}, {"nodos", ns}, {"cables", cs}};
}

GrafoVideo GrafoVideo::desde(const json& j) {
    GrafoVideo g;
    if (!j.is_object()) return g;
    if (j.value("version", 0) == 2) {
        std::set<std::string> ids;
        for (const auto& n : j.value("nodos", json::array())) {
            Nodo x;
            x.id = n.value("id", std::string());
            x.tipo = n.value("tipo", std::string());
            if (x.id.empty() || ids.count(x.id) ||
                !(x.tipo == "entrada" || x.tipo == "escena" || tipoValido(x.tipo)))
                continue;
            x.etiqueta = n.value("etiqueta", titulo(x.tipo));
            x.grupo = n.value("grupo", std::string());
            x.x = n.value("x", 0);
            x.y = n.value("y", 0);
            x.params = fabrica(x.tipo);
            const json p = n.value("params", json::object());
            if (p.is_object())
                for (auto it = p.begin(); it != p.end(); ++it)
                    x.params[it.key()] = it.value();
            ids.insert(x.id);
            g.nodos_.push_back(std::move(x));
        }
        for (const auto& c : j.value("cables", json::array())) {
            std::string e;
            g.conectar(c.value("desde", std::string()), c.value("salida", std::string("out")),
                       c.value("hacia", std::string()), c.value("entrada", std::string("in")),
                       &e); // valida todo
        }
        return g;
    }
    // Formato de antes: un grafo por entrada, con `in`/`out` y la salida como
    // tarjeta de paso.
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) continue;
        const std::string fuente = it.key();
        g.asegurarEntrada(fuente);
        const int y0 = g.nodo(idEntrada(fuente))->y;
        const std::string pre = slug(fuente) + ".";
        const auto mapear = [&](const std::string& id) {
            return id == "in" ? idEntrada(fuente) : id == "out" ? idEscena(fuente) : pre + id;
        };
        for (const auto& n : it.value().value("nodos", json::array())) {
            const std::string tipo = n.value("tipo", std::string());
            if (tipo == "in" || tipo == "out") {
                g.mover(mapear(tipo), n.value("x", 0), y0 + n.value("y", 0));
                continue;
            }
            if (!tipoValido(tipo)) continue;
            Nodo x;
            x.id = mapear(n.value("id", std::string()));
            x.tipo = tipo;
            x.etiqueta = n.value("etiqueta", titulo(tipo));
            x.grupo = fuente;
            x.x = n.value("x", 0);
            x.y = y0 + n.value("y", 0);
            x.params = fabrica(tipo);
            const json p = n.value("params", json::object());
            if (p.is_object())
                for (auto q = p.begin(); q != p.end(); ++q)
                    x.params[q.key()] = q.value();
            g.nodos_.push_back(std::move(x));
        }
        g.cables_.erase(std::remove_if(g.cables_.begin(), g.cables_.end(),
                                       [&](const Cable& c) { return c.hacia == idEscena(fuente); }),
                        g.cables_.end());
        const json cs = it.value().value("cables", json::array());
        std::map<std::string, std::string> alimenta; // quién alimentaba cada nodo
        for (const auto& c : cs)
            alimenta[mapear(c.value("hacia", std::string()))] = mapear(c.value("desde", std::string()));
        for (const auto& c : cs) {
            std::string d = mapear(c.value("desde", std::string()));
            // Una salida ya no deja pasar: lo que salía de ella sale de lo que la alimentaba.
            const Nodo* nd = g.nodo(d);
            if (nd != nullptr && nd->tipo == "salida" && alimenta.count(d)) d = alimenta[d];
            std::string e;
            g.conectar(d, "out", mapear(c.value("hacia", std::string())), "in", &e);
        }
    }
    return g;
}

const GrafoVideo::Nodo* GrafoVideo::controlador(const std::string& id) const {
    for (const auto& c : cables_)
        if (c.hacia == id && c.entrada == "ctl") return nodo(c.desde);
    return nullptr;
}

json GrafoVideo::efectivos(const Nodo& n) const {
    const Nodo* c = esFiltro(n.tipo) ? controlador(n.id) : nullptr;
    json p = n.params;
    if (c != nullptr) {
        // Lo del controlador, y encima lo propio de esta tarjeta.
        mezclar(p, c->params.value("params", json::object()));
        mezclar(p, n.params.value("local", json::object()));
    }
    p.erase("local");
    return p;
}

std::string GrafoVideo::controlarDesde(const std::string& id, std::string* error) {
    const Nodo* n = nodo(id);
    if (n == nullptr || !esFiltro(n->tipo)) {
        if (error) *error = "«" + id + "» no es una tarjeta de filtro";
        return {};
    }
    if (const Nodo* c = controlador(id)) return c->id;
    json molde = n->params;
    molde.erase("local");
    const std::string etiqueta = n->etiqueta, tipo = n->tipo;
    const std::string cid = agregar("control", "", "", json{{"tarjeta", tipo}, {"params", molde}}, error);
    if (cid.empty()) return {};
    ajustar(cid, json{{"etiqueta", etiqueta + " (todas)"}}, nullptr); // agregarControlada lo saca
    // Abajo de todo, en la columna de la tarjeta: los cables de control suben
    // a cada entrada.
    int yMax = 0;
    for (const auto& m : nodos_)
        if (m.id != cid) yMax = std::max(yMax, m.y);
    mover(cid, nodo(id)->x, yMax + 200);
    if (conectar(cid, "ctl", id, "ctl", error).empty()) return {};
    return cid;
}

bool GrafoVideo::soltar(const std::string& id, std::string* error) {
    const Nodo* c = controlador(id);
    if (c == nullptr) {
        if (error) *error = "«" + id + "» no está manejada por un controlador";
        return false;
    }
    const json ef = efectivos(*nodo(id));
    desconectar(c->id, "ctl", id, "ctl");
    for (auto& n : nodos_)
        if (n.id == id) n.params = ef;
    return true;
}

std::string GrafoVideo::agregarControlada(const std::string& control, const std::string& grupo,
                                          const std::string& despues, std::string* error) {
    const Nodo* c = nodo(control);
    if (c == nullptr || c->tipo != "control") {
        if (error) *error = "«" + control + "» no es un controlador";
        return {};
    }
    const std::string tipo = c->params.value("tarjeta", std::string());
    const json molde = c->params.value("params", json::object());
    const std::string etiqueta = c->etiqueta;
    const std::string id = agregar(tipo, grupo, despues, molde, error);
    if (id.empty()) return {};
    static const std::string kTodas = " (todas)";
    std::string base = etiqueta;
    if (base.size() > kTodas.size() && base.compare(base.size() - kTodas.size(), kTodas.size(), kTodas) == 0)
        base.resize(base.size() - kTodas.size());
    ajustar(id, json{{"etiqueta", base}}, nullptr);
    if (conectar(control, "ctl", id, "ctl", error).empty()) return {};
    return id;
}

json GrafoVideo::configCompositor(const std::string& id) const {
    const Nodo* n = nodo(id);
    if (n == nullptr || n->tipo != "compositor") return json::object();
    const json& p = n->params;
    // Los puertos conectados, en orden; cada uno con lo que le llega.
    struct Entra {
        int indice;
        std::string puerto;
        const Nodo* desde;
    };
    std::vector<Entra> es;
    for (const auto& c : entrantes(id)) {
        const Nodo* d = nodo(c.desde);
        // Por ahora, lo que viene de una entrada (un compositor dentro de otro, no).
        if (d == nullptr || d->grupo.empty() || indicePedido(c.entrada) < 0) continue;
        es.push_back({indicePedido(c.entrada), c.entrada, d});
    }
    const json capas = p.value("capas", json::object());
    const auto orden = [&](const Entra& e) {
        const json k = capas.value(e.puerto, json::object());
        return k.is_object() ? k.value("orden", e.indice) : e.indice;
    };
    std::sort(es.begin(), es.end(), [&](const Entra& a, const Entra& b) { return orden(a) < orden(b); });
    // Sin posición: en mosaico (la grilla más chica que entra).
    const int n_ = static_cast<int>(es.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n_)))));
    const int filas = std::max(1, (n_ + cols - 1) / cols);
    json out = json::array();
    for (int i = 0; i < n_; ++i) {
        const Entra& e = es[static_cast<size_t>(i)];
        json k = capas.value(e.puerto, json::object());
        if (!k.is_object()) k = json::object();
        const bool ubicada = k.contains("x") || k.contains("ancho");
        // En la grilla, por el número de puerto (no se mueve si se reordena).
        int pos = 0;
        for (const auto& o : es)
            pos += o.indice < e.indice ? 1 : 0;
        out.push_back({{"toma", e.desde->id},
                       {"fuente", e.desde->grupo},
                       {"x", ubicada ? k.value("x", 0.0) : static_cast<double>(pos % cols) / cols},
                       {"y", ubicada ? k.value("y", 0.0) : static_cast<double>(pos / cols) / filas},
                       {"ancho", ubicada ? k.value("ancho", 1.0) : 1.0 / cols},
                       {"alto", ubicada ? k.value("alto", 1.0) : 1.0 / filas},
                       {"opacidad", k.value("opacidad", 1.0)},
                       {"ajuste", k.value("ajuste", std::string("encajar"))}});
    }
    return {{"ancho", p.value("ancho", 0)}, // 0: el del lienzo (lo resuelve el estudio)
            {"alto", p.value("alto", 0)},
            {"fondo", p.value("fondo", 0xFF000000LL)},
            {"capas", out}};
}

json GrafoVideo::paraFiltro(const std::string& fuente) const {
    const std::string ie = idEntrada(fuente), is = idEscena(fuente);
    // Lo que se alcanza desde la entrada por nodos de UNA entrada (los de
    // varias, y las escenas de otras entradas, los ejecuta el estudio).
    std::set<std::string> dentro{ie};
    std::vector<std::string> pila{ie};
    while (!pila.empty()) {
        const std::string n = pila.back();
        pila.pop_back();
        for (const auto& c : salientes(n)) {
            const Nodo* h = nodo(c.hacia);
            if (h == nullptr || dentro.count(h->id)) continue;
            if (!(esFiltro(h->tipo) || h->tipo == "salida")) continue;
            dentro.insert(h->id);
            pila.push_back(h->id);
        }
    }
    const auto mapear = [&](const std::string& id) {
        return id == ie ? std::string("in") : id == is ? std::string("out") : id;
    };
    json ns = json::array(), cs = json::array();
    ns.push_back({{"id", "in"}, {"tipo", "in"}});
    ns.push_back({{"id", "out"}, {"tipo", "out"}});
    for (const auto& n : nodos_)
        if (dentro.count(n.id) && n.id != ie)
            ns.push_back({{"id", n.id}, {"tipo", n.tipo}, {"params", efectivos(n)}});
    for (const auto& c : cables_)
        if (dentro.count(c.desde) && (dentro.count(c.hacia) || c.hacia == is))
            cs.push_back({{"desde", mapear(c.desde)}, {"hacia", mapear(c.hacia)}});
    // Lo que sale de esta entrada hacia otra parte (un compositor, la escena
    // de otra entrada): se publica como toma, con el id del nodo.
    std::set<std::string> tomadas;
    for (const auto& c : cables_) {
        if (c.entrada == "ctl" || !dentro.count(c.desde) || dentro.count(c.hacia) || c.hacia == is) continue;
        if (!tomadas.insert(c.desde).second) continue;
        const std::string t = "toma:" + c.desde;
        ns.push_back({{"id", t}, {"tipo", "toma"}, {"params", {{"nombre", c.desde}}}});
        cs.push_back({{"desde", mapear(c.desde)}, {"hacia", t}});
    }
    // Lo que llega a su escena desde afuera: se trae (un compositor, o la toma
    // de otra entrada).
    for (const auto& c : entrantes(is)) {
        if (dentro.count(c.desde)) continue;
        const Nodo* d = nodo(c.desde);
        if (d == nullptr) continue;
        json p = d->tipo == "compositor"
                     ? json{{"compositor", d->id}, {"config", configCompositor(d->id).dump()}}
                     : json{{"toma", d->id}, {"fuente", d->grupo}};
        ns.push_back({{"id", "traer"}, {"tipo", "traer"}, {"params", p}});
        cs.push_back({{"desde", "traer"}, {"hacia", "out"}});
    }
    return {{"nodos", ns}, {"cables", cs}};
}

json GrafoVideo::describir(const std::string& grupo, bool soloGrupo) const {
    std::set<std::string> visibles;
    for (const auto& n : nodos_)
        if (!soloGrupo || n.grupo == grupo) visibles.insert(n.id);
    std::set<std::string> externos;
    // Los nodos del estudio (un compositor) se ven siempre, como de afuera:
    // así se los puede cablear desde cualquier entrada.
    if (soloGrupo)
        for (const auto& n : nodos_)
            if (n.grupo.empty()) externos.insert(n.id);
    if (soloGrupo)
        for (const auto& c : cables_) {
            if (visibles.count(c.desde) && !visibles.count(c.hacia)) externos.insert(c.hacia);
            if (visibles.count(c.hacia) && !visibles.count(c.desde)) externos.insert(c.desde);
        }
    const auto puertos = [](const std::vector<Puerto>& ps) {
        json a = json::array();
        for (const auto& p : ps)
            a.push_back({{"id", p.id}, {"tipo", p.tipo}, {"pedido", p.pedido}});
        return a;
    };
    json ns = json::array(), cs = json::array();
    for (const auto& n : nodos_) {
        if (!visibles.count(n.id) && !externos.count(n.id)) continue;
        ns.push_back({{"id", n.id},
                      {"tipo", n.tipo},
                      {"titulo", titulo(n.tipo)},
                      {"etiqueta", n.etiqueta},
                      {"grupo", n.grupo},
                      {"x", n.x},
                      {"y", n.y},
                      {"params", n.params},
                      {"efectivos", efectivos(n)},
                      {"controlador", controlador(n.id) != nullptr ? controlador(n.id)->id : std::string()},
                      {"externo", externos.count(n.id) > 0},
                      {"entradas", puertos(puertosEntrada(n))},
                      {"salidas", puertos(puertosSalida(n))}});
    }
    for (const auto& c : cables_)
        if ((visibles.count(c.desde) || externos.count(c.desde)) &&
            (visibles.count(c.hacia) || externos.count(c.hacia)))
            cs.push_back(
                {{"desde", c.desde}, {"salida", c.salida}, {"hacia", c.hacia}, {"entrada", c.entrada}});
    return {{"nodos", ns}, {"cables", cs}, {"trivial", soloGrupo ? trivial(grupo) : false}};
}

} // namespace ba::app::video
