// ============================================================================
// src/app/escenas/GrillaPersonas.hpp — la «Grilla de personas»: quién va dónde
//
// Una fuente de escena (tipo ba_grilla) que muestra a la gente conectada —los
// admitidos de la Sala y los integrantes habilitados de la Asamblea— en un
// mosaico que crece y se acomoda solo. Esta es la LÓGICA, en C++ puro (sin Qt ni
// video, la prueban test_grilla_personas): con la gente y las grillas decide qué
// persona va en qué grilla y en qué rectángulo. El mosaico lo pinta el gateway
// (src/gateway/Grillas.hpp, canal BA.Grilla.<k>) y por qué allá está explicado
// en ese archivo; lo conecta escenas/ControlGrillas.
//
// Por instancia (los ajustes de la fuente, ver Config):
//   - origen: sala, asamblea o las dos;
//   - grupo de reparto (opcional): las grillas con el mismo grupo se reparten la
//     gente SIN REPETIR. «En orden» llena la primera hasta su tope y sigue con la
//     siguiente; «parejo» pone la misma cantidad en cada una. El orden de las
//     grillas en el grupo es «orden_en_grupo» (y el nombre, a igualdad); el orden
//     de la gente y el modo los manda la PRIMERA del grupo;
//   - tope de celdas: si no entran todos, la última celda (de la última grilla
//     del grupo) es un cuadro «+N» con los que quedaron afuera;
//   - orden: llegada, quién habla (el que habló hace menos, primero) o prioridad
//     (el campo «prioridad» del integrante en Asamblea; en la Sala, el
//     presentador fijo va primero). El orden decide QUIÉNES entran cuando no
//     entran todos y dónde va el que llega; NO reordena a los que ya están;
//   - filtros: solo los que tienen la cámara prendida; sin los que ya están al
//     aire en otra fuente (el invitado que ocupa un lugar del programa, el
//     integrante con la palabra cuando su canal o la banca están al aire, y quien
//     ya se ve en OTRA grilla que está al aire). «Al aire» es lo que se ve en el
//     programa entrando en las escenas anidadas (una escena en una capa DSK, ver
//     fuentesQueSeVen);
//   - resaltar a quien habla (un borde).
//
// GRILLAS AL AIRE PRIMERO: los grupos que tienen alguna grilla al aire se reparten
// antes que los demás, y quien quedó en una de esas cuenta como «al aire» para los
// grupos que vienen después. Así dos grillas sin grupo, una al aire y otra en la
// escena que se prepara, no muestran a la misma persona, y no hay vaivén: nadie
// mira el resultado de una vuelta para decidir la misma vuelta al revés. Dos
// grillas al aire sin grupo tampoco se repiten (gana la que se reparte primero).
//
// A MANO (Manual): el operador puede FIJAR a alguien en una grilla (va siempre en
// esa, primero, aunque los filtros digan otra cosa y aunque esté al aire en otra
// fuente; en su grupo no va en ninguna otra), SACARLO de una grilla (no aparece en
// esa ni en las de su grupo) o DEVOLVERLO a lo automático. Mover de grilla es
// sacar de la de origen y fijar en la de destino. No se guarda en disco: las
// claves son de la conexión de cada uno.
//
// LUGARES ESTABLES: cada grilla recuerda en qué lugar está cada uno. El que llega
// va al final (o al hueco de alguien que se fue en la misma vuelta); el que se va
// deja su hueco al ÚLTIMO de esa grilla (se mueve uno solo, no todos). Solo
// cuando cambia la cantidad de columnas se reacomoda el dibujo, y eso el gateway
// lo anima (cada cara viaja a su lugar nuevo).
//
// LAS ENTRADAS SE JUNTAN (ajuste «grilla.reacomodoMinMs», 2026-11-02): con 120 invitados
// entrando de a uno, cada entrada era un reacomodo entero (todas las celdas cambian de
// tamaño, el gateway rearrancaba el viaje de TODAS desde velocidad cero y le pedía a cada
// navegador otra calidad): el mosaico no terminaba nunca de moverse y se veía trabado. Ahora
// una grilla reacomoda por entradas como mucho una vez cada reacomodoMinMs: el primero que
// llega después de un rato entra en el acto; los que llegan dentro del intervalo esperan
// («entrando» en el Resultado, sin contar en el «+N») y entran TODOS JUNTOS en el próximo
// reacomodo, con una sola animación. No esperan el orador ni los fijados a mano (el fundido
// al dar la palabra sigue siendo inmediato), y con ellos entran también los nuevos que van
// ANTES en el orden (el orador no se cuela delante de los que llegaron antes que él). Los que
// se van salen en el acto (su lugar no puede quedar con alguien que ya no está).
//
// EN UN GRUPO, EL QUE HABLA NO CAMBIA DE GRILLA (2026-11-03): se queda en su lugar, en la
// grilla donde está, y se lo muestra ahí según el «al_hablar» de ESA grilla (resaltar,
// destacar o superponer dentro de ella, agrandar…); las demás del grupo no tienen orador
// (Resultado::orador vacío). Antes el orador contaba como fijado en la grilla cuya máquina lo
// elegía (siempre la primera) y el reparto lo llevaba allá, largando al último de esa a la
// siguiente: con cada orador nuevo, uno subía y otro bajaba. Hay dos órdenes distintos: el DE
// REPARTO (fijados y después llegada / quién habla / prioridad; estable, el orador no va
// adelante) y la PRIORIDAD para entrar y para la capa alta (ahí sí el orador primero: si no
// hay lugar para todos, entra igual en lugar del último elegido). «Parejo» reparte a los que
// llegan de a uno por turno, a la grilla con menos: cada grilla, en orden de llegada. Solo el
// orador elegido A MANO («destacar» en una grilla) va a esa grilla, como un fijado.
//
// La misma fuente en dos escenas es LA MISMA IMAGEN: para repartir gente entre
// escenas se usan instancias distintas con el mismo grupo.
//
// CUANDO ALGUIEN HABLA (ajuste «al_hablar», 2026-11-01). Quién es «el orador» de cada
// grilla lo decide MaquinaOrador según el disparo («disparo»: la palabra de la asamblea,
// el orador al aire de la conferencia, voz sostenida «voz_ms», o a mano), con un tiempo
// mínimo en pantalla («minimo_ms»: que no salte con cada tos). Cómo se lo muestra:
//   - sin cambios: la grilla no se toca (nadie se mueve ni crece); al orador solo se le pide
//     su capa alta (y entra con el fundido);
//   - resaltar: nadie cambia de lugar; el orador lleva el borde del color de resaltar y, según
//     «agrandar» (aparte del modo, de fábrica no): «encima» (su celda crece un 6 % sobre las
//     vecinas, sin achicar a nadie) o «achicando» (además, mientras hay orador, las demás se
//     achican al 94 %). El gateway lo ANIMA (2026-11-03): la del orador crece suave y las
//     demás se achican a la vez, en transicion_ms con la curva suave, y el borde aparece con
//     un fundido en el mismo tiempo; al dejar de hablar, al revés. Con resaltar, destacar,
//     superponer y con agrandar, el orador lleva el borde SIEMPRE (aunque «resaltar», el borde
//     a quien habla, esté apagado);
//   - destacar: el orador a un lugar grande (centro o izquierda, «destacar_tamano» % del
//     lienzo) y los demás a una franja (abajo o al costado) en el MISMO orden de siempre
//     (no se mezclan: se achican y se corren); los que no entran, «+N». El gateway anima
//     el viaje (transicion_ms);
//   - superponer: el orador encima, en un hueco que la grilla le deja en el medio (nadie
//     queda tapado); con «superponer_completa», a pantalla completa;
//   - escena: esta grilla no cambia (salvo «agrandar»); una grilla con «muestra» = orador (y
//     «sigue» = esta) muestra solo al orador, grande y con su nombre: va en la escena del
//     orador («escena_orador»). Cómo se pasa a esa escena («pasar_a_escena»): a mano (el
//     operador corta), automático (al cambiar el orador, la app la pone al aire en
//     «salida_orador», y vuelve a la de antes cuando ya no hay orador; el mínimo en pantalla
//     y la histéresis son los del orador) o con el asistente (se le pide al de
//     automatización, que corta si está autorizado, y SIEMPRE se le avisa al operador).
//     La decisión es PaseEscenaOrador (pura);
//   - nada: como antes (sin orador: solo el borde a quien habla, si «resaltar»).
// LA CAPA: el orador pide la ALTA (y el gateway funde de la baja a la HD cuando llega,
// src/gateway/Fundido.hpp); con «precarga», el siguiente (el siguiente de la fila de la
// asamblea o de la conferencia; con voz, el que empezó a hablar y todavía no llegó a
// voz_ms) también. Las miniaturas no cambian de capa por achicarse un poco: CapasGrilla
// sube a la alta pasando kSubirAlta píxeles de alto y baja recién por debajo de
// kBajarAlta, y no sube más de una por vuelta (cada subida es un cuadro clave). El que
// estaba en alta por ser orador (o precargado) y deja de serlo vuelve a la regla por
// tamaño SIN la histéresis: sigue en alta solo si su celda pasa kSubirAlta (si no, cada
// orador que pasaba quedaba en alta para siempre: medido el 2026-11-01, uno más cada
// ~6 s hasta tener a los 40 en alta). «Alta definición hasta N personas» («alta_hasta»,
// de fábrica 10; 0 = sin límite): si la grilla muestra más de N, TODAS sus miniaturas en
// baja (salvo el orador y el precargado), sea cual sea el tamaño; vuelve a la regla por
// tamaño recién con N - kMargenAltaHasta o menos (que no salte con uno que entra y sale
// justo en el borde; todasEnBaja). TOPE DURO («alta_max», de fábrica 4: un debate entre
// dos, el que queda en espera y el presentador): nunca más de N personas en alta a la vez,
// sean 5 o 40 invitados. Si hay más candidatos, CapasGrilla elige por prioridad: el orador
// (o los oradores) primero, después el presentador, después el precargado (el siguiente de
// la fila), después los fijados a mano que piden la alta por su lugar y al final el resto
// por tamaño; dentro de cada grupo, el que ya estaba en alta antes que el que llega (que
// nadie parpadee en el borde), y después el más grande. «alta_hasta» queda como estaba: dice
// CUÁNDO las miniaturas pueden pedir la alta por su tamaño; «alta_max» dice CUÁNTOS como
// mucho, por la razón que sea. Con varias grillas manda el «alta_max» más chico (la capa es
// de la persona, no de la celda). LA CAPA DE CADA PERSONA LA DECIDE SOLO ESTO (para la
// grilla): el gateway toma lo que dice cada celda y no vuelve a decidir por tamaño; lo
// único que suma es lo que se ve de esa persona fuera de la grilla (su lugar del programa
// en la escena al aire o armada, sala/CapasRemotos) y las vistas web que la miran.
// NOMBRES («nombres»: nombre, o nombre y cargo/banca): cada celda lleva el id de su rótulo
// (idRotulo); lo dibuja la app (ControlGrillas) y lo pinta el gateway con el estilo de la
// grilla (nombre_color, nombre_fondo, nombre_tamano, nombre_lugar).
//
// LA FORMA DE LAS CELDAS (C18, 2026-11-03): con «proporcion» = llenar, la disposición
// elegía las columnas que dejaban la celda de más área SIN mirar la forma, y con 22 o 43
// personas salía 11×2 o 11×4 de franjas verticales (~110×170 en 1280×720) con el video
// recortado al medio. Ahora la proporción del video (16:9) SIEMPRE cuenta (ver Forma):
// con «llenar» la celda puede alejarse de ella hasta «recorte_max» % por lado (de fábrica
// 25: entre 4:3 y 21:9) y entre las disposiciones que cumplen se elige la de más área de
// video a la vista (el área de la celda por la raíz de la fracción del cuadro que se ve:
// recortar suma, pero menos que agrandar); con una proporción fija, como siempre (la celda
// exacta, con bandas). Además ninguna celda baja de «celda_min_ancho» × «celda_min_alto»
// (px a 1080, de fábrica 96×54, escalados al lado corto del lienzo): si no entran todos
// así, el resto va al «+N» (capacidad), nunca se achica más.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ba::app::grilla {

