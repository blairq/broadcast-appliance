// ============================================================================
// src/gateway/Grillas.hpp — la «Grilla de personas»: el mosaico lo arma la sala
//
// Una fuente de escena de la app («Grilla de personas», ver
// src/app/escenas/GrillaPersonas.hpp) muestra a la gente conectada (sala y
// asamblea) en un mosaico que crece y se acomoda solo. QUIÉN va en cada grilla y
// DÓNDE lo decide la app (reparto, orden, filtros, lugares estables); acá solo se
// pinta: cada grilla es UN canal de video BA.Grilla.<k> (k = 1..kMaxGrillas) que
// la app toma con bavideoanillo como cualquier invitado.
//
// POR QUÉ EL MOSAICO SE ARMA ACÁ Y NO UN VIDEO POR PERSONA HACIA LA APP
// (decisión del 2026-10-25; números en el informe de la tarea):
//   - El gateway YA decodifica el video de cada admitido (libwebrtc → I420 →
//     BGRA en RemoteVideoSink), esté o no en algún canal: el costo de decodificar
//     ya se paga acá. Mandar 50 videos a la app sumaría 50 canales de memoria
//     compartida (640x360 BGRA = 0,9 MB cada uno, triple buffer), 50 entradas del
//     motor (un pipeline, una subida a la GPU y un pad del compositor cada una) y
//     50 copias por cuadro, para terminar dibujando cuadritos de 200 píxeles.
//   - Armar el mosaico cuesta lo que mide el LIENZO, no la cantidad de gente: un
//     1280x720 a 25 cps son 23 Mpx/s de escalado con filtro (libyuv, SIMD), unos
//     pocos ms por cuadro, tengan 4 o 50 personas.
//   - Lo que de verdad escala con la gente es DECODIFICAR (50 x 720p30 = 1,4
//     Gpx/s, imposible por software). Por eso cada persona se pide al navegador
//     a la calidad del lugar que ocupa (alCambiarCalidad): en un cuadrito, 180p a
//     15 cps; en uno grande, 360p; el que está al aire, la de siempre.
//   - La asamblea manda una FOTO cada 1-3 s (no video) salvo quien tiene la
//     palabra (BA.Remoto.<k>, ruta Remoto): las fotos se decodifican acá (libyuv
//     MJPG) y el video en vivo, si lo hay, les gana.
//
// Cada persona guarda UN retrato: su último cuadro ya achicado al lugar más
// grande que ocupa (nunca agrandado), así componer es escalar poco y el costo
// por cuadro que llega es chico. Los lugares se ANIMAN: si la app manda otra
// disposición, cada cara viaja de donde estaba a donde va (transicion_ms); la
// que entra crece desde el centro de su lugar y la que sale se achica.
//
// Además mide quién HABLA (nivel del audio de cada sesión, con histéresis) y
// avisa a la app los cambios (alCambiarHablan → SSE «hablan»): la app ordena por
// quién habla y resalta.
//
// CUANDO ALGUIEN HABLA (2026-11-01): cómo se lo muestra (resaltar en su lugar, destacar
// con los demás en una franja, superponer dejándole el hueco, la escena del orador) lo
// decide la app y llega acá como rectángulos, igual que siempre. Lo propio de este lado:
//   - la CAPA de cada celda («capa»: alta|baja, con histéresis en la app: solo el que pasa
//     a grande sube) y los PRECARGADOS (el siguiente orador: alta de antemano). Se los
//     junta por persona (alta si alguna grilla la pide) y se avisa (alCambiarCapa);
//   - el FUNDIDO de la baja a la HD (Fundido.hpp): cuando llega el primer cuadro de la alta
//     (fundir), el retrato funde desde lo último que se vio de la baja en `fundido_ms`;
//   - el NOMBRE de cada celda (y su cargo o banca): el gateway no tiene fuentes, así que
//     la app dibuja cada rótulo (Qt, cualquier escritura) como una máscara A8 y la manda una
//     vez (POST /api/v1/grillas/rotulos); las celdas la nombran por su id y acá se pinta con
//     el color y el fondo de la grilla, escalada al alto de la celda.
//
// API de control (la llama SignalingServer::handleAdmin):
//   GET  /api/v1/grillas  {grillas:[{canal, ancho, alto, celdas, mas, cps}], hablan:[…]}
//   POST /api/v1/grillas  {fundido_ms, precarga:[persona…],
//                          grillas:[{canal, ancho, alto, fondo:"#AARRGGBB",
//                          resaltar_color:"#RRGGBB", transicion_ms,
//                          rotulos:{color, fondo, tamano (0..1 del alto de la celda), lugar: abajo|arriba},
//                          celdas:[{persona, x, y, w, h, resaltar, capa, rotulo}], mas:{n, x, y, w, h}}]}
//        el estado ENTERO: las grillas que no vienen se apagan. `persona`:
//        "sala:<id del participante>" | "asamblea:<id del integrante>".
//   POST /api/v1/grillas/rotulos {rotulos:[{id, w, h, a8: base64 (w*h bytes)}]}: se suman a
//        los que hay. Se olvidan los que ninguna grilla nombra (al aplicar).
//
// Threading: aplicar/estado desde la API; cuadro/foto/audio desde los hilos de
// libwebrtc y de la sesión; un hilo propio pinta. Orden de candados:
// mu_ (grillas) → retratosMu_ → Retrato::m. vozMu_ va solo.
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Capas.hpp"
#include "Fundido.hpp"
#include "MediaRoute.hpp"

