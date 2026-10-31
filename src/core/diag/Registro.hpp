// ============================================================================
// src/core/diag/Registro.hpp — el registro de todo lo que pasa
//
// EL PROBLEMA QUE VIENE A RESOLVER. Hasta acá cada proceso dejaba rastro a su
// manera: el launcher escribía líneas estructuradas, el gateway también, y el
// Proceso A volcaba lo que saliera por stdout y stderr —printf, avisos de Qt y
// de QML, GStreamer— sin nivel, sin componente y sin hora. Cuando algo falla en
// vivo, eso es un archivo de texto que hay que leer a ojo, y lo único que se
// puede hacer con él es buscar una palabra.
//
// El Registro es el logger de verdad, uno solo para todo el árbol:
//
//   * SEIS NIVELES, de traza a fatal, y el mínimo se cambia EN VIVO (también
//     por la variable de entorno BA_LOG_NIVEL, para poder arrancar con traza
//     cuando algo falla al arrancar y no llegás a tocar nada).
//   * UN COMPONENTE por línea ("motor", "sala", "qml", "obs", "graficos"...),
//     que es lo que permite mirar una sola cosa cuando hay veinte hablando.
//   * UN CÓDIGO estable por error, para poder buscar el mismo problema en dos
//     máquinas distintas.
//   * ARCHIVO ROTATIVO en Logs/, que no crece sin fin.
//   * Y un ANILLO EN MEMORIA con lo último, que es lo que lee el visor sin
//     tocar el disco: en vivo, abrir el visor no puede costar una lectura de
//     archivo de varios megabytes.
//
// EL FORMATO es el que ya usaba el árbol, y no se cambia por gusto: lo que
// escribe el launcher y el gateway hoy se sigue leyendo igual.
//
//     [AAAA-MM-DDTHH:MM:SS.mmmZ] nivel | componente | codigo | contexto
//
// Es de las dos cosas a la vez: se lee de corrido y se parsea sin ambigüedad
// (ver LectorDeLogs). La hora es UTC para poder entrelazar los archivos de los
// tres procesos sin pensar en husos.
//
// NO LANZA NUNCA, y no es para el hilo de audio: escribe a disco.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ba::diag {

/// De menos a más grave. El orden importa: el filtro es «de este para arriba».
enum class Nivel : int {
    Traza = 0,      ///< el detalle fino: cada cuadro, cada paquete. Ruidoso.
    Depuracion = 1, ///< lo que le sirve a quien está buscando un problema.
    Info = 2,       ///< lo que pasó y está bien que pase. El nivel de siempre.
    Aviso = 3,      ///< algo no está como debería, pero se sigue.
    Error = 4,      ///< algo no se pudo hacer.
    Fatal = 5,      ///< algo que impide seguir.
};

const char* nombreDeNivel(Nivel n);
/// Entiende "traza", "info", "aviso"... y también "warn"/"warning"/"error" en
/// inglés, que es lo que escupen Qt y GStreamer.
bool nivelDeNombre(const std::string& s, Nivel& out);

/// Una línea del registro, ya partida.
struct Registrado {
    std::int64_t ms = 0; ///< época UTC en milisegundos
    Nivel nivel = Nivel::Info;
    std::string componente;
    int codigo = 0;
    std::string texto;

    /// La línea tal como va al archivo (sin el salto).
    std::string aLinea() const;
};

/// El registro del proceso. Uno solo, compartido: `ba::diag::registro()`.
class Registro {
   public:
    /// Empieza a escribir a `<dir>/<archivo>`, rotando. Idempotente y
    /// no-fallible: si el directorio no se puede crear, se sigue con stderr y
    /// con el anillo, que es mejor que no registrar nada.
    void abrir(const std::filesystem::path& dir, const std::string& archivo,
               std::size_t maxBytes = 4u * 1024u * 1024u, std::size_t archivos = 5);

    /// Abre el archivo SOLO si nadie está capturando ya la salida.
    ///
    /// El launcher redirige stdout y stderr de sus hijos a Logs/<proceso>.log:
    /// si además abriéramos nuestro propio archivo, cada línea quedaría dos
    /// veces —una por el espejo en stderr y otra por el rotador— y habría dos
    /// escritores rotando el mismo archivo. Cuando la salida está capturada,
    /// ESA captura es el log; cuando no (una corrida suelta, desde una
    /// terminal), se abre el archivo para que igual quede rastro.
    ///
    /// :returns: true si se abrió archivo propio.
    bool abrirSegunEntorno(const std::filesystem::path& dir, const std::string& archivo);

    /// El componente que se usa cuando no se dice otro (el del proceso).
    void setComponentePorDefecto(std::string c);

    /// Mínimo que se registra. Todo lo de abajo se descarta sin costo.
    void setNivel(Nivel n);
    Nivel nivel() const;
    /// true si una línea de ese nivel se va a registrar. Sirve para no armar
    /// un texto caro que después se tira.
    bool registra(Nivel n) const;

    /// Si además se espeja a stderr. En el Proceso A conviene dejarlo: el
    /// launcher captura esa salida y queda el archivo crudo por las dudas.
    void setEspejoEnStderr(bool si);

    void linea(Nivel n, const std::string& componente, int codigo, const std::string& texto);
    /// Con el componente por defecto y código 0.
    void info(const std::string& texto) { linea(Nivel::Info, {}, 0, texto); }
    void depuracion(const std::string& texto) { linea(Nivel::Depuracion, {}, 0, texto); }
    void aviso(const std::string& texto, int codigo = 0) { linea(Nivel::Aviso, {}, codigo, texto); }
    void error(const std::string& texto, int codigo = 0) { linea(Nivel::Error, {}, codigo, texto); }

    /// Lo último que se registró, de lo más viejo a lo más nuevo. Es lo que
    /// lee el visor: sin tocar el disco y sin parsear nada.
    std::vector<Registrado> ultimas(std::size_t cuantas = 500) const;
    /// Cuántas líneas se registraron desde que arrancó el proceso (incluye las
    /// que ya salieron del anillo). El visor lo usa para saber si hay algo
    /// nuevo sin copiarse la lista entera.
    std::uint64_t cuenta() const;
    /// Cuántas se descartaron por estar bajo el nivel mínimo. Que esto sea
    /// visible evita la pregunta «¿no pasó nada o no lo estaba mirando?».
    std::uint64_t descartadas() const;

    /// Se llama con cada línea registrada, desde el hilo que la registró. Es
    /// para que la interfaz se entere sin preguntar. Tiene que ser barato y no
    /// puede volver a llamar al registro.
    void setOyente(std::function<void(const Registrado&)> fn);
    /// Otro oyente más (la bitácora del evento, por ejemplo), con las mismas
    /// reglas. Devuelve un id para sacarlo.
    int agregarOyente(std::function<void(const Registrado&)> fn);
    void quitarOyente(int id);

    /// Tamaño del anillo en memoria.
    static constexpr std::size_t kEnMemoria = 2000;

    Registro();
    ~Registro();
    Registro(const Registro&) = delete;
    Registro& operator=(const Registro&) = delete;

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

/// El registro del proceso.
Registro& registro();

} // namespace ba::diag
