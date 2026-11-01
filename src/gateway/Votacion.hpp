// ============================================================================
// src/gateway/Votacion.hpp — la urna
//
// DOS COSAS DISTINTAS, Y NO SE MEZCLAN:
//
//   ENCUESTA   Abierta: vota cualquiera que tenga el enlace, una vez por
//              teléfono. Es enganche —«¿de qué tema quieren que hablemos?»— y
//              sus números son INDICATIVOS. No sirve para decidir nada.
//
//   VOTACIÓN   Formal: vota quien está en el padrón con derecho a voto y está
//              presente, una vez cada uno, identificándose con un código. Es
//              lo que hace que lo que se decide en una asamblea valga.
//
// Se llaman distinto a propósito y el resultado dice cuál fue. La tentación de
// tener «una votación con un interruptor de rigor» termina en alguien leyendo
// al aire el resultado de una encuesta como si fuera una decisión.
//
// EL CÓDIGO NO ES EL NAVEGADOR. En la formal, `quien` (el id que el teléfono se
// inventa) NO alcanza: se borra el sitio y se vota de nuevo. Lo que identifica
// es un código corto que se le da a cada persona al acreditarla, y el gateway
// solo sabe qué códigos son válidos —se los pasa A, que es quien tiene el
// padrón—. Un código, un voto.
//
// EL QUÓRUM ES DEL PADRÓN, NO DE LA URNA. Cuántos podían votar lo dice A
// cuando abre la votación; acá solo se cuenta cuántos votaron. Sin eso, una
// votación con dos votos a favor y ninguno en contra parecería unánime cuando
// en realidad no vino nadie.
//
// SECRETA quiere decir que no se guarda quién votó qué, no que no se sepa
// quién votó: para el acta hace falta saber que Fulano votó (si no, no hay
// cómo probar el quórum) y puede no corresponder saber qué votó.
//
// LA URNA ES DE A; ESTO ES SU BORDE. Los teléfonos llegan acá, así que acá se
// valida y se cuenta al instante; pero cada cambio va a la app (evento
// «votacion», con la `foto()`), que la guarda en la carpeta del evento. Si la
// sala se reinicia en medio de una votación, la app se la devuelve entera
// (`restaurar`) y la urna sigue donde estaba. En una SECRETA la foto no lleva
// quién votó qué —solo quiénes votaron y cuántos votos tiene cada opción—: lo
// que vuelve de una caída queda «sellado» (cuenta, pero ya no se puede cambiar).
//
// LOS CÓDIGOS NO SE GUARDAN EN CLARO. Son la credencial de voto: la urna los guarda (y
// la foto los lleva, y la app los escribe en votacion.json) como su MARCA, un HMAC con
// una clave que vive cifrada en el almacén de credenciales (setClaveCodigos). Con el
// archivo solo no se puede votar por nadie; un código corto tampoco se adivina probando,
// sin la clave. Una foto vieja con los códigos en claro se pasa a marcas al restaurarla.
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

class Votacion {
   public:
    /// Tope de opciones. Más que esto no entra en la pantalla de un teléfono
    /// ni se puede leer al aire.
    static constexpr std::size_t kMaxOpciones = 10;
    static constexpr std::size_t kMaxTexto = 200;

    enum class Modo { Encuesta, Formal };
    static const char* nombreDeModo(Modo m);
    static bool modoDeNombre(const std::string& s, Modo& out);

    /// Abre una. Reemplaza la que hubiera abierta: una por vez, porque en una
    /// sala nadie puede atender dos preguntas a la vez y dos urnas abiertas es
    /// la forma más rápida de mezclar los resultados.
    /// :param habilitados: en la formal, los códigos que pueden votar, con el
    ///     nombre de cada uno. En la encuesta se ignora.
    /// :param cuantosPodian: cuántos tenían derecho, según el padrón. Es lo que
    ///     permite decir «votaron 12 de 40» en vez de solo «12».
    /// :returns: false si no hay pregunta o no hay al menos dos opciones.
    bool abrir(const std::string& pregunta, const std::vector<std::string>& opciones, Modo modo, bool secreta,
               const std::map<std::string, std::string>& habilitados, int cuantosPodian,
               std::int64_t ahoraMs);