namespace ba::ipc {
class SpoutSender;
}

namespace ba::gateway {

class Log;

/// Cuántas grillas (canales BA.Grilla.<k>) puede haber a la vez.
inline constexpr int kMaxGrillas = 8;
/// Celdas por grilla como mucho (la app pone el tope; esto es el techo).
inline constexpr int kMaxCeldasGrilla = 64;

/// La calidad que se le pide a la cámara de un participante de la sala
/// ("180p" | "360p" | "libre"), según dónde se lo ve. Decodificar es lo que escala
/// con la gente (50 x 720p no se decodifican por software):
///   - al aire (ruta Program): la de siempre;
///   - en una grilla (`altoGrilla` > 0, el alto del lugar más grande que ocupa): la
///     de ese lugar (un cuadrito, 180p; uno mediano, 360p; uno grande, la de siempre);
///   - en la fila (ruta Backstage): si tiene vista previa (el cuadrito del operador),
///     al menos 360p; en ninguna grilla y sin vista previa no lo ve nadie: 180p (algo
///     tiene que mandar para que la grilla lo tenga apenas entre). Manda el lugar más
///     grande donde se lo ve; al pasar a un lugar o a una grilla, vuelve lo de arriba;
///   - el lobby y lo demás: la de siempre (el operador lo atiende cara a cara).
std::string nivelCalidad(const MediaRoute& ruta, int altoGrilla);

class Grillas {
   public:
    /// Un rectángulo en píxeles del lienzo (float: la animación pasa por medios píxeles).
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
    };

    explicit Grillas(Log& log);
    ~Grillas();
    Grillas(const Grillas&) = delete;
    Grillas& operator=(const Grillas&) = delete;

    /// El hilo que pinta. `cps`: cuadros por segundo de las grillas si no se sabe el ritmo
    /// del lienzo de la app. `lienzo`: el canal que publica la app a ese ritmo (BA.Program):
    /// se pinta a su ritmo y a mitad de camino entre dos de sus cuadros (ver proximoPintado).
    void start(int cps = 30, std::wstring lienzo = {});
    void stop();

    /// Rutas /api/v1/grillas. true si la tomó.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& body);
    /// El estado entero que manda la app. "" o el error (y entonces no cambia nada).
    std::string aplicar(const nlohmann::json& pedido, std::int64_t ahoraMs);
    nlohmann::json estado() const;

    // --- Lo que llega de cada persona -------------------------------------------
    /// Un cuadro BGRA de la cámara de `persona` (solo se guarda si está en una grilla).
    void cuadro(const std::string& persona, const std::uint8_t* bgra, int w, int h, std::int64_t ahoraMs);
    /// La foto (JPEG) de un integrante de la asamblea. Si tiene video en vivo, lo gana el video.
    void foto(const std::string& persona, const std::string& jpeg, std::int64_t ahoraMs);
    /// Audio float intercalado (`canales`) de `persona`: para saber quién habla.
    void audio(const std::string& persona, const float* muestras, std::size_t n, int canales, std::int64_t ahoraMs);

