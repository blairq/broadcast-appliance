// ============================================================================
// src/core/documentos/Mrz.hpp — la zona de lectura mecánica (MRZ) del dorso
//
// El dorso del DNI argentino (y de las cédulas de muchos países) trae abajo
// tres renglones de 30 caracteres —A-Z, 0-9 y «<»— con el formato TD1 de la
// OACI (Doc 9303, parte 5):
//
//   renglón 1  ID ARG 11222333< 5 <<<<<<<<<<<<<<<
//              │  │   │         │ └ datos opcionales (16-30)
//              │  │   │         └ verificador del número (15)
//              │  │   └ número de documento (6-14)
//              │  └ quién lo emite (3-5)
//              └ tipo de documento (1-2)
//   renglón 2  900315 2 F 340420 7 ARG <<<<<<<<<<< 4
//              nacimiento+verif, sexo, vencimiento+verif, nacionalidad,
//              opcionales (19-29) y el verificador compuesto (30)
//   renglón 3  PRUEBA<<JUANA<ANDREA<<<<<<<<<<<<  apellido<<nombres
//
// Cuando el código de barras del frente no sale, el tótem pide el dorso y un
// modelo de visión (el modelo propio del operador; ver agent/Mirada en la
// app) transcribe los renglones. UN MODELO PUEDE INVENTAR: por eso lo que
// contesta pasa por acá y se acepta SOLO si cierran TODOS los dígitos
// verificadores del número, del nacimiento y del vencimiento, Y el compuesto
// cierra u OTRA foto coincide en todo lo que importa (ConfirmadorMrz: en la prueba
// con un DNI real, el compuesto falló en 6 de 14 fotos por un «<» corrido en los
// opcionales; los otros tres, nunca). Antes se corrigen las confusiones típicas según la posición: en
// un lugar de dígitos una O es un 0, en uno de letras un 0 es una O (I/1, B/8,
// S/5, Z/2, G/6…). En el número (que puede tener las dos cosas) se prueba con su
// verificador: gana la corrección con menos cambios, y si hay dos igual de
// buenas, no se acepta. Los opcionales no se corrigen (no tienen verificador
// propio: «arreglarlos» para que cierre el compuesto sería inventar).
//
// Es C++ puro, sin imágenes ni Qt, y se prueba entero con MRZ SINTÉTICAS
// (test_mrz). Ver docs/funciones/DOCUMENTOS.md.
// ============================================================================
#pragma once

#include <array>
#include <string>
#include <vector>

#include "documentos/Documento.hpp"

