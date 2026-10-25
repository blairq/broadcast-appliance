// src/app/agent/EstadoIA.cpp — qué parte de la asistencia con IA no anda (ver el .hpp)

#include "agent/EstadoIA.hpp"

#include "agent/AgentViewModel.hpp"
#include "agent/ModelosLocales.hpp"
#include "agent/Traductor.hpp"
#include "agent/Voz.hpp"
#include "agent/VozPrompter.hpp"

#include <QJsonDocument>

namespace ba::app::agent {

namespace {
// Las páginas y secciones a donde manda cada problema (RielConfig y SettingsPage).
constexpr const char* kAsistente = "asistente";
constexpr const char* kConfig = "configuracion";
constexpr const char* kPrompter = "prompter";
} // namespace

EstadoIA::EstadoIA(AgentViewModel* agente, ModelosLocales* modelos, Voz* voz, VozPrompter* vozPrompter,
                   Traductor* traductor, QObject* parent)
    : QObject(parent), agente_(agente), modelos_(modelos), voz_(voz), vozPrompter_(vozPrompter),
      traductor_(traductor) {
    pronto_.setSingleShot(true);
    pronto_.setInterval(50);
    connect(&pronto_, &QTimer::timeout, this, &EstadoIA::recalcular);
    const auto avisar = [this]() { pronto_.start(); };
    if (agente_) {
        connect(agente_, &AgentViewModel::configChanged, this, avisar);
        connect(agente_, &AgentViewModel::installChanged, this, avisar);
        connect(agente_, &AgentViewModel::runningChanged, this, avisar);
        connect(agente_, &AgentViewModel::mcpReadyChanged, this, avisar);
        connect(agente_, &AgentViewModel::errorChanged, this, avisar);
    }
    if (modelos_) connect(modelos_, &ModelosLocales::cambio, this, avisar);
    if (voz_) connect(voz_, &Voz::cambio, this, avisar);
    if (vozPrompter_) connect(vozPrompter_, &VozPrompter::cambio, this, avisar);
    if (traductor_) connect(traductor_, &Traductor::cambio, this, avisar);
    recalcular();
}

void EstadoIA::recalcular() {
    QVariantList p;
    int errores = 0, avisos = 0;
    const auto agregar = [&](const QString& id, const QString& titulo, const QString& detalle, bool error,
                             const QString& pagina, const QString& seccion = {}) {
        p << QVariantMap{{"id", id},           {"titulo", titulo},   {"detalle", detalle},
                         {"gravedad", error ? "error" : "aviso"}, {"pagina", pagina}, {"seccion", seccion}};
        ++(error ? errores : avisos);
    };

    // El asistente: instalado, con su clave, corriendo y viendo el sistema (MCP).
    bool usaModeloLocal = false;
    if (agente_) {
        const QVariantMap cur = agente_->current();
        const QString nombre = cur.value("name").toString();
        usaModeloLocal = agente_->provider() == "local";
        if (!agente_->installed())
            agregar("asistente-instalar", "El asistente no está instalado",
                    "Instalalo en la pestaña Asistente (" + nombre + ").", true, kAsistente);
        else if (cur.value("needsKey").toBool() && !agente_->hasKey())
            agregar("asistente-clave", "Falta la clave del asistente",
                    nombre + " necesita su clave de API: pegala en la pestaña Asistente.", true, kAsistente);
        else if (!agente_->running())
            agregar("asistente-arrancar", "El asistente no está corriendo",
                    "Arrancalo en la pestaña Asistente (" + nombre + ")." +
                        (cur.value("auth").toString() == "cuenta"
                             ? " La primera vez, escribí /login en su terminal para entrar con tu cuenta."
                             : QString()),
                    true, kAsistente);
        else if (!agente_->mcpReady())
            agregar("asistente-mcp", "El asistente no ve el sistema",
                    "Está corriendo pero sin conexión MCP con la app: no puede manejar nada.", true, kConfig,
                    "Asistente (MCP)");
        if (!agente_->lastError().isEmpty())
            agregar("asistente-error", "El asistente tuvo un error", agente_->lastError(), false, kAsistente);
    }

    // El modelo local: hace falta si el asistente o la voz del operador lo usan.
    if (voz_ && voz_->usarModeloLocal()) usaModeloLocal = true;
    if (modelos_ && usaModeloLocal) {
        const QString est = modelos_->estado();
        if (modelos_->activo().isEmpty())
            agregar("modelo-elegir", "No hay modelo local elegido",
                    "El asistente o la voz del operador usan el modelo local: elegí y bajá uno.", true, kConfig,
                    "Modelos locales");
        else if (est == "error")
            agregar("modelo-error", "El modelo local no arrancó", modelos_->detalle(), true, kConfig,
                    "Modelos locales");
        else if (est == "apagado")
            agregar("modelo-apagado", "El modelo local está apagado",
                    "Prendé «" + modelos_->activo() + "» (o que arranque solo con la app).", true, kConfig,
                    "Modelos locales");
        else if (est == "cargando")
            agregar("modelo-cargando", "El modelo local está cargando", modelos_->detalle(), false, kConfig,
                    "Modelos locales");
    }

    // La voz del operador: el dictado al asistente (reconocedor y micrófono), la voz
    // que contesta y los auriculares.
    if (voz_ && voz_->disponible()) {
        if (!voz_->lista())
            agregar("voz-reconocedor", "El dictado no está listo", voz_->estado(), false, kConfig,
                    "Voz del sistema");
        if (!voz_->dictadoConMicrofono())
            agregar("voz-microfono", "No hay micrófono para el dictado",
                    "Elegí el micrófono del operador (el que se usa con " + voz_->tecla() + ").", false, kConfig,
                    "Voz del sistema");
        if (voz_->contestarConVoz() && (voz_->vozElegida().isEmpty() || !voz_->vozLista()))
            agregar("voz-contesta", "No hay voz para que conteste el asistente",
                    voz_->vozElegida().isEmpty() ? "Elegí una voz (Voz que contesta)." : "Falta bajar la voz elegida.",
                    false, kConfig, "Voz del sistema");
        const QString aur = voz_->auriculares();
        if (!aur.isEmpty()) {
            bool esta = false;
            for (const QVariant& g : voz_->salidasAudio())
                for (const QVariant& s : g.toMap().value("salidas").toList())
                    if (s.toMap().value("id").toString() == aur) esta = true;
            if (!esta)
                agregar("voz-auriculares", "Los auriculares del operador no están conectados",
                        "Se eligió una salida que no aparece: enchufala o elegí otra.", false, kConfig,
                        "Voz del sistema");
        }
    }

    // La voz del teleprompter (si está prendida y no puede leer).
    if (vozPrompter_ && vozPrompter_->activa()) {
        const QString e = vozPrompter_->estado();
        if (e.startsWith("Sin voz") || e.startsWith("El teleprompter no contesta"))
            agregar("voz-prompter", "La voz del teleprompter no puede leer", e, false, kPrompter);
    }

    // Las traducciones activas que no están andando.
    if (traductor_) {
        if (traductor_->motor().startsWith("error"))
            agregar("traduccion-motor", "El traductor no arrancó", traductor_->motor(), true, kConfig, "Traducción");
        for (const auto& t : traductor_->lista()) {
            if (!t.value("activa", true)) continue;
            const std::string est = t.value("estado", std::string());
            if (est == "lista" || est == "pausada" || est.rfind("error", 0) == 0) continue;
            agregar(QString::fromStdString("traduccion-" + t.value("id", std::string())),
                    QString::fromStdString("Traducción a " + t.value("a", std::string()) + " sin andar"),
                    QString::fromStdString(est), false, kConfig, "Traducción");
        }
    }

    if (p == problemas_ && errores == errores_ && avisos == avisos_) return;
    problemas_ = p;
    errores_ = errores;
    avisos_ = avisos;
    emit cambio();
}

nlohmann::json EstadoIA::estado() const {
    const auto j = nlohmann::json::parse(QJsonDocument::fromVariant(problemas_).toJson().toStdString(), nullptr, false);
    return {{"todo_bien", problemas_.isEmpty()}, {"errores", errores_}, {"avisos", avisos_},
            {"problemas", j.is_array() ? j : nlohmann::json::array()}};
}

} // namespace ba::app::agent