using json = nlohmann::json;

/// El tipo de fuente.
constexpr const char* kTipo = "ba_grilla";
/// Grillas a la vez (canales BA.Grilla.1..8; el mismo techo que el gateway).
constexpr int kMaxGrillas = 8;
/// Celdas por grilla como mucho (el techo del gateway).
constexpr int kMaxCeldas = 64;

enum class Origen { Sala, Asamblea, Ambas };
enum class Orden { Llegada, Habla, Prioridad };
enum class Reparto { EnOrden, Parejo };
/// Cómo se muestra a quien habla (ver arriba).
enum class AlHablar { Nada, SinCambios, Resaltar, Destacar, Superponer, Escena };
/// Agrandar al orador en su celda (resaltar y escena): no, encima de las vecinas, o achicando a las demás.
enum class Agrandar { No, Encima, Achicando };
/// Cómo se pasa a la escena del orador (al_hablar = escena).
enum class PasarEscena { AMano, Automatico, Asistente };
/// Qué decide quién es el orador.
enum class Disparo { Voz, Palabra, Orador, Manual };
enum class Nombres { No, Nombre, NombreCargo };

/// Las miniaturas pasan a la capa alta por encima de esto (alto en píxeles)…
constexpr int kSubirAlta = 260;
/// …y vuelven a la baja recién por debajo de esto (histéresis).
constexpr int kBajarAlta = 180;
/// Las que no están en alta pasan a la MEDIA (360p) por encima de esto: la baja (180p) ya se
/// ve estirada (2026-11-03: celdas de 640x360 pixeladas con la baja, por el tope de la alta)…
constexpr int kSubirMedia = 200;
/// …y vuelven a la baja recién por debajo de esto.
constexpr int kBajarMedia = 150;
/// «Alta definición hasta N personas» de fábrica (más que esto: todas las miniaturas en baja).
constexpr int kAltaHastaDefecto = 10;
/// Con todas en baja, se vuelve a la regla por tamaño recién con N - esto o menos (histéresis).
constexpr int kMargenAltaHasta = 1;
/// «alta_max» de fábrica: nunca más de esta cantidad de personas en alta a la vez.
constexpr int kAltaMaxDefecto = 4;
/// Una celda de la franja (destacar) o de la grilla con hueco no baja de esto de alto: si
/// no entran todos así, el resto va al «+N».
constexpr int kMinCelda = 40;
/// La proporción del video de cada persona (la de las cámaras y del gateway).
constexpr double kAspectoVideo = 16.0 / 9.0;
/// «Llenar»: cuánto del video se puede recortar por lado, de fábrica y como mucho (%).
constexpr int kRecorteMaxDefecto = 25;
constexpr int kRecorteMaxTope = 40;
/// La celda más chica, de fábrica (px en un lienzo de 1080 de lado corto).
constexpr int kCeldaMinAncho = 96;
constexpr int kCeldaMinAlto = 54;
/// Agrandar «encima»: la celda del orador crece a esto de su lugar (centrada, sin salirse del
/// lienzo y dibujada encima de las vecinas); las demás quedan exactamente como siempre.
constexpr double kResaltarOrador = 1.06;
/// Agrandar «achicando»: las demás celdas van a esta fracción de su lugar (el orador, entero).
constexpr double kResaltarResto = 0.94;

