// ============================================================================
// src/app/rostros/QuienEs.hpp — quién es cada cara que se ve (modelo)
//
// Las REGLAS del reconocimiento de la libreta de Personas, sin Qt, sin imágenes y sin
// disco: a quién se parece cada cara (y desde qué parecido se lo cree), «la vi recién» y
// cada cuánto se guarda, las caras desconocidas que se anotan solas (tres pasadas antes de
// creerle, se olvida la que no volvió, el tope de la libreta), las etiquetas que van a la
// tarjeta «Rostros» de una cámara, a quién se acredita en la puerta, a quién se deja
// entrar sola del lobby, el alta automática con el nombre que dio y la ronda del lobby.
//
// La libreta (rostros::Personas) es la misma que usan la página y el MCP: esto la lee y
// la cambia; guardarla, sacar la foto de una ficha (el recorte de la cara) y mirar los
// cuadros lo hace quien lo usa (ui/PersonasViewModel), que le pasa lo visto ya descrito
// (la cara, su vector y si sirve: ui/CalidadCara). La hora viene como argumento.
//
// Con test (test_rostros_quien_es).
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"

namespace ba::rostros {

class QuienEs {
   public:
    // --- Los números (ajustes; el porqué de cada uno, en el .cpp) ---
    static int vecesParaAnotar();
    static std::int64_t olvidoPendienteMs();
    static int maxFichasAuto();
    /// El parecido para ABRIRLE LA PUERTA a alguien sin preguntar (y para acreditarlo).
    static float abrirLaPuerta();
    static std::int64_t guardarVistasMs();

    explicit QuienEs(Personas& libreta) : libreta_(libreta) {}

    /// Una cara vista en un cuadro, ya descrita.
    struct Visto {
        Cara cara;
        std::vector<float> vector;
        bool sirve = false;     ///< alcanza para reconocerla (ui/CalidadCara)
        std::string sugerencia; ///< si no sirve, qué pedirle
    };
    /// Lo que se ve de una cara: quién es (si se cree) y dónde está.
    struct Fila {
        std::string id, nombre; ///< vacíos si no se sabe quién es
        float parecido = 0;
        Cara cara;
    };
    struct Resultado {
        std::vector<Fila> filas; ///< una por cara, en el mismo orden
        /// Entró una ficha o una cara nueva: guardar ya (y la lista de fichas cambió).
        bool alta = false;
        /// Solo «la vi recién», y ya toca guardar (cada guardarVistasMs).
        bool guardarVistas = false;
        /// Fichas nuevas sin foto: {id, índice de la cara} (la foto la saca quien mira el cuadro).
        std::vector<std::pair<std::string, std::size_t>> fotos;
        /// La libreta llegó al tope del anotado automático: hay que apagarlo.
        bool lleno = false;
    };
    /// Quién es cada cara. Con `registrar` (el anotado automático), anota «la vi recién»,
    /// le suma la pose a quien reconoce y anota sola a la desconocida que volvió las veces
    /// suficientes.
    Resultado resolver(const std::vector<Visto>& caras, bool registrar, std::int64_t ahora);
    /// Lo que estaba a medio ver no se arrastra (otra cámara, se apagó o se prendió).
    void olvidarPendientes() { pendientes_.clear(); }

    /// «La vi recién» de varias (lo que identificó el MCP). true = toca guardar.
    bool vistas(const std::vector<std::string>& ids, std::int64_t ahora);
    /// Se guardó la libreta (con todo lo visto hasta ahora).
    void guardada(std::int64_t ahora) { ultimoGuardado_ = ahora; }

    /// QUÉ CARA ES QUIÉN: la ficha más parecida, si llega al parecido de un rótulo
    /// (kMismaPersona); si no, `id` y `nombre` vacíos (el parecido queda, para mostrarlo).
    Coincidencia quien(const std::vector<float>& vector) const;
    /// La cara más grande (la de quien está frente a la cámara), o -1.
    static int laMasGrande(const std::vector<Visto>& v);
    /// Quién es (si se cree) la cara: la marca como vista recién. Vacío si no se sabe.
    Coincidencia identificar(const Visto& v, std::int64_t ahora);
    /// El mejor parecido de la cara con la ficha `id`; nada si no hay ficha.
    std::optional<float> comparar(const Visto& v, const std::string& id) const;
    /// Alta con nombre desde una foto: la ficha nueva (o la que el reconocimiento anotó
    /// sola, que se completa) con su cara. Si la cara ya es de alguien con nombre, ese id
    /// sin tocar nada (dos fichas para la misma cara reconocen a cualquiera de las dos).
    struct Alta {
        std::string id;
        bool cambio = false;   ///< se tocó la libreta: guardar
        bool sinFoto = false;  ///< la ficha no tiene foto: sacarle una de esta cara
    };
    Alta alta(const Visto& v, const std::string& nombre, const std::string& cargo);

