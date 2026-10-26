// ============================================================================
// src/app/registro/Conversacion.hpp — el guion hablado de un tótem de registro
//
// Quien se para frente al tótem no escribe: HABLA, y el asistente le contesta
// con voz. Esto es lo que decide qué contestarle. Un guion determinista y no un
// agente de lenguaje, a propósito:
//   - tiene que contestar EN EL ACTO (una pausa de tres segundos frente a una
//     pantalla es «se colgó»), y con varias personas a la vez (hasta cuatro
//     tótems/celulares conversando);
//   - lo que pide lo configuró el organizador (registro/Totems): no hay nada
//     que improvisar, y un modelo que improvisa en la puerta de un evento es un
//     riesgo sin ganancia.
//
// Entradas: la cara vista (conocida o no), lo que se le oyó, lo que tipeó en la
// pantalla (el respaldo táctil), un silencio largo y «cancelar». Salidas, en
// una `Respuesta`: qué decir, si cambiaron los campos, qué campo se pide,
// guardar y terminar. No sabe de audio, ni de red, ni de Qt: ui/TotemsViewModel
// lo conecta con el oído (agent::Voz), la boca (audio/Habla) y el gateway.
//
// LO DIFÍCIL ES ENTENDER, no preguntar. La voz a texto devuelve «jorge punto
// ortega arroba gmail punto com», «once cuatro cinco seis…» o «dale»: de eso
// se ocupan los normalizadores de abajo (públicos para probarlos uno por uno,
// test_registro_conversacion). Castellano rioplatense.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "registro/Totems.hpp"

namespace ba::registro {

// --- Normalizadores: de lo dicho (o tipeado) al dato -----------------------------
// Todos devuelven false si no se entiende, y nunca inventan: mejor volver a
// preguntar que anotar un correo que no existe.

/// Minúsculas, sin acentos (la ñ queda como n) y sin espacios de más.
std::string aplanar(const std::string& s);
/// «jorge punto ortega arroba gmail punto com» -> "jorge.ortega@gmail.com".
/// También lo ya escrito con @, y «guion bajo», «guion», dígitos en palabras y
/// lo que se dice antes («mi correo es…»). Un gmail/hotmail sin «punto com» lo
/// completa.
bool entenderCorreo(const std::string& dicho, std::string& out);
/// «once cuatro cinco seis siete ocho nueve cero uno dos» -> "1145678912".
/// Dígitos, palabras («cuarenta y cinco», «doble cinco»), «más» al principio
/// (-> "+"), guiones y espacios. Entre 6 y 15 dígitos.
bool entenderTelefono(const std::string& dicho, std::string& out);
/// «dos mil veinticinco», «35», «ninguno» -> el número (entero, >= 0).
bool entenderNumero(const std::string& dicho, long long& out);
/// 1 = sí, 0 = no, -1 = no se entiende (o se contradice: «no, sí»).
/// Con las del Río de la Plata: «dale», «de una», «joya», «obvio», «ni ahí»…
int entenderSiNo(const std::string& dicho);
/// «me llamo jorge ortega» -> "Jorge Ortega" (sin «me llamo», con mayúsculas,
/// «de»/«del» en minúscula). false si no queda un nombre (vacío, números, un «sí»).
bool entenderNombre(const std::string& dicho, std::string& out);
/// Cuál de `opciones` dijo (por su texto o «la primera», «la segunda»…).
bool entenderOpcion(const std::string& dicho, const std::vector<std::string>& opciones, std::string& out);
/// Texto libre: lo dicho, prolijo (sin el punto final, con mayúscula).
bool entenderTexto(const std::string& dicho, std::string& out);
/// El número de un documento dicho o tipeado («once, dos dos dos, tres tres
/// tres», «11.222.333») -> "11222333": de 6 a 9 dígitos, sin ceros adelante. Es
/// el respaldo de cuando la cámara no lee el código.
bool entenderNumeroDocumento(const std::string& dicho, std::string& out);

/// Lo dicho como dato de un campo, ya en su forma de `datos` (texto, número o
/// booleano). false si no se entiende.
bool entender(const Campo& c, const std::string& dicho, nlohmann::json& out);
/// Cómo se repite un dato EN VOZ ALTA para confirmarlo (el correo letra por
/// palabra: «jorge punto ortega arroba gmail punto com»; el teléfono de a un
/// dígito, en grupos).
std::string paraDecir(const Campo& c, const nlohmann::json& valor);
/// Cómo se muestra en la pantalla ("11 4567 8901", "Sí").
std::string paraMostrar(const Campo& c, const nlohmann::json& valor);

// --- La conversación ------------------------------------------------------------

class Conversacion {
   public:
    /// Foto: ya contestó todo y se le pidió que mire a la cámara (Totem::foto); quien
    /// llama mira los cuadros y contesta con fotoLista() o fotoFallo().
    /// Documento: se le pidió que muestre su documento (un campo `documento`); quien
    /// llama busca el código en los cuadros y contesta con documentoLeido() o
    /// documentoFallo(). Mientras tanto se lo puede tipear (escrito) o dictar el número.
    enum class Estado { Esperando, Preguntando, Confirmando, Foto, Terminada, Documento };

    /// Quién está frente al tótem, según la cara.
    struct Cara {
        std::string personaId; ///< su ficha en la libreta ("" = no se la conoce)
        std::string nombre;    ///< el de la ficha ("" = sin nombre)
        bool yaRegistrado = false; ///< ya está en el padrón por un registro de este evento
    };