struct Persona {
    std::string clave; ///< "sala:<id del participante>" | "asamblea:<id del integrante>"
    std::string nombre;
    bool deSala = true;
    std::int64_t llegadaMs = 0; ///< cuándo entró (admitido / habilitado)
    int prioridad = 0;          ///< mayor = antes
    bool conCamara = true;
    bool hablando = false;
    std::int64_t vozMs = 0; ///< la última vez que habló (0 = todavía no)
    bool alAire = false;    ///< ya se ve en otra fuente del programa
    std::string cargo;      ///< cargo, bloque o banca (el rótulo); "" = sin
    /// Desde cuándo habla sin parar (0 = no habla): para la voz sostenida.
    std::int64_t hablaDesdeMs = 0;
    bool palabra = false;   ///< tiene la palabra (asamblea)
    bool orador = false;    ///< es el orador al aire (conferencia, modo oradores)
    bool siguiente = false; ///< es el siguiente de la fila (asamblea o conferencia): se precarga
    bool presentador = false; ///< el presentador de la conferencia: alta (después del orador)
};

struct Config {
    std::string fuente; ///< el nombre de la fuente (la identifica)
    int canal = 0;      ///< BA.Grilla.<canal> (1..kMaxGrillas; 0 = sin asignar todavía)
    Origen origen = Origen::Ambas;
    std::string grupo; ///< "" = sola
    int ordenEnGrupo = 1;
    Reparto reparto = Reparto::EnOrden;
    int tope = 16; ///< celdas (con la del «+N»); 0 = hasta kMaxCeldas
    Orden orden = Orden::Llegada;
    bool soloConCamara = false;
    bool sinAlAire = true;
    bool resaltar = true;
    int ancho = 1280, alto = 720;
    int separacion = 8;
    int margen = 0;
    double aspecto = 16.0 / 9.0; ///< de cada celda; 0 = llenar el lugar (el gateway recorta, con tope: recorteMax)
    int recorteMax = kRecorteMaxDefecto; ///< llenar: % que se puede recortar del video por lado (0..kRecorteMaxTope)
    int celdaMinAncho = kCeldaMinAncho;  ///< px a 1080 (se escala al lado corto del lienzo)
    int celdaMinAlto = kCeldaMinAlto;
    int transicionMs = 400;
    std::string fondo = "#00000000";         ///< "#AARRGGBB" (transparente de fábrica)
    std::string colorResaltar = "#FFFFC107"; ///< el borde de quien habla
    // --- Cuando alguien habla ---
    AlHablar alHablar = AlHablar::Resaltar;
    Disparo disparo = Disparo::Voz;
    int vozMs = 1500;    ///< voz sostenida: cuánto tiene que hablar para pasar a orador
    int minimoMs = 4000; ///< el orador no cambia antes de esto (histéresis)
    bool precarga = true;
    bool destacarIzquierda = false; ///< destacar: el lugar grande a la izquierda (si no, al centro)
    int destacarTamano = 70;        ///< % del lienzo (alto con la franja abajo; ancho al costado)
    bool franjaCostado = false;     ///< destacar: la franja al costado (si no, abajo)
    int superponerTamano = 60;      ///< % del lienzo que ocupa el hueco del orador
    bool superponerCompleta = false;
    int fundidoMs = 300; ///< de la baja a la HD
    /// Más personas que esto en la grilla: todas las miniaturas en baja (salvo el orador y el
    /// precargado). 0 = sin límite (solo la regla por tamaño).
    int altaHasta = kAltaHastaDefecto;
    /// Tope duro: nunca más que esto en alta a la vez, por la razón que sea (1..kMaxCeldas).
    int altaMax = kAltaMaxDefecto;
    Agrandar agrandar = Agrandar::No; ///< resaltar y escena: agrandar al orador en su celda
    PasarEscena pasarEscena = PasarEscena::AMano; ///< escena: cómo se pasa a la escena del orador
    std::string salidaOrador; ///< escena: en qué salida se la pone al aire ("" = la principal)
    std::string escenaOrador; ///< escena: el nombre de la escena del orador (lo pone «Armar la escena del orador»)
    // --- Nombres ---
    Nombres nombres = Nombres::No;
    int nombreTamano = 14;                  ///< % del alto de la celda
    std::string nombreColor = "#FFFFFFFF";  ///< "#AARRGGBB"
    std::string nombreFondo = "#A0000000";
    bool nombreArriba = false;
    // --- Qué muestra ---
    bool soloOrador = false; ///< muestra = orador: solo el orador de `sigue`, grande (la escena del orador)
    std::string sigue;       ///< la grilla a la que sigue ("" = la primera que tenga orador)
    /// La fuente se ve en el programa (no es un ajuste: lo dice quien arma las
    /// configuraciones en cada vuelta, ver ControlGrillas).
    bool alAire = false;

