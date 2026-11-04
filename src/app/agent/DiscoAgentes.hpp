// ============================================================================
// src/app/agent/DiscoAgentes.hpp — lo que el asistente lee y escribe en el disco
//
// Todo esto corre FUERA del hilo de la interfaz: en el hilo de fondo del
// AgentViewModel (fondo_, uno solo, de a una cosa) o en el de la cola de descargas
// (la instalación). El modelo guarda lo leído (qué hay instalado, las versiones
// elegidas, qué conversaciones guardó cada motor, lo que ocupa cada uno) y la vista
// se engancha a eso; acá no hay estado.
//
// La carpeta de cada motor: <datos>/Agentes/<motor>/<versión>/ con un `.ok`
// ({version, exe, fecha}) cuando la instalación quedó entera. Sus perfiles (login,
// chats): <datos>/Credenciales/agentes/<motor>/.
// ============================================================================
#pragma once

#include "net/Descargas.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ba::app::agent::disco {

/// Los motores que se instalan desde la pestaña.
inline const char* const kMotores[] = {"claude", "antigravity", "opencode", "pi"};

/// Un recurso compilado en el programa (qrc: no es el disco), tal cual o como JSON.
QByteArray recurso(const QString& qrc);
nlohmann::json recursoJson(const QString& qrc);

/// Agentes/versiones.json: las versiones elegidas desde la pestaña ({} si no hay).
std::filesystem::path archivoElegidas(const std::filesystem::path& raiz);
nlohmann::json leerElegidas(const std::filesystem::path& raiz);

/// La instalación de un motor que se puede usar.
struct Usable {
    QString dir;            ///< vacío: no hay ninguna con `.ok`
    QString exe;            ///< el que registró su `.ok` (vacío en las viejas: el del pin)
    bool piSinJiti = false; ///< pi instalado antes de la guarda: a reinstalar
};
/// La fijada si está; si no, la más nueva que haya quedado de la MISMA versión mayor.
Usable buscarUsable(const std::filesystem::path& raiz, const QString& runtime, const QString& fijada);

/// Lo instalado de cada motor, según las versiones fijadas (agentes.json con las elegidas encima).
struct Estado {
    nlohmann::json elegidas = nlohmann::json::object();
    std::map<QString, Usable> usables;
};
Estado leer(const std::filesystem::path& raiz, const nlohmann::json& fijadas);

// --- Limpieza -------------------------------------------------------------------------

/// Por motor: versiones (cuál se usa), bytes del programa, de los chats y del perfil; y los
/// espacios de trabajo. `enUso`: motor → carpeta en uso; `motorElegido`: el del asistente.
QVariantList revisarLimpieza(const std::filesystem::path& raiz, const std::map<QString, QString>& enUso,
                             const QString& motorElegido);
/// Borra los chats de un motor ("" = de todos, y los espacios de trabajo). Los bytes liberados.
double borrarChats(const std::filesystem::path& raiz, const QString& runtime);
/// Borra las versiones que no se usan. Los bytes liberados; en `borradas`, cuáles.
double borrarVersionesViejas(const std::filesystem::path& raiz, const QString& runtime,
                             const std::map<QString, QString>& enUso, QStringList& borradas);
/// Borra el programa del motor (y con `deCero`, su perfil y los espacios de trabajo).
void borrarMotor(const std::filesystem::path& raiz, const QString& runtime, bool deCero);

// --- Conversaciones guardadas ---------------------------------------------------------

/// Claude guardó algo de esa conversación (<perfil>/projects/<carpeta>/<id>.jsonl): si no,
/// `--resume` no la encuentra.
bool claudeGuardada(const std::filesystem::path& raiz, const QString& id);
/// Antigravity: la última conversación escrita desde la carpeta `ws` desde `desdeMs` ("" si ninguna).
QString ultimaAntigravity(const std::filesystem::path& raiz, const QString& ws, qint64 desdeMs);

// --- La instalación (en el hilo de la cola de descargas) --------------------------------

struct Item {
    std::string url, sha, kind, dest;
    std::string sha512 = {}; ///< base64, como lo publica npm (si no hay sha256)
};
struct Instalacion {
    std::vector<Item> items;
    QString dir;     ///< el destino final (la versión fijada)
    QString staging; ///< dónde se arma (recién al final reemplaza a `dir`)
    QString bajado;  ///< lo bajado, hasta que se instala (una descarga cortada sigue)
    QString runtime, exeName, script, version;
};
/// Baja, verifica, descomprime y deja la instalación en su lugar. "" o el error (traducido).
/// `aviso`: el texto de lo que está haciendo (Verificando…, Descomprimiendo…).
std::string instalar(net::Descargas::Contexto& ctx, const Instalacion& in,
                     const std::function<void(const QString&)>& aviso);

} // namespace ba::app::agent::disco
