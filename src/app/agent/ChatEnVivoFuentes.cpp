// ============================================================================
// src/app/agent/ChatEnVivoFuentes.cpp — qué servidor usa cada proveedor (ver ChatEnVivo.hpp)
//
// Aparte del cliente para que el test de los turnos (test_registro_agente_totem) no arrastre
// las credenciales ni la configuración del modelo propio.
// ============================================================================
#include "agent/ChatEnVivo.hpp"

#include <QByteArray>
#include <QString>

#include "i18n/I18n.hpp"
#include "net/Credentials.hpp"
#include "platform/Paths.hpp"

namespace ba::app::agent {

bool ChatEnVivo::fuenteDeProveedor(const std::string& proveedor, const std::string& modelo,
                                   const std::filesystem::path& dataRoot, FuenteMirada& out, std::string* falta) {
    if (proveedor == "propio") {
        if (!fuentePropia(dataRoot, out, falta)) return false;
        if (!modelo.empty()) {
            // En la pestaña se elige como lo llama opencode («propio-<canal>/<modelo>»): acá va
            // solo el modelo.
            const auto barra = modelo.find('/');
            out.modelo = barra == std::string::npos ? modelo : modelo.substr(barra + 1);
        }
        return true;
    }
    // Los de la nube con la API de OpenAI y una clave. Las bases son las públicas de cada uno.
    struct Nube {
        const char* id;
        const char* url;
        const char* clave;
        const char* nombre;
        const char* modeloDeFabrica;
        const char* prefijo; ///< lo que la pestaña le agrega al modelo y el servidor no quiere
    };
    static const Nube nubes[] = {
        {"openrouter", "https://openrouter.ai/api/v1", "openrouter-api-key", "OpenRouter", "google/gemini-2.5-flash",
         "openrouter/"},
        {"gemini-api", "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-api-key", "Gemini",
         "gemini-2.5-flash", ""},
    };
    for (const Nube& n : nubes) {
        if (proveedor != n.id) continue;
        QByteArray clave;
        const QString cred = QString::fromStdWString(ba::platform::credentialsDir().wstring());
        if (!ba::app::cred::read(cred, n.clave, clave) || clave.trimmed().isEmpty()) {
            if (falta != nullptr) *falta = ba::i18n::llenar(ba::i18n::traducir("asistente", "falta la clave de %1 (pestaña Agentes)"), {n.nombre});
            return false;
        }
        out.url = n.url;
        out.clave = clave.trimmed().toStdString();
        out.modelo = modelo.empty() ? std::string(n.modeloDeFabrica) : modelo;
        if (*n.prefijo != '\0' && out.modelo.rfind(n.prefijo, 0) == 0) out.modelo.erase(0, std::string(n.prefijo).size());
        out.nombre = n.nombre;
        return true;
    }
    if (falta != nullptr)
        *falta = ba::i18n::traducir("asistente",
                                    "ese motor solo se usa por su agente de consola (tarda en arrancar): para el tótem "
                                    "elegí el modelo local, el propio, OpenRouter o Gemini con clave");
    return false;
}

} // namespace ba::app::agent