    /// Desde los ajustes guardados de la fuente (los de fábrica, ver defectos()).
    static Config desdeAjustes(const std::string& fuente, const json& ajustes);
    /// Los ajustes de fábrica del tipo (los mismos nombres que lee desdeAjustes).
    static json defectos();
    /// Un ajuste que no se entiende: "" o por qué (para el MCP).
    static std::string validar(const json& cambios);
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct Celda {
    std::string clave;
    Rect r;
    bool resaltar = false;
    std::string capa;   ///< "alta" | "media" | "baja" ("" = no se dice)
    std::string rotulo; ///< el id de su rótulo ("" = sin nombre)
};

/// Lo que decidió el operador a mano (ver arriba). Lo valida quien lo cambia
/// (ControlGrillas): acá solo se guarda y se consulta.
struct Manual {
    std::map<std::string, std::string> fijos;             ///< clave → la fuente donde está fijada
    std::set<std::pair<std::string, std::string>> fuera;  ///< (fuente, clave): sacada de esa grilla y su grupo
    std::map<std::string, std::string> destacados;        ///< fuente → clave: el orador elegido a mano

    bool fijo(const std::string& fuente, const std::string& clave) const;
    bool sacado(const std::string& fuente, const std::string& clave) const { return fuera.count({fuente, clave}) > 0; }
    /// La fija en `fuente` (si estaba fijada en otra, deja de estarlo allá) y ya no está sacada de esa.
    void fijar(const std::string& fuente, const std::string& clave);
    /// La saca de `fuente` (si estaba fijada ahí, deja de estarlo).
    void sacar(const std::string& fuente, const std::string& clave);
    /// Vuelve a lo automático en `fuente`: ni fijada ahí ni sacada.
    void devolver(const std::string& fuente, const std::string& clave);
    /// Se olvida de lo que era de grillas que ya no existen.
    void olvidarFuentesSalvo(const std::set<std::string>& existen);
    bool vacio() const { return fijos.empty() && fuera.empty() && destacados.empty(); }
    /// El orador a mano de `fuente` ("" = ninguno: lo decide el disparo).
    std::string destacado(const std::string& fuente) const;
};

/// Lo que se ve de verdad desde `raices` (la escena del programa y lo que muestran
/// las capas DSK que están arriba): una escena cuenta con lo que tiene visible
/// adentro, y así para abajo (escenas anidadas; una que se contiene a sí misma no se
/// recorre dos veces). `itemsDe(nombre)`: los ítems visibles si `nombre` es una
/// escena, nullopt si es una fuente. Cada fuente una vez, en el orden en que aparece.
std::vector<std::string> fuentesQueSeVen(
    const std::vector<std::string>& raices,
    const std::function<std::optional<std::vector<std::string>>(const std::string&)>& itemsDe);

/// Lo que muestra una grilla en esta vuelta.
struct Resultado {
    std::string fuente;
    int canal = 0;
    std::vector<std::string> personas; ///< en orden de lugar
    int mas = 0;                       ///< los que quedaron afuera (el cuadro «+N»)
    std::vector<Celda> celdas;
    Rect rMas;
    std::string orador;    ///< el orador de esta grilla ("" = nadie)
    std::string siguiente; ///< el que se precarga ("" = nadie)
    /// Muestra más de «alta_hasta» personas (con histéresis): todas sus miniaturas en baja.
    bool enBaja = false;
    /// Los que ya llegaron y esperan el próximo reacomodo de su grupo (ver «LAS ENTRADAS SE
    /// JUNTAN»): no están en `personas` ni en `mas`.
    int entrando = 0;
    /// La disposición de la grilla (sin orador grande): columnas × filas y la celda en px
    /// (0 = no hay celdas o hay un lugar grande; la celda es la de la franja).
    int columnas = 0, filas = 0;
    int celdaAncho = 0, celdaAlto = 0;
    /// Cuántas celdas entran como mucho con la celda mínima (con la del «+N»).
    int caben = 0;
};

/// Quién es el orador de una grilla, con su tiempo mínimo en pantalla. Pura: la prueba
/// test_grilla_personas con relojes de mentira.
class MaquinaOrador {
   public:
    /// Una vuelta. `gente`: los que pueden ser orador en esta grilla; `manual`: el elegido a
    /// mano ("" = ninguno; si está, gana siempre y en el acto).
    const std::string& paso(const Config& c, const std::vector<const Persona*>& gente, const std::string& manual,
                            std::int64_t ahoraMs);
    const std::string& actual() const { return actual_; }
    std::int64_t desdeMs() const { return desde_; }
    /// El que se precarga según el disparo: el siguiente de la fila (palabra / orador) o, con
    /// voz, el que empezó a hablar y todavía no llegó a voz_ms. "" = nadie.
    static std::string siguiente(const Config& c, const std::vector<const Persona*>& gente, const std::string& actual,
                                 std::int64_t ahoraMs);