    /// Lo que hay que hacer después de cada entrada.
    struct Respuesta {
        std::string decir;    ///< lo que dice el asistente ("" = nada)
        bool campos = false;  ///< cambiaron los datos anotados: mostrarlos
        std::string pedir;    ///< la clave del campo que se está pidiendo ("" = ninguno)
        bool guardar = false; ///< terminó bien: guardar `datos()`
        bool terminar = false; ///< terminó (bien o no): pantalla de gracias y a esperar
        bool foto = false;    ///< buscar una toma de frente (cuando termine de decirlo)
        /// buscar el código del documento en la cámara (cuando termine de decirlo);
        /// también al volver a pedirlo, así quien llama cuenta el tiempo de nuevo
        bool documento = false;
    };

    Conversacion(Totem totem, std::string evento);

    /// Empieza (una cara estable frente al tótem, o «Tocá para empezar»). Si ya
    /// había empezado, nada.
    Respuesta empezar(const Cara& cara);
    /// La cara se conoció después de empezar (se empezó con el toque): sirve
    /// para vincularla al guardar, sin decir nada.
    void conocerCara(const Cara& cara);
    /// Lo que se le oyó (una frase de la voz a texto).
    Respuesta oido(const std::string& texto);
    /// Lo que tipeó en la pantalla para el campo `clave`.
    Respuesta escrito(const std::string& clave, const std::string& valor);
    /// No dijo nada en un buen rato (lo mide quien llama).
    Respuesta silencio();
    /// Apretó «cancelar» (o lo dijo).
    Respuesta cancelar();
    /// En el paso de la foto: hubo una toma de frente que sirve. Se despide y guarda.
    Respuesta fotoLista();
    /// En el paso de la foto: no salió (pasó el tiempo sin una cara que sirva).
    /// `sugerencia` es qué corregir, en palabras que se le pueden decir («acercate un
    /// poco»). La primera vez se lo pide de nuevo; la segunda sigue sin foto.
    Respuesta fotoFallo(const std::string& sugerencia);
    /// En el paso del documento: se leyó uno de los tipos aceptados. `datos` son los
    /// normalizados (documentos::Datos::aJson). Con «confirmar» se lo repite («Sos
    /// Juana Prueba, DNI 11.222.333, ¿está bien?»); al aceptarlo completa los campos
    /// que se pueden sacar de ahí (el nombre, y las claves apellido/nombres/dni/
    /// nacimiento/cuil/sexo) que todavía no tengan dato.
    Respuesta documentoLeido(const nlohmann::json& datos);
    /// En el paso del documento: pasó el tiempo sin leerlo. `sugerencia`, qué
    /// corregir. La primera vez se lo pide de nuevo; la segunda, si no es
    /// obligatorio, se saltea (si es, se le pide que tipee el número).
    Respuesta documentoFallo(const std::string& sugerencia);
    /// Los tipos de documento que acepta el campo que se pide ahora (vacío =
    /// cualquiera, o no se está pidiendo un documento).
    std::vector<std::string> documentosAceptados() const;

    Estado estado() const { return estado_; }
    bool enCurso() const {
        return estado_ == Estado::Preguntando || estado_ == Estado::Confirmando || estado_ == Estado::Foto ||
               estado_ == Estado::Documento;
    }
    const Cara& cara() const { return cara_; }
    const Totem& totem() const { return t_; }
    /// clave -> valor, solo lo ya anotado.
    nlohmann::json datos() const;
    /// Para la pantalla: [{clave, etiqueta, valor, listo}].
    nlohmann::json camposParaMostrar() const;
    /// El campo que se pide ahora: {clave, etiqueta, tipo, ejemplo}, o null.
    nlohmann::json pedido() const;
    /// El nombre que dijo (el primer campo de tipo nombre) o, si no, el de la ficha.
    std::string nombre() const;
    /// En qué paso va, para leer («preguntando Correo»).
    std::string paso() const;
    /// El encabezado de la pantalla, con los datos puestos.
    std::string titulo() const;
    std::string subtitulo() const;

   private:
    std::string pregunta(std::size_t i) const;
    std::string reintento(const Campo& c, int intento) const;
    /// Pasa al próximo campo sin dato (o termina). `antes` va adelante de lo que se diga.
    void avanzar(Respuesta& r, const std::string& antes);
    void terminar(Respuesta& r, const std::string& antes);
    void fallo(Respuesta& r);
    void aceptar(const nlohmann::json& v);
    /// Lo que dijo mientras se busca el documento (saltear, repetir, el número).
    Respuesta oidoDocumento(const std::string& texto, const std::string& aplanado);
    /// Los campos sin dato que salen del documento leído (ver documentoLeido).
    void completarDesdeDocumento(const nlohmann::json& doc);

    Totem t_;
    std::string evento_;
    Cara cara_;
    Estado estado_ = Estado::Esperando;
    std::size_t actual_ = 0;
    std::vector<nlohmann::json> valores_; ///< null = sin dato
    std::vector<bool> salteado_;
    nlohmann::json propuesto_; ///< lo que se está confirmando
    int intentos_ = 0;
    int silencios_ = 0;
    bool fotoHecha_ = false; ///< ya pasó por el paso de la foto (salió o se dio por perdida)
    int fotoIntentos_ = 0;
};

} // namespace ba::registro
