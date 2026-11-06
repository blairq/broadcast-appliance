// ============================================================================
// src/gateway/PruebaSala.hpp — la prueba de sala (simulacro antes del evento real)
//
// Antes de un evento con mucha gente, el operador prueba si la máquina y la red
// aguantan N personas: entran participantes «de prueba» (los genera hinchada, el
// probador de carga, o un generador externo) por el MISMO camino que un invitado
// real (/api/join + /ws), pero con una CREDENCIAL DE PRUEBA en lugar del PIN:
//
//   - la emite la app al arrancar la prueba (POST /api/v1/prueba-sala), es
//     aleatoria, vence sola (`venceMs`) y se revoca al terminar (DELETE);
//   - quien entra con ella NO pasa por el lobby (si hay lugar) y queda marcado
//     `prueba` en la sala: fuera del padrón, la votación y el quórum (la app lo
//     muestra con el rótulo «prueba»);
//   - los reales siguen pasando por el lobby, salvo que el operador prenda
//     «Entrada directa para todos» (`directa`), que solo existe mientras dura la
//     prueba y se apaga sola al terminar.
//   - La prueba de CAPACIDAD desde otro equipo (docs/funciones/CAPACIDAD.md): el
//     probador de afuera usa la misma credencial y lee la MEDIDA de esta máquina (CPU,
//     memoria, grilla, audio) que la app deja acá cada segundo (`ponerMedida`), por
//     GET /api/prueba-sala/medida con la credencial en una cabecera (nunca en la URL).
//     Las IP que entraron con la credencial (`anotarIp`) no pasan por el tope de
//     conexiones por IP mientras dura (todos los de prueba salen de una sola máquina).
//
// Esto es el MODELO (sin sockets ni sala): la credencial, los tokens de un uso
// que se emitieron con ella (para reconocer al que abre el /ws), los ids de los
// participantes de prueba y el interruptor. La limpieza (echar a los de prueba)
// la hace SignalingServer con los ids que devuelve `cerrar()`.
//
// Threading: thread-safe (un mutex interno).
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class PruebaSala {
   public:
    /// La prueba más larga que se acepta (una credencial no vive para siempre).
    static constexpr int64_t kDuracionMaxMs = 4LL * 3600 * 1000;

    /// Abre (o renueva) la prueba con `credencial` hasta `venceMs`. Una prueba ya
    /// abierta conserva sus participantes y su interruptor; la credencial cambia.
    /// false si la credencial es corta (< 16) o el plazo no sirve.
    bool abrir(const std::string& credencial, int64_t ahoraMs, int64_t venceMs);
    /// Termina: olvida la credencial y los tokens, apaga la entrada directa.
    /// Devuelve los ids de los participantes de prueba (para echarlos).
    std::vector<std::string> cerrar();

    /// Abierta y sin vencer.
    bool activa(int64_t ahoraMs) const;
    /// Abierta pero ya vencida (el tic del gateway la cierra).
    bool vencida(int64_t ahoraMs) const;

    /// ¿`pin` es la credencial de prueba vigente? (comparación en tiempo constante)
    bool credencialValida(const std::string& pin, int64_t ahoraMs) const;
    /// Un token de un uso emitido con la credencial (vence con el token).
    void anotarToken(const std::string& token, int64_t ahoraMs, int64_t venceTokenMs);
    /// Si `token` se emitió con la credencial vigente, lo consume y dice true.
    bool tomarToken(const std::string& token, int64_t ahoraMs);

    void anotarParticipante(const std::string& id);
    void olvidarParticipante(const std::string& id);
    bool esDePrueba(const std::string& id) const;
    std::vector<std::string> participantes() const;

    /// La entrada directa para todos (sin lobby). Solo con la prueba activa:
    /// devuelve false (y no cambia nada) si no lo está.
    bool setDirecta(bool on, int64_t ahoraMs);
    bool directa() const;

    /// La medida de esta máquina para el probador remoto (la pone la app cada segundo).
    void ponerMedida(const nlohmann::json& medida, int64_t ahoraMs);
    /// {medida, edadMs} (null sin medida o sin prueba).
    nlohmann::json medida(int64_t ahoraMs) const;
    /// Lo que manda cada máquina que GENERA contra esta (con la credencial, en la cabecera
    /// X-BA-Generador del pedido de la medida): su CPU, su red, lo que decodifica, su ficha.
    /// Un objeto con `maquina`; hasta kMaxGeneradores y kMaxGeneradorBytes cada uno.
    void ponerGenerador(const nlohmann::json& g, int64_t ahoraMs);
    /// [{maquina, edadMs, medida}] de los que mandaron en los últimos 30 s.
    nlohmann::json generadores(int64_t ahoraMs) const;
    static constexpr std::size_t kMaxGeneradores = 16;
    static constexpr std::size_t kMaxGeneradorBytes = 6000;
    /// Una IP (orden de red) entró con la credencial: sin tope por IP mientras dure.
    void anotarIp(std::uint32_t ip);
    bool ipDePrueba(std::uint32_t ip, int64_t ahoraMs) const;

    /// Para la API de control (loopback): activa, desde/vence, directa, cuántos.
    /// `conCredencial`: la credencial va (solo a la app, nunca al snapshot).
    nlohmann::json estado(int64_t ahoraMs, bool conCredencial) const;

   private:
    bool activaLocked(int64_t ahoraMs) const;

    mutable std::mutex m_;
    std::string credencial_; ///< "" = no hay prueba
    int64_t desdeMs_ = 0;
    int64_t venceMs_ = 0;
    bool directa_ = false;
    std::map<std::string, int64_t> tokens_; ///< token -> vence
    std::set<std::string> ids_;
    int entradas_ = 0; ///< tokens canjeados en esta prueba
    nlohmann::json medida_; ///< la de la app (null = ninguna)
    int64_t medidaMs_ = 0;
    std::map<std::string, std::pair<nlohmann::json, int64_t>> generadores_; ///< maquina -> (lo que mandó, cuándo)
    std::set<std::uint32_t> ips_; ///< las que entraron con la credencial
};

} // namespace ba::gateway