   private:
    std::string actual_;
    std::int64_t desde_ = 0;
};

/// Una disposición con un lugar grande (destacar, superponer).
struct Disposicion {
    Rect grande;
    std::vector<Rect> resto; ///< en orden: los primeros `resto.size()` de los demás
    bool hayMas = false;     ///< no entraron todos: la última celda es el «+N»
    Rect rMas;
};
/// Destacar: el orador en el lugar grande y `n` más en la franja (abajo o al costado), en orden.
Disposicion disponerDestacar(int n, const Config& c);
/// Superponer: el orador en un hueco en el medio y `n` más alrededor (o a pantalla completa).
Disposicion disponerSuperponer(int n, const Config& c);
/// Un rectángulo escalado a `f` de su tamaño (achicado o agrandado), con el mismo centro.
Rect achicar(const Rect& r, double f);
/// Agrandar (encima o achicando): la celda del orador a kResaltarOrador de su lugar, con el
/// mismo centro pero sin salirse del lienzo de `ancho`×`alto`.
Rect agrandarOrador(const Rect& lugar, int ancho, int alto);

/// ¿Todas las miniaturas de una grilla en baja? Con más de `altaHasta` personas, sí; si ya
/// estaban (`antes`), siguen hasta bajar a altaHasta - kMargenAltaHasta. 0 = sin límite.
bool todasEnBaja(int personas, int altaHasta, bool antes);

/// La capa de cada uno con histéresis y sin muchos cuadros clave a la vez (ver arriba).
class CapasGrilla {
   public:
    struct Pedido {
        int alto = 0;          ///< el lugar más grande donde se lo ve (0 = solo en miniaturas en baja)
        bool orador = false;   ///< es el orador de alguna grilla: alta ya
        bool precarga = false; ///< lo precarga alguna grilla (el siguiente): alta ya
        bool presentador = false; ///< el presentador (en una celda): alta ya
        bool fijo = false;     ///< fijado a mano en alguna grilla: solo prioridad para el tope
    };
    /// clave → true si va en alta. `maxSubidas`: cuántas miniaturas pueden subir en esta vuelta;
    /// `altaMax`: cuántos en alta como mucho (0 = sin tope), elegidos por prioridad (ver arriba).
    std::map<std::string, bool> decidir(const std::map<std::string, Pedido>& pedidos, int maxSubidas = 1,
                                        int altaMax = 0);
    /// De los que NO quedaron en alta (`alta`, lo que dio decidir), los que van en la MEDIA
    /// (360p): su lugar pasa kSubirMedia de alto, o ya estaban y no bajó de kBajarMedia. La media
    /// no cuenta para «alta_max» (cuesta un cuarto de la alta al decodificar) ni espera turno
    /// para subir (no hay transceptor nuevo: la alta topada; ver src/gateway/Capas.hpp).
    std::set<std::string> medias(const std::map<std::string, Pedido>& pedidos, const std::map<std::string, bool>& alta);