    /// Cambió quién habla: la lista entera (claves de persona).
    void setAlCambiarHablan(std::function<void(const std::vector<std::string>&)> fn);
    /// La altura en píxeles del lugar más grande que ocupa `persona` (0 = ninguno):
    /// el servidor le pide al navegador esa calidad.
    void setAlCambiarCalidad(std::function<void(const std::string& persona, int alto)> fn);
    /// La altura más grande que ocupa ahora (0 = no está en ninguna grilla).
    int altoDe(const std::string& persona) const;
    /// Cambió la capa que piden las grillas de `persona` (o si está precargada): el servidor
    /// vuelve a decidir su capa (capaDe / precargada).
    void setAlCambiarCapa(std::function<void(const std::string& persona)> fn);
    /// La capa que piden las grillas para `persona` (Nada = ninguna dijo).
    Capa capaDe(const std::string& persona) const;
    /// ¿Alguna grilla la precarga (el siguiente orador)?
    bool precargada(const std::string& persona) const;
    /// Cuánto dura el fundido de la baja a la HD (lo manda la app; de fábrica kFundidoMsDefecto).
    int fundidoMs() const { return fundidoMs_.load(); }

    /// El próximo cuadro EN VIVO de `persona` es el primero de la alta: su retrato funde
    /// desde lo último que se vio en `ms`.
    void fundir(const std::string& persona, int ms);
    /// Un cuadro de la capa que se va durante el fundido (refresca la imagen que se funde).
    void cuadroAnterior(const std::string& persona, const std::uint8_t* bgra, int w, int h);
    /// Los rótulos que manda la app ({rotulos:[{id, w, h, a8}]}): "" o el error.
    std::string rotulos(const nlohmann::json& pedido);
    /// ¿Ya tiene el rótulo `id`? (las pruebas y el estado)
    bool tieneRotulo(const std::string& id) const;

    /// Pinta la grilla `canal` en el instante `ahoraMs` (lo usa el hilo y las pruebas).
    /// false si esa grilla no existe.
    bool pintar(int canal, std::int64_t ahoraMs, std::vector<std::uint8_t>& lienzo, int& w, int& h);

    /// Dibuja «+N» (un dígito de 5x7 escalado) centrado en el rectángulo: sin fuentes del
    /// sistema (el gateway no tiene Qt ni pango). Público para las pruebas.
    static void dibujarMas(std::uint8_t* lienzo, int w, int h, const Rect& r, int n, std::uint32_t color);
    /// "#RRGGBB" o "#AARRGGBB" → BGRA en un entero (byte 0 = B). false si no se entiende.
    static bool color(const std::string& texto, std::uint32_t& bgra);
    /// El reloj de las grillas (ms monotónicos): el que hay que pasarles a cuadro/foto/audio.
    static std::int64_t reloj();
    /// La curva de la animación (0..1 → 0..1, suave al salir y al llegar).
    static float suavizar(float t);
    /// Cuándo pintar el próximo mosaico (ns del reloj monotónico). Con el ritmo del lienzo
    /// (`envioNs`: su último cuadro, `periodoNs`: cada cuánto publica; los dos de la cabecera
    /// de su canal): a mitad de camino entre dos cuadros del lienzo, el primero después de
    /// `ahoraNs`. Pintar a otro ritmo (25 contra 30) o en la misma fase que el lienzo bate:
    /// el lienzo toma a veces uno nuevo y a veces repite (intervalos de 50 y 83 ms). Sin el
    /// lienzo (no publica, o hace más de 1 s), al ritmo propio: `propioNs` (lo previsto),
    /// nunca antes de ahora (sin ráfaga para alcanzar). Nunca a menos de medio período del
    /// último pintado (`anteriorNs`): el lienzo tiembla y, si publica tarde, «su mitad de
    /// camino» cae recién pintado (dos mosaicos en un cuadro del lienzo y uno se pierde).
    static std::uint64_t proximoPintado(std::uint64_t ahoraNs, std::uint64_t envioNs, std::uint64_t periodoNs,
                                        std::uint64_t propioNs, std::uint64_t anteriorNs = 0);

