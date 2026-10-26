// ============================================================================
// src/app/registro/Totems.hpp — los tótems de registro de un evento
//
// Un tótem es una página web (assets/web/registro, la sirve el gateway) que se
// abre en un televisor con cámara y micrófono, en una tablet o en el celular
// de quien escanea su QR: la persona pone la cara, el asistente la saluda y le
// pide de viva voz los datos que el organizador configuró acá. Ver
// docs/funciones/REGISTRO-TOTEM.md.
//
// UNO O VARIOS POR EVENTO, y de ESE evento («evento es todo»): la entrada del
// congreso pide empresa y cargo, la del recital pide si viene acompañado. Por
// eso viven en la carpeta del evento, al lado del padrón que llenan:
//
//     Eventos/<nombre>/
//     ├── evento.json
//     ├── padron.json      lo que contestaron (Asistente::datos)
//     └── registro.json    esto: qué se pregunta y cómo
//
// EL TOKEN NO VA ACÁ. La url de cada tótem lleva un secreto de 256 bits en el
// fragmento (`#r=<id>&t=<token>`); se guarda cifrado en el almacén de secretos
// (security/CredentialStore), como los links de la sesión remota, así la url
// se puede volver a mostrar y el json se puede copiar a otro evento sin
// llevarse la llave. Eso lo hace ui/TotemsViewModel.
//
// C++ puro, como el padrón: se prueba entero sin Qt (test_registro_totems).
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::registro {

/// Qué clase de dato es: decide cómo se entiende lo que la persona dice (ver
/// Conversacion.hpp) y cómo se lo repite para confirmarlo.
/// `Documento`: no se dice, se MUESTRA: la persona pone su DNI (u otro documento
/// con código de barras) frente a la cámara y se lee el código (docs/funciones/DOCUMENTOS.md).
enum class TipoCampo { Texto, Nombre, Correo, Telefono, Numero, SiNo, Opcion, Documento };

const char* nombreDeTipo(TipoCampo t);
bool tipoDeNombre(const std::string& s, TipoCampo& out);
/// Para leer en la interfaz («Correo electrónico»).
const char* etiquetaDeTipo(TipoCampo t);
/// Todos, en el orden en que se ofrecen.
const std::vector<TipoCampo>& todosLosTipos();

/// Un dato que se le pide a quien se registra.
struct Campo {
    /// La clave en `Asistente::datos` ([a-z0-9_], única en el tótem).
    std::string clave;
    /// Cómo se muestra en la pantalla («Correo»).
    std::string etiqueta;
    /// Cómo se lo pregunta EN VOZ ALTA («¿Me decís tu correo?»). Vacía = una
    /// de fábrica según el tipo.
    std::string pregunta;
    TipoCampo tipo = TipoCampo::Texto;
    /// Si no es obligatorio, a los dos reintentos fallidos se saltea.
    bool obligatorio = false;
    /// Solo para `opcion`: las que se aceptan.
    std::vector<std::string> opciones;
    /// Un ejemplo para cuando no se entiende («juan punto perez arroba gmail
    /// punto com»).
    std::string ejemplo;
    /// Solo para `documento`: los tipos de documento que se aceptan, en orden
    /// ("dni-ar", "plugin:<plugin>/<tipo>"). Vacío = cualquiera de los que haya
    /// (los de fábrica y los de los plugins que corren).
    std::vector<std::string> documentos;

    nlohmann::json aJson() const;
    static Campo deJson(const nlohmann::json& j);
};

/// La pregunta de fábrica de un tipo.
std::string preguntaDeFabrica(TipoCampo t);

struct Totem {
    /// [a-z0-9-]{1,32}: va en la url y en el almacén de secretos.
    std::string id;
    /// Para el operador («Entrada principal»).
    std::string nombre;
    /// Apagado: el gateway no lo conoce y su url no entra.
    bool activo = true;
    /// El encabezado de la pantalla. Admiten {evento}.
    std::string titulo;
    std::string subtitulo;
    /// Lo que dice al empezar. Admite {hola} («Hola» o «Hola Ana» si la
    /// reconoció y `reconocer`), {nombre} y {evento}.
    std::string saludo;
    /// Lo que dice al terminar.
    std::string despedida;
    /// Lo que dice a quien ya se registró en este evento.
    std::string yaRegistrado;
    /// Repetir cada dato y preguntar «¿está bien?» antes de anotarlo.
    bool confirmar = true;
    /// Saludar por el nombre a quien reconoce la cara (y no preguntárselo).
    bool reconocer = true;
    /// Al final, pedirle que mire derecho a la cámara y esperar una toma de frente y
    /// nítida: es la foto de su ficha y la cara con que se lo va a reconocer.
    bool foto = true;
    std::vector<Campo> campos;

    nlohmann::json aJson() const;
    static Totem deJson(const nlohmann::json& j);
};

/// Si `id` sirve como id de tótem.
bool idValido(const std::string& id);
/// Si `clave` sirve como clave de un campo.
bool claveValida(const std::string& clave);
/// Un id a partir del nombre («Entrada principal» -> "entrada-principal"),
/// que no choque con `usados`.
std::string idDesdeNombre(const std::string& nombre, const std::vector<std::string>& usados);
/// Un tótem nuevo con valores razonables: pide nombre (obligatorio) y correo.
Totem totemNuevo(const std::string& id, const std::string& nombre);
/// Revisa que el tótem se pueda usar. "" o qué está mal (en castellano, para
/// mostrarlo tal cual).
std::string validar(const Totem& t);

/// Reemplaza {evento}, {nombre} y {hola} en un texto de la configuración y
/// emprolija lo que queda («Hola , bienvenido» -> «Hola, bienvenido»).
std::string conDatos(const std::string& texto, const std::string& evento, const std::string& nombre);

class Totems {
   public:
    const std::vector<Totem>& lista() const { return lista_; }
    const Totem* porId(const std::string& id) const;

    /// Agrega uno (con id nuevo si viene vacío). Devuelve "" o el error.
    std::string agregar(Totem t);
    /// Cambia las claves que vengan en `cambios` (las de Totem::aJson; `campos`
    /// reemplaza la lista entera). Nada cambia si el resultado no valida.
    /// Devuelve "" o el error.
    std::string cambiar(const std::string& id, const nlohmann::json& cambios);
    bool quitar(const std::string& id);

    nlohmann::json aJson() const;
    static Totems deJson(const nlohmann::json& j);

   private:
    std::vector<Totem> lista_;
};

/// Lee `registro.json` de la carpeta del evento. Si no está devuelve una lista
/// vacía con `ok` en true (lo normal: el evento no usa tótems).
Totems cargarTotems(const std::filesystem::path& carpeta, bool* ok = nullptr, std::string* err = nullptr);
/// Lo escribe (al lado y renombrando, como el padrón).
bool guardarTotems(const std::filesystem::path& carpeta, const Totems& t, std::string* err = nullptr);

} // namespace ba::registro
