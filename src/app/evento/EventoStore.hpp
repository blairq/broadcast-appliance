// ============================================================================
// src/app/evento/EventoStore.hpp — los eventos en el disco
//
// Un evento es una CARPETA, no un registro en un archivo compartido:
//
//     Eventos/[<carpeta>/<subcarpeta>/]<nombre>/
//     ├── evento.json         el guion
//     ├── ficha.json          qué fue y en qué estado está (el histórico)
//     ├── Medios/             música, videos y placas propias del evento
//     ├── Discursos/          los textos del teleprompter
//     └── Respaldos/          copias del guion antes de cada guardado
//
// LAS CARPETAS ordenan el histórico como el usuario quiera («2026/Octubre»,
// «Consejo/Ordinarias»): son carpetas de verdad. Una carpeta con evento.json es
// un evento; una sin, una carpeta que agrupa. Un evento se nombra por su ruta
// relativa a Eventos/ con «/» («2026/Octubre/Sesion-ordinaria»).
//
// EL EVENTO EN CURSO (docs/PLAN-EVENTOS-HISTORICO.md) es el que recibe todo lo
// que pasa: uno a la vez, anotado en Eventos/en-curso.json para que un corte de
// luz no lo pierda. Sin uno elegido, el del día («Jornada del 07/10/2026», en
// «2026/Octubre»): nada queda sin evento.
//
// Así se lo lleva entero a otra máquina o al equipo de respaldo, y si algo
// sale mal se puede abrir con un editor de texto y ver qué dice.
//
// GUARDAR NO PISA. Antes de escribir, el guion que estaba se copia a
// Respaldos/ con la fecha. Un evento es el trabajo de días y se edita el mismo
// día del acto, con gente alrededor: perderlo por un clic no es una opción.
//
// El nombre de la carpeta sale del nombre del evento, pero no es lo mismo: se
// limpia para que sea un nombre de carpeta válido en Windows y en Linux.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "evento/Evento.hpp"

namespace ba::evento {

/// ficha.json: lo que el histórico sabe del evento (el guion es otra cosa).
/// Las claves que no conoce se conservan (las agregan fases siguientes).
struct DatosDelEvento {
    std::string estado = "planificado"; ///< "planificado" | "en_curso" | "terminado"
    std::int64_t iniciadoMs = 0;        ///< reloj de pared; 0 = nunca
    std::int64_t terminadoMs = 0;
    std::string lugar;
    std::vector<std::string> etiquetas;
    std::string notas;
    bool automatico = false; ///< el evento del día que se crea solo
    std::string dia;         ///< "YYYY-MM-DD" del automático
    nlohmann::json resto = nlohmann::json::object();

    nlohmann::json aJson() const;
    static DatosDelEvento deJson(const nlohmann::json& j);
};

/// Lo que se sabe de un evento sin abrirlo entero (para la lista).
struct Ficha {
    std::string carpeta; ///< ruta dentro de Eventos/, con «/» ("2026/Octubre/Sesion")
    std::string grupo;   ///< la carpeta que lo agrupa ("2026/Octubre"; vacío = la raíz)
    std::string nombre;
    std::string fecha; ///< el dia del evento, "YYYY-MM-DD" (vacio = sin fecha)
    std::string inicio;
    std::string huso;
    int duracionSeg = 0;
    std::size_t bloques = 0;
    std::size_t cues = 0;
    /// Cuando se guardo por ultima vez, en los tics del reloj de archivos.
    /// NO es una fecha: la epoca de ese reloj depende de la biblioteca (en
    /// libstdc++ es el ano 2174, asi que hoy da NEGATIVO). Solo sirve para
    /// ordenar entre archivos.
    std::int64_t modificadoTic = 0;
    /// false: no se pudo leer la marca de modificacion del archivo. No tiene
    /// nada que ver con `fecha`, que es el dia del evento.
    bool tieneModificado = false;
    DatosDelEvento datos; ///< ficha.json
};

/// Convierte un nombre cualquiera en algo que sirva de nombre de carpeta en
/// todos los sistemas. Vacío si no queda nada usable.
std::string nombreDeCarpeta(const std::string& nombre);

/// La otra mitad de la revisión previa: lo que hay que ir a mirar al disco.
/// Va acá (y no en Evento.cpp) porque necesita saber dónde vive el evento.
///
/// Es la parte que más evita problemas el día del acto: un cue que nombra un
/// archivo que se movió, o un discurso que quedó en la otra máquina, no se ve
/// hasta que toca — y entonces ya es tarde.
std::vector<Reparo> revisarArchivos(const Evento& e, const std::filesystem::path& carpeta);

class EventoStore {
   public:
    /// :param raiz: la carpeta Eventos/ (se crea si no está).
    explicit EventoStore(std::filesystem::path raiz);