   private:
    struct Retrato {
        std::mutex m;
        std::vector<std::uint8_t> px;
        int w = 0, h = 0;
        int quiereW = 0, quiereH = 0; ///< el lugar más grande que ocupa (para achicar al guardar)
        std::int64_t ms = 0;          ///< cuándo llegó el último
        std::int64_t vivoMs = 0;      ///< el último cuadro de video (no foto)
        std::uint64_t seq = 0;
        std::vector<std::uint8_t> repuesto; ///< donde se achica el próximo (fuera del candado)
        Fundido fundido;              ///< de la baja a la HD (ver fundir)
        int fundidoPendienteMs = 0;   ///< > 0: el próximo cuadro en vivo arranca el fundido
    };
    struct Celda {
        std::string persona;
        Rect r;
        bool resaltar = false;
        Capa capa = Capa::Nada; ///< la que pide la app (Nada: no dijo)
        std::string rotulo;     ///< el id de su rótulo ("" = sin nombre)
    };
    struct Anim {
        Rect desde, hasta;
        std::int64_t t0 = 0;
        bool sale = false;
        bool resaltar = false;
        std::string rotulo;
    };
    /// La máscara de un nombre (A8, la dibuja la app).
    struct Rotulo {
        int w = 0, h = 0;
        std::vector<std::uint8_t> a8;
    };
    struct EstiloRotulo {
        std::uint32_t color = 0xFFFFFFFFu; ///< BGRA
        std::uint32_t fondo = 0xA0000000u;
        float tamano = 0.14f; ///< del alto de la celda
        bool arriba = false;
    };
    struct Grilla {
        int canal = 0, ancho = 0, alto = 0;
        std::uint32_t fondo = 0;            // BGRA (alfa 0 = transparente)
        std::uint32_t resaltar = 0xFF07C1FF; // ámbar
        int transicionMs = 400;
        EstiloRotulo estiloRotulo;
        std::vector<Celda> celdas;
        int mas = 0;
        Rect rMas;
        std::map<std::string, Anim> anim; // por persona
        std::unique_ptr<ba::ipc::SpoutSender> sender;
        int senderW = 0, senderH = 0;
        std::vector<std::uint8_t> lienzo;
        std::uint64_t cuadro = 0;
        std::int64_t enviadoMs = 0;
        std::map<std::string, std::uint64_t> visto; // persona → seq del retrato pintado
        bool sucia = true;
        // medición
        std::uint64_t pintados = 0;
        double msPintar = 0;
    };
    struct Voz {
        std::int64_t sobreDesdeMs = 0; ///< desde cuándo está por encima del umbral (0 = no)
        std::int64_t vozMs = 0;        ///< la última vez que sonó por encima
        bool habla = false;
    };

    std::shared_ptr<Retrato> retrato(const std::string& persona) const;
    void guardar(Retrato& r, const std::uint8_t* bgra, int w, int h, std::int64_t ahoraMs, bool vivo);
    static Rect animado(const Anim& a, int durMs, std::int64_t ahoraMs);
    /// Pinta el rótulo `r` en la celda `c` (con el estilo de la grilla).
    static void pintarRotulo(std::uint8_t* lienzo, int W, int H, const Grillas::Rect& celda, const Rotulo& r,
                             const EstiloRotulo& e);
    void pintarLocked(Grilla& g, std::int64_t ahoraMs);
    void bucle();
    /// Saca los retratos que ninguna grilla usa ni anima (con mu_ tomado).
    void limpiarRetratosLocked();

    Log& log_;
    mutable std::mutex mu_;
    std::map<int, Grilla> grillas_; // por canal
    mutable std::mutex retratosMu_;
    std::map<std::string, std::shared_ptr<Retrato>> retratos_;
    std::map<std::string, int> altos_; // persona → alto del lugar más grande (con mu_)
    std::map<std::string, Capa> capas_;  // persona → la capa que piden (con mu_)
    std::set<std::string> precarga_;     // los precargados (con mu_)
    std::map<std::string, std::shared_ptr<const Rotulo>> rotulos_; // id → máscara (con mu_)
    std::atomic<int> fundidoMs_{kFundidoMsDefecto};
    std::function<void(const std::string&)> alCapa_;
    mutable std::mutex vozMu_;
    std::map<std::string, Voz> voces_;
    std::set<std::string> hablan_;
    std::mutex fnMu_;
    std::function<void(const std::vector<std::string>&)> alHablan_;
    std::function<void(const std::string&, int)> alCalidad_;
    std::vector<std::uint8_t> tmpFundido_; // lo que se va, recortado a la celda (con mu_)
    std::atomic<bool> parar_{false};
    std::thread hilo_;
    int cps_ = 30;
    std::wstring lienzo_;
    /// Cada cuánto se pinta ahora (ns): el del lienzo o el propio (cuadro() lo mira).
    std::atomic<std::uint64_t> periodoPintarNs_{1'000'000'000ULL / 30};
};

} // namespace ba::gateway