    /// Las etiquetas de la tarjeta «Rostros» de una cámara: quién es cada cara que trae
    /// vector ({id: número de la cara, nombre, cargo, persona}). Solo si cambiaron desde lo
    /// último que se le mandó a esa cámara (y si había vectores); si no, nada.
    std::optional<std::string> etiquetas(const std::string& feed, const nlohmann::json& caras);

    // --- La puerta ---
    /// Las caras que se acreditan en la puerta: reconocidas con el margen de abrir la
    /// puerta y que sirven (índices en `r.filas`).
    std::vector<std::size_t> paraAcreditar(const Resultado& r, const std::vector<Visto>& caras) const;
    /// La primera vez que se avisa de esta persona en la puerta (después, no: alguien
    /// parado frente a la cámara daría un aviso por segundo).
    bool avisarPuerta(const std::string& personaId) { return avisadosPuerta_.insert(personaId).second; }
    /// Otra cámara (o volver a mirar) es otra puerta.
    void olvidarPuerta() { avisadosPuerta_.clear(); }

    // --- El lobby ---
    /// Le toca mirar a alguien de la lista de espera ({uid, posición}): uno por vuelta,
    /// rotando. `idos`: los que ya no esperan (lo que se sabía de ellos deja de valer).
    struct Turno {
        std::string uid;
        int lugar = 0;
        std::vector<std::string> idos;
    };
    Turno proximoDelLobby(const std::vector<std::pair<std::string, int>>& gente);
    /// Lo que se ve de quien espera en `uid` (la cara más grande), para su baldosa: {id,
    /// nombre, parecido, x, y, ancho, alto, cw, ch, sirve, sugerencia, cargo, caras, foto
    /// (el archivo), uid, lugar}. Sin cara, lo olvida.
    void enLobby(const std::string& uid, int lugar, const Resultado& r, const std::vector<Visto>& caras, int cw, int ch);
    const nlohmann::json& enLobby() const { return enLobby_; }
    void olvidarLobby() { enLobby_ = nlohmann::json::object(); }
    /// ¿Puede entrar sola? Una vez por persona que espera, con el margen de abrir la puerta
    /// y una cara que sirve (el permiso del padrón lo mira quien llama, y después `paso`).
    bool podriaPasar(const std::string& uid, float parecido, bool sirve) const;
    void paso(const std::string& uid) { yaPasaron_.insert(uid); }
    /// El alta automática: la ficha `fichaId` de quien espera en `uid` toma el nombre que
    /// dio (y su cargo) si no tenía nombre o si la nombró esto mismo para esta persona.
    /// La ficha como queda (para guardarla), o nada. `propia`: ya la había nombrado esto.
    std::optional<Persona> nombrar(const std::string& uid, const std::string& fichaId, bool sirve,
                                   const std::string& nombre, const std::string& cargo, bool* propia = nullptr) const;
    void nombrada(const std::string& uid, const std::string& fichaId) { nombradas_[uid] = fichaId; }

   private:
    /// Una cara que no cayó en ninguna ficha: el id de la ficha nueva cuando la vio las
    /// veces suficientes, o vacío si todavía la está mirando (o si la libreta está llena).
    std::string anotarDesconocida(const std::vector<float>& v, std::int64_t ahora, bool& lleno);

    Personas& libreta_;
    /// Una cara desconocida que todavía no llegó a ser ficha.
    struct Pendiente {
        std::vector<float> v;
        int veces = 0;
        std::int64_t ultimoMs = 0;
    };
    std::vector<Pendiente> pendientes_;
    std::int64_t ultimoGuardado_ = 0;
    std::map<std::string, std::string> etiquetasDe_; ///< lo último que se le mandó a cada tarjeta
    std::set<std::string> avisadosPuerta_;
    nlohmann::json enLobby_ = nlohmann::json::object(); ///< uid -> lo que se ve de quien espera
    int turnoLobby_ = 0;
    std::set<std::string> yaPasaron_; ///< a quién ya se hizo pasar sola (una vez c/u)
    /// Las fichas que nombró el alta automática, por quien espera: si en el lobby le
    /// corrigen el nombre, se corrige también la ficha (es la misma persona). uid -> ficha.
    std::map<std::string, std::string> nombradas_;
};

} // namespace ba::rostros