namespace ba::documentos {

/// El dígito verificador de la OACI: pesos 7, 3, 1 repetidos; «<» vale 0, los
/// dígitos lo suyo y las letras A=10 … Z=35; la suma módulo 10. -1 si hay un
/// carácter que no va en una MRZ.
int digitoMrz(const std::string& s);

/// Una TD1 leída y validada.
struct Td1 {
    std::string codigo;       ///< "ID", "I<", "IC"… (los dos primeros caracteres, tal cual)
    std::string emisor;       ///< "ARG" (alfa-3)
    std::string numero;       ///< el número de documento, sin los «<»
    std::string opcional1;    ///< los datos opcionales del renglón 1, sin los «<» de los costados
    std::string nacimiento;   ///< ISO ("1990-03-15")
    std::string sexo;         ///< "M", "F", "X" o "" (un «<»)
    std::string vencimiento;  ///< ISO
    std::string nacionalidad; ///< "ARG" (alfa-3)
    std::string opcional2;
    std::string apellido;     ///< con mayúscula inicial
    std::string nombres;
    std::array<std::string, 3> renglones; ///< ya corregidos (los que cierran)
    int correcciones = 0;     ///< cuántos caracteres hubo que cambiar
    bool compuesto = true;    ///< cerró el verificador compuesto (false solo sin exigirlo)
};

/// Los tres renglones de un texto libre (lo que contestó un modelo, lo que se
/// pegó): sin espacios ni comillas, en mayúsculas, «« como «<<»; si un renglón
/// tiene hasta tres «<» de más o de menos, se ajusta la tira de «<» más larga
/// (el relleno; nunca un dato). false si no hay tres renglones que parezcan de una TD1.
bool renglonesTd1(const std::string& texto, std::array<std::string, 3>& out);

/// Lee una TD1 (tres renglones o un texto libre que los contenga), con las
/// correcciones por posición, y exige que cierren los verificadores del número,
/// el nacimiento y el vencimiento; el compuesto, si `exigirCompuesto` (si no,
/// queda en `Td1::compuesto`). `motivo`, por qué no (sin los datos: va a los
/// registros). `anioActual`, para el siglo de las fechas (0 = el de hoy).
bool leerTd1(const std::string& texto, Td1& out, std::string* motivo = nullptr, int anioActual = 0,
             bool exigirCompuesto = true);

/// «ARG» -> «AR». "" si no se lo conoce.
std::string paisAlfa2(const std::string& alfa3);
/// «ARG» -> «Argentina». El código tal cual si no se lo conoce.
std::string nombrePais(const std::string& alfa3);
/// Los datos normalizados de una TD1 (el número tal cual: cada tipo lo ajusta).
Datos datosDeTd1(const Td1& t);

/// Cualquier documento con una MRZ TD1 (cédulas de identidad OACI). El DNI
/// argentino lo toma antes DniArgentino (que también entiende su dorso).
class MrzTd1 : public TipoDocumento {
   public:
    std::string id() const override { return "mrz-td1"; }
    std::string titulo() const override { return QT_TRANSLATE_NOOP("documentos", "Documento de identidad con zona MRZ (OACI TD1)"); }
    std::string comoSeDice() const override { return QT_TRANSLATE_NOOP("documentos", "tu documento"); }
    std::vector<std::string> formatos() const override { return {"MRZ"}; }
    bool leer(const Codigo& c, Datos& out) const override;
};

// --- El dorso leído por un modelo de visión ----------------------------------------

/// El formato de `Codigo` para los renglones de una MRZ ("MRZ").
inline constexpr const char* kFormatoMrz = "MRZ";
/// Lo que se le pide al modelo de visión (con la franja de abajo de la foto): que
/// transcriba EXACTAMENTE los tres renglones, o «NO SE VE». El de la prueba real.
const std::string& pedidoMrz();

/// Lo que se sacó de una respuesta del modelo.
struct LecturaMrz {
    /// Leida: cierran todos los verificadores. Dudosa: todos menos el compuesto
    /// (hace falta otra foto que coincida: ConfirmadorMrz).
    enum class Estado { Leida, Dudosa, NoSeVe, NoCierra };
    Estado estado = Estado::NoCierra;
    /// Leida/Dudosa: los tres renglones corregidos, separados por «\n» (para un Codigo "MRZ").
    std::string texto;
    /// Por qué no, o por qué es dudosa (sin datos).
    std::string motivo;
    /// Leida/Dudosa: lo que tiene que coincidir entre dos fotos (los campos con
    /// verificador y el renglón del nombre). DATOS: no va a ningún registro.
    std::string clave;
};
LecturaMrz interpretarRespuestaMrz(const std::string& respuesta, int anioActual = 0);

/// El criterio para aceptar el dorso con varias fotos: una Leida se acepta; una
/// Dudosa, si coincide en todo lo que importa (LecturaMrz::clave) con otra Dudosa
/// anterior. Uno por conversación (por conexión); reiniciar() cuando se vuelve a
/// pedir el documento.
class ConfirmadorMrz {
   public:
    /// true = aceptarla.
    bool agregar(const LecturaMrz& l);
    void reiniciar();

   private:
    static constexpr std::size_t kRecuerda = 6;
    std::vector<std::string> claves_;
};

} // namespace ba::documentos