    const std::filesystem::path& raiz() const { return raiz_; }
    std::filesystem::path carpetaDe(const std::string& carpeta) const { return raiz_ / carpeta; }

    /// El que se guardo mas recientemente: el que se estaba trabajando.
    /// Vacio si no hay ninguno.
    std::string ultimoTocado() const;

    /// Los eventos que hay (en todas las carpetas), ordenados por nombre.
    std::vector<Ficha> listar() const;

    /// Las carpetas que agrupan (no los eventos), con «/», ordenadas.
    std::vector<std::string> carpetas() const;

    /// Crea una carpeta para agrupar (anidada con «/»). Cada parte se limpia
    /// como un nombre de carpeta. Devuelve la ruta creada, o vacío.
    std::string crearCarpeta(const std::string& ruta, std::string* err = nullptr);

    /// Una ruta dentro de Eventos/ que no se sale («..», absoluta, «\»).
    static bool rutaValida(const std::string& ruta);

    /// Crea la carpeta con su esqueleto y un guion vacío. Devuelve la ruta
    /// del evento creado, o vacío con el motivo en `err`. Si ya existe uno
    /// con ese nombre, le agrega un sufijo en vez de pisarlo.
    /// :param enCarpeta: dónde (vacío = la raíz); se crea si no está.
    /// :param nombreCarpeta: el nombre de la carpeta (vacío = del nombre).
    std::string crear(const std::string& nombre, std::string* err = nullptr, const std::string& enCarpeta = {},
                      const std::string& nombreCarpeta = {});

    /// Lleva un evento a otra carpeta. Devuelve su ruta nueva, o vacío.
    /// Si era el que está en curso, lo sigue siendo.
    std::string mover(const std::string& evento, const std::string& aCarpeta, std::string* err = nullptr);

    // --- El histórico -----------------------------------------------------------
    DatosDelEvento datos(const std::string& evento) const;
    bool guardarDatos(const std::string& evento, const DatosDelEvento& d, std::string* err = nullptr) const;

    /// El evento en curso (vacío si ninguno, o si su carpeta ya no está).
    std::string enCurso() const;
    /// Lo pone en curso (termina el que estaba). `ahoraMs`: reloj de pared.
    bool iniciar(const std::string& evento, std::int64_t ahoraMs, std::string* err = nullptr);
    /// Termina el que está en curso. false si no había ninguno.
    bool terminar(std::int64_t ahoraMs, std::string* err = nullptr);
    /// Que haya un evento en curso: si no hay, el del día `dia` ("YYYY-MM-DD"),
    /// que se crea si hace falta. Un automático de otro día se termina y se pasa
    /// al de hoy; uno elegido por el usuario sigue aunque pase la medianoche.
    /// Devuelve el que queda en curso.
    std::string asegurarDelDia(const std::string& dia, std::int64_t ahoraMs, std::string* err = nullptr);

    /// Carga el guion. `ok` dice si se pudo (una carpeta sin evento.json, o
    /// con un JSON roto, devuelve un evento vacío).
    Evento cargar(const std::string& carpeta, bool* ok = nullptr, std::string* err = nullptr) const;

    /// Guarda el guion, respaldando antes el que estaba.
    bool guardar(const std::string& carpeta, const Evento& e, std::string* err = nullptr) const;

    /// Borra la carpeta entera. Es lo único que borra: se pide explícito.
    bool borrar(const std::string& carpeta, std::string* err = nullptr) const;

   private:
    std::filesystem::path raiz_;
};

} // namespace ba::evento