   private:
    enum class Por { Tamano, Orador }; ///< por qué está en alta (Orador: orador, presentador o precargado)
    std::map<std::string, Por> alta_;  ///< los que están en alta
    std::set<std::string> media_;      ///< los que están en la media
};

/// Pasar a la escena del orador (al_hablar = escena) a mano, automático o con el asistente.
/// Pura: con el orador de cada vuelta y lo que hay al aire en la salida, dice qué hacer. El
/// mínimo en pantalla y la histéresis ya vienen en el orador (MaquinaOrador).
class PaseEscenaOrador {
   public:
    struct Orden {
        enum Tipo { Nada, Poner, Proponer } tipo = Nada;
        std::string escena; ///< la que hay que poner (o proponer)
        std::string orador; ///< el orador nuevo ("" = se vuelve porque ya no hay)
        bool volver = false; ///< es la vuelta a la escena de antes
    };
    /// Una vuelta. `alAire`: lo que hay al aire en la salida ahora.
    Orden paso(PasarEscena modo, const std::string& orador, const std::string& escenaOrador, const std::string& alAire);
    const std::string& volverA() const { return volverA_; }

   private:
    std::string orador_;  ///< el de la vuelta anterior
    std::string volverA_; ///< la escena que estaba antes del primer pase
};

/// El id del rótulo de una persona (el texto y la versión del dibujo): "" si no lleva.
std::string idRotulo(Nombres n, const std::string& nombre, const std::string& cargo);

/// ¿Esta grilla acepta a esta persona? (origen y filtros)
bool acepta(const Config& c, const Persona& p);
/// ¿`a` va antes que `b` con este orden?
bool antes(Orden o, const Persona& a, const Persona& b);

/// La forma que puede tomar una celda (ver «LA FORMA DE LAS CELDAS»): su proporción entre
/// minAspecto y maxAspecto (iguales = fija, con bandas) y un tamaño mínimo en px del lienzo.
struct Forma {
    double video = kAspectoVideo; ///< la proporción del video (para medir cuánto se recorta)
    double minAspecto = kAspectoVideo, maxAspecto = kAspectoVideo;
    int minAncho = 0, minAlto = 0;
    /// Proporción fija (aspecto > 0) o «llenar» (0) con el recorte de fábrica; sin mínimo.
    static Forma de(double aspecto, int recorteMax = kRecorteMaxDefecto);
};
/// La forma de las celdas de una grilla: su proporción, el recorte y el mínimo escalado al lienzo.
Forma formaDe(const Config& c);

/// Cómo quedó una disposición.
struct InfoGrilla {
    int columnas = 0, filas = 0;
    int ancho = 0, alto = 0; ///< de cada celda
};

/// Los `n` rectángulos de una grilla (en orden de lectura; la última fila, centrada): entre
/// las disposiciones cuyas celdas respetan la forma y el mínimo, la de más video a la vista
/// (a igualdad, la de menos columnas). Vacío si `n` no entran con el mínimo (salvo n = 1,
/// que siempre entra). `info` (opcional) dice columnas, filas y tamaño de la celda.
std::vector<Rect> disponer(int n, int ancho, int alto, int separacion, int margen, const Forma& forma,
                           InfoGrilla* info = nullptr);
/// Lo de siempre: proporción fija o «llenar» (0) con el recorte de fábrica, sin mínimo.
std::vector<Rect> disponer(int n, int ancho, int alto, int separacion, int margen, double aspecto, int* cols = nullptr);
/// Cuántas celdas entran como mucho (hasta `tope`) en la grilla con su forma y su mínimo.
int capacidad(const Config& c, int tope = kMaxCeldas);

/// El reparto con memoria (los lugares estables): una instancia vive lo que vive el control.
class Repartidor {
   public:
    /// Una vuelta: todas las grillas y toda la gente → lo que muestra cada una (en el orden de `grillas`).
    std::vector<Resultado> calcular(const std::vector<Config>& grillas, const std::vector<Persona>& gente,
                                    const Manual& manual = {}, std::int64_t ahoraMs = 0);
    /// Olvidar los lugares (la próxima vuelta reparte desde cero).
    void olvidar() {
        lugares_.clear();
        reacomodo_.clear();
    }
    /// Cada cuánto como mucho una grilla reacomoda por gente que entra (0 = en el acto, cada una).
    void reacomodoMinMs(int ms) { reacomodoMinMs_ = std::max(0, ms); }
    int reacomodoMinMs() const { return reacomodoMinMs_; }
    /// Cuándo pueden entrar los que esperan (el reloj de `calcular`); 0 = no espera nadie.
    /// Quien llama vuelve a calcular a esa hora (si no, entran en la vuelta siguiente).
    std::int64_t proximoReacomodoMs() const { return proximo_; }

