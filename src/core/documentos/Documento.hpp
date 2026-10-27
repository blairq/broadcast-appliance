// ============================================================================
// src/core/documentos/Documento.hpp — documentos de identidad leídos por su código
//
// El DNI argentino trae en el frente un código de barras 2D (PDF417) con los
// datos de la persona separados por «@»; la licencia de conducir de EE.UU.
// (AAMVA), el pasaporte de muchos países y otros documentos traen algo
// parecido. Esto es lo que convierte ese texto en DATOS NORMALIZADOS, iguales
// para cualquier documento ({tipo, numero, apellido, nombres, nacimiento…}),
// así quien los usa (el tótem de registro, el MCP) no sabe de formatos.
// Ver docs/funciones/DOCUMENTOS.md.
//
// (No confundir con los «documentos» del evento —los PDF de ui/DocumentosViewModel—:
// acá son documentos de IDENTIDAD.)
//
// MODULAR A PROPÓSITO. Cada formato es un `TipoDocumento`: dice si un código es
// suyo y lo traduce. De fábrica viene el DNI argentino (DniArgentino.hpp, en C++
// y con test); los demás entran por PLUGIN de Python (la capacidad «documentos»
// del SDK, plugins/Plugins), que la app consulta después de los de fábrica. Qué
// tipos acepta cada lugar (cada campo de un tótem) es configuración: «cambiar
// de tipo de DNI» es elegir otro tipo, no tocar el código.
//
// Leer el código de la imagen es otra cosa (documentos/lector/LectorCodigos,
// con zxing-cpp): esto es C++ puro, sin imágenes ni Qt, y se prueba entero
// (test_documentos).
//
// LOS DATOS SON SENSIBLES. Quedan en el padrón del evento (local) y no van a
// ningún servicio de afuera; en los registros, a lo sumo `paraLog` (el tipo y
// los últimos tres dígitos).
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Sin Qt: el título y «cómo se dice» se marcan para lupdate (contexto "documentos") y
// los traduce quien los muestra o los dice; acá quedan en el original.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::documentos {

/// Un código leído de una imagen (o tipeado): el formato como lo nombra zxing
/// ("PDF417", "QRCode", "DataMatrix", "Code128", "Aztec"…, o "MRZ": los renglones
/// del dorso, ver Mrz.hpp), su texto (UTF-8) y
/// los bytes tal como venían (para los formatos binarios).
struct Codigo {
    std::string formato;
    std::string texto;
    std::vector<std::uint8_t> bytes;
};

/// Lo que se sabe de la persona por su documento. Todo texto; vacío = el
/// documento no lo trae (nunca se inventa).
struct Datos {
    std::string tipo;         ///< el id del tipo que lo leyó ("dni-ar", "plugin:aamva/licencia")
    std::string pais;         ///< quién lo emite (ISO 3166 alfa-2: "AR")
    std::string numero;       ///< el número del documento (solo dígitos si es numérico)
    std::string apellido;     ///< con mayúscula inicial («Pérez» queda «Perez»: el código no trae tildes)
    std::string nombres;
    std::string sexo;         ///< "M", "F" o "X"
    std::string nacimiento;   ///< ISO: "1990-03-15"
    std::string emision;      ///< ISO
    std::string vencimiento;  ///< ISO
    std::string nacionalidad;
    std::string tramite;      ///< el número de trámite (DNI argentino)
    std::string ejemplar;     ///< "A", "B"… (DNI argentino)
    std::string cuil;         ///< "27-11222333-4", solo si el dígito verificador cierra
    std::string crudo;        ///< el texto del código tal cual
    /// Lo que un tipo de plugin quiera agregar (domicilio, categoría de la licencia…).
    nlohmann::json extra = nlohmann::json::object();

    /// {tipo, numero, …} con solo lo que hay (más `extra` si tiene algo).
    nlohmann::json aJson() const;
    static Datos deJson(const nlohmann::json& j);
    /// «Juana Andrea Prueba».
    std::string nombreCompleto() const;
};

