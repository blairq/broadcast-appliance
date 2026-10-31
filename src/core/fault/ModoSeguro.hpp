// ============================================================================
// src/core/fault/ModoSeguro.hpp — el modo seguro escalonado (F1-10) y a quién culpar
//
// EL PROBLEMA. El modo seguro de antes era uno solo y era lo contrario de
// «vuelve como estaba»: escenas vacías y x264. Si lo que tumbaba la app era la
// superresolución, el cortacircuito dejaba la sala en negro para sacar UNA
// tarjeta. Ahora sube de a un escalón por cada disparo del cortacircuito:
//
//   nivel 1  sin superresolución, CLAP de terceros, plugins de Python, agentes
//            ni modelos locales. Las escenas SE RESTAURAN.
//   nivel 2  además sin NDI ni capturas de ventana. Las escenas se restauran.
//   nivel 3  el de siempre: escenas vacías, x264, no se guarda nada.
//
// Y si el volcado de la última caída dice de quién fue (`atribuir`, con los
// módulos y los nombres de la pila del hilo que se cayó), esa pieza se apaga
// también, aunque el nivel no la incluya (`extra`): una caída en NDI con el
// nivel 1 apaga NDI sin esperar al nivel 2.
//
// Lo arma el launcher (Supervisor) y se lo pasa a la app por argumentos
// (`--safe-level=N`, `--safe-off=ndi,superres`); la app lo lee una vez al
// arrancar (`leerArgumentos`) y cada pieza pregunta `delProceso().apagada(…)`.
// No se guarda en ningún lado: es de esta corrida (como el modo seguro del
// diario de arranque). La configuración no se toca: al volver al arranque
// normal está todo como estaba.
// ============================================================================
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ba::fault {

/// Lo que el modo seguro puede apagar (lo que más se cae, según el historial).
enum class Pieza : int { Superres, ClapTerceros, PluginsPython, Agentes, Modelos, Ndi, CapturaVentana };

inline constexpr int kNivelMaximo = 3;

/// Todas, en orden (para listar).
const std::vector<Pieza>& piezas();
/// La clave de la pieza (argumentos, `status`, caidas.jsonl): no se traduce.
const char* clave(Pieza p);
std::optional<Pieza> piezaDeClave(std::string_view clave);
/// Qué es, para el operador (marcado con QT_TRANSLATE_NOOP, contexto «modo-seguro»).
const char* descripcion(Pieza p);
/// Desde qué nivel queda apagada.
int nivelDe(Pieza p);

struct ModoSeguro {
    int nivel = 0;          ///< 0 = normal; 1..3
    std::set<Pieza> extra;  ///< apagadas por la atribución de la última caída

    bool activo() const { return nivel > 0 || !extra.empty(); }
    bool apagada(Pieza p) const { return nivel >= nivelDe(p) || extra.count(p) > 0; }
    /// El nivel 3: escenas sin fuentes, x264, no se guarda (el modo seguro de siempre).
    bool escenasVacias() const { return nivel >= kNivelMaximo; }
    std::vector<Pieza> apagadas() const;
    /// Lo que el launcher le pasa a la app: {"--safe-level=N"[, "--safe-off=a,b"]}.
    std::vector<std::string> argumentos() const;
    /// El aviso para el operador (traducido): qué se apagó y que las escenas están.
    std::string aviso() const;
};

/// `--safe-level=N`, `--safe-off=a,b` y el viejo `--safe-mode` (= nivel 3).
ModoSeguro leerArgumentos(int argc, char** argv);

/// El del proceso (lo pone la app al arrancar; vacío en los demás).
ModoSeguro& delProceso();

/// De quién fue una caída: el primer módulo de la pila del hilo que se cayó que
/// no es del sistema ni del propio manejador, y la pieza que le corresponde (si
/// le corresponde alguna). Mira también los símbolos y el nombre del hilo
/// (`ba-ndi-…`, `superres`): un hilo de NDI que se cae adentro de libc es de NDI.
struct Atribucion {
    std::string modulo;          ///< «libgstbasuperres.dylib» (vacío: no se sabe)
    std::optional<Pieza> pieza;  ///< la que el modo seguro puede apagar
};
struct Marco {
    std::string modulo; ///< basename de la biblioteca
    std::string simbolo;
};
Atribucion atribuir(const std::vector<Marco>& pila, std::string_view nombreHilo = {});

/// Bibliotecas del sistema / del runtime, que no se culpan (libc, ntdll, libsystem…).
bool esDelSistema(std::string_view modulo);
/// La pieza de una biblioteca, de un símbolo o de un nombre de hilo (sin distinguir mayúsculas).
std::optional<Pieza> piezaDeTexto(std::string_view texto);

} // namespace ba::fault