   private:
    /// Las celdas de una grilla según «al_hablar» (personas en orden de lugar, sin el «+N» del reparto).
    void disponerCeldas(const Config& c, Resultado& r, const std::map<std::string, const Persona*>& porClave) const;

    std::map<std::string, std::vector<std::string>> lugares_; // fuente → claves por lugar
    std::map<std::string, MaquinaOrador> oradores_;            // fuente → su orador
    std::set<std::string> enBaja_;                             // las grillas con todas las miniaturas en baja
    CapasGrilla capas_;
    int reacomodoMinMs_ = 0;                          // 0 = cada entrada reacomoda en el acto
    std::map<std::string, std::int64_t> reacomodo_;   // grupo → la última vez que entró alguien
    std::int64_t proximo_ = 0;                        // cuándo entran los que esperan (0 = nadie)
};

/// El estado entero para el gateway (POST /api/v1/grillas), ver src/gateway/Grillas.hpp.
json pedidoGateway(const std::vector<Config>& grillas, const std::vector<Resultado>& res);
/// Lo que dice cada rótulo (para dibujarlo): id → {nombre, cargo}. Solo los de las celdas.
std::map<std::string, std::pair<std::string, std::string>> rotulosDe(const std::vector<Config>& grillas,
                                                                     const std::vector<Resultado>& res,
                                                                     const std::vector<Persona>& gente);

/// Color de OBS (entero 0xAABBGGRR, como guarda la ventana Propiedades) → "#AARRGGBB".
std::string colorDeObs(std::uint32_t abgr);

} // namespace ba::app::grilla