    /// Cierra la que esté abierta. Los votos quedan: el resultado se sigue
    /// mirando después, y el acta se arma con eso.
    bool cerrar();
    bool hayAbierta() const;

    /// Vota. `quien` es el navegador (encuesta) y `codigo` la persona
    /// (formal). Volver a votar CAMBIA el voto mientras esté abierta: alguien
    /// que se equivocó de botón tiene que poder arreglarlo, y cerrar la urna es
    /// lo que lo vuelve definitivo.
    /// :returns: un motivo vacío si contó; si no, por qué no.
    std::string votar(const std::string& quien, const std::string& codigo, int opcion);
    /// El código estable de un motivo de votar() («cerrada», «sin_codigo»…), para que la
    /// página no dependa de cómo está escrita la frase (la frase sigue yendo aparte).
    static const char* codigoDeMotivo(const std::string& motivo);

    /// Lo que ve el operador: el recuento, si está abierta y quién votó (o
    /// solo cuántos, si es secreta).
    nlohmann::json estado() const;
    /// Lo que ve un teléfono: la pregunta, las opciones, qué votó él y —si se
    /// dejó— cómo va. `codigo` vacío en la encuesta.
    nlohmann::json estadoPublico(const std::string& quien, const std::string& codigo) const;

    /// Todas las votaciones que se cerraron, en orden. Es lo que hace posible
    /// el acta: una asamblea vota varios puntos y el acta los lleva todos, no
    /// el último. Se guarda al CERRAR, que es cuando el resultado es firme.
    nlohmann::json historial() const;
    /// Vacía el historial. Es para empezar un evento nuevo.
    void olvidarHistorial();

    /// Si el resultado se ve mientras está abierta. En una votación formal
    /// suele NO quererse: ver que va perdiendo cambia lo que vota el que
    /// todavía no votó.
    void setMostrarEnVivo(bool si);

    /// Todo lo que hace falta para volver a armarla (ver arriba): lo que la app guarda.
    nlohmann::json foto() const;
    /// La clave de las marcas de los códigos (ver arriba). Antes de abrir o restaurar.
    void setClaveCodigos(const std::string& clave);
    /// La marca de un código («c:» + HMAC); "" si no hay código.
    std::string marca(const std::string& codigo) const;
    /// La vuelve a armar desde una foto (la que guardó la app). Una vacía la deja sin nada.
    void restaurar(const nlohmann::json& foto);

   private:
    mutable std::mutex m_;
    bool abierta_ = false;
    bool hubo_ = false; ///< false hasta que se abrió una por primera vez
    Modo modo_ = Modo::Encuesta;
    bool secreta_ = false;
    bool enVivo_ = false;
    std::string pregunta_;
    std::vector<std::string> opciones_;
    std::int64_t desdeMs_ = 0;
    std::int64_t hastaMs_ = 0;
    int cuantosPodian_ = 0;
    std::string claveCodigos_; ///< ver setClaveCodigos
    /// marca del código -> nombre, los que pueden votar en la formal.
    std::map<std::string, std::string> habilitados_;
    /// quién votó qué. La clave es la marca del código (formal) o el navegador
    /// (encuesta).
    std::map<std::string, int> votos_;
    /// Lo que volvió de una caída en una secreta: cuántos votos por opción (los votantes
    /// están en votos_ con -1: sellados).
    std::vector<int> sellados_;

    std::vector<nlohmann::json> historial_;
    /// Guarda la que está abierta en el historial. Se llama al cerrar y al
    /// abrir otra encima: una que se reemplaza sin cerrar igual paso.
    void archivarSinLock();

    nlohmann::json recuentoSinLock() const;
    nlohmann::json estadoSinLock() const;
};

} // namespace ba::gateway