/// Un formato de documento.
class TipoDocumento {
   public:
    virtual ~TipoDocumento() = default;
    /// "dni-ar": va en la configuración (los tipos aceptados de un campo) y en `Datos::tipo`.
    virtual std::string id() const = 0;
    /// Para leer y para decir: «DNI argentino».
    virtual std::string titulo() const = 0;
    /// Cómo se lo nombra en voz alta al pedirlo («tu DNI»).
    virtual std::string comoSeDice() const { return QT_TRANSLATE_NOOP("documentos", "tu documento"); }
    /// Los formatos de código que puede traer ("PDF417"). Vacío = cualquiera.
    virtual std::vector<std::string> formatos() const = 0;
    /// Si el código es de este tipo, lo traduce y devuelve true. Nunca a medias:
    /// si falta lo esencial (número, apellido) es que no es suyo.
    virtual bool leer(const Codigo& c, Datos& out) const = 0;
};

/// Los tipos de fábrica, en orden. Uno solo dueño de la lista: lo usan el
/// tótem, el MCP y la interfaz.
class Catalogo {
   public:
    /// Con los de fábrica: el DNI argentino (frente y dorso) y cualquier cédula con MRZ TD1 (Mrz.hpp).
    Catalogo();
    ~Catalogo();
    Catalogo(const Catalogo&) = delete;
    Catalogo& operator=(const Catalogo&) = delete;

    void registrar(std::unique_ptr<TipoDocumento> t);
    const TipoDocumento* porId(const std::string& id) const;
    const std::vector<std::unique_ptr<TipoDocumento>>& tipos() const { return tipos_; }
    /// Prueba los tipos de `aceptados` (vacío = todos los de acá) en orden y
    /// devuelve el primero que lo entiende. Los ids que no son de acá (los de
    /// plugins) se ignoran: de esos se ocupa quien llama.
    bool leer(const Codigo& c, const std::vector<std::string>& aceptados, Datos& out) const;

   private:
    std::vector<std::unique_ptr<TipoDocumento>> tipos_;
};

// --- Ayudas que comparten los tipos (públicas para probarlas) --------------------

/// Parte por un separador, CONSERVANDO los campos vacíos («@a@@b» -> "", "a", "", "b").
std::vector<std::string> partir(const std::string& s, char sep);
/// Sin espacios (ni \r \n \t) a los costados.
std::string recortar(const std::string& s);
/// "15/03/1990" (o "15-03-1990", "15.03.1990") -> "1990-03-15". "" si no es una fecha válida.
std::string fechaIso(const std::string& ddmmaaaa);
/// «JUANA ANDREA» -> «Juana Andrea»; «DE LA FUENTE» -> «de la Fuente» salvo al
/// principio. Entiende las mayúsculas acentuadas de UTF-8 (Ñ, Á…).
std::string capitalizar(const std::string& s);
/// "12345678" -> "12.345.678" (como se escribe un DNI). Lo que no es solo dígitos, tal cual.
std::string numeroConPuntos(const std::string& numero);
/// Para un registro: el tipo y los últimos tres caracteres del número
/// («dni-ar …678»). NUNCA el documento entero en un log.
std::string paraLog(const Datos& d);
/// Lo que se puede mostrar FUERA de la máquina (al asistente por MCP, que puede
/// ser un modelo en la nube): {tipo, nombre, numero: "…333", origen}. Sin fechas,
/// trámite, CUIL ni el texto crudo. `doc` es un documento ya en `datos`
/// (Datos::aJson, o {numero, origen} si se tipeó).
nlohmann::json resumenParaAfuera(const nlohmann::json& doc);
/// Tapa los números largos de un texto («Sos Juana, DNI 11.222.333» -> «Sos
/// Juana, DNI …333»): toda tira de dígitos (con puntos o espacios en el medio)
/// de 6 dígitos o más queda en sus últimos tres. Para lo que se dijo o se oyó.
std::string taparNumeros(const std::string& texto);
/// El dígito verificador de un CUIL/CUIT (prefijo de 2 + documento de 8), o -1
/// si no tiene (el resto da 10: se usa otro prefijo).
int digitoCuil(const std::string& prefijoYDocumento);

} // namespace ba::documentos
