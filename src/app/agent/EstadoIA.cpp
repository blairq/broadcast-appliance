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
            agregar("asistente-instalar", tr("El asistente no está instalado"),
                    tr("Instalalo en la pestaña Asistente (%1).").arg(nombre), true, kAsistente);
        else if (cur.value("needsKey").toBool() && !agente_->hasKey())
            agregar("asistente-clave", tr("Falta la clave del asistente"),
                    tr("%1 necesita su clave de API: pegala en la pestaña Asistente.").arg(nombre), true, kAsistente);
        else if (!agente_->running())
            agregar("asistente-arrancar", tr("El asistente no está corriendo"),
                    (cur.value("auth").toString() == "cuenta"
                         ? tr("Arrancalo en la pestaña Asistente (%1). La primera vez, escribí /login en su "
                              "terminal para entrar con tu cuenta.")
                         : tr("Arrancalo en la pestaña Asistente (%1)."))
                        .arg(nombre),
                    true, kAsistente);
        else if (!agente_->mcpReady())
            agregar("asistente-mcp", tr("El asistente no ve el sistema"),
                    tr("Está corriendo pero sin conexión MCP con la app: no puede manejar nada."), true, kConfig,
                    "Asistente (MCP)");
        if (!agente_->lastError().isEmpty())
            agregar("asistente-error", tr("El asistente tuvo un error"), agente_->lastError(), false, kAsistente);
    }

    // El modelo local: hace falta si el asistente o la voz del operador lo usan.
    if (voz_ && voz_->usarModeloLocal()) usaModeloLocal = true;
    if (modelos_ && usaModeloLocal) {
        const QString est = modelos_->estado();
        if (modelos_->activo().isEmpty())
            agregar("modelo-elegir", tr("No hay modelo local elegido"),
                    tr("El asistente o la voz del operador usan el modelo local: elegí y bajá uno."), true, kConfig,
                    "Modelos locales");
        else if (est == "error")
            agregar("modelo-error", tr("El modelo local no arrancó"), modelos_->detalle(), true, kConfig,
                    "Modelos locales");
        else if (est == "apagado")
            agregar("modelo-apagado", tr("El modelo local está apagado"),
                    tr("Prendé «%1» (o que arranque solo con la app).").arg(modelos_->activo()), true, kConfig,
                    "Modelos locales");
        else if (est == "cargando")
            agregar("modelo-cargando", tr("El modelo local está cargando"), modelos_->detalle(), false, kConfig,
                    "Modelos locales");
    }

    // La voz del operador: el dictado al asistente (reconocedor y micrófono), la voz
    // que contesta y los auriculares.
    if (voz_ && voz_->disponible()) {
        if (!voz_->lista())
            agregar("voz-reconocedor", tr("El dictado no está listo"), voz_->estado(), false, kConfig,
                    "Voz del sistema");
        // Los dos reconocedores en memoria a la vez (Parakeet y Nemotron, por consumidores
        // distintos): anda, pero ocupa el doble. Se elige en la pestaña Agentes.
        if (const QString dos = voz_->reconocedores().value("alerta").toString(); !dos.isEmpty())
            agregar("voz-dos-reconocedores", tr("Están cargados los dos reconocedores de voz"), dos, false, "agentes");
        if (!voz_->dictadoConMicrofono())
            agregar("voz-microfono", tr("No hay micrófono para el dictado"),
                    tr("Elegí el micrófono del operador (el que se usa con %1).").arg(voz_->tecla()), false, kConfig,
                    "Voz del sistema");
        if (voz_->contestarConVoz() && (voz_->vozElegida().isEmpty() || !voz_->vozLista()))
            agregar("voz-contesta", tr("No hay voz para que conteste el asistente"),
                    voz_->vozElegida().isEmpty() ? tr("Elegí una voz (Voz que contesta).")
                                                 : tr("Falta bajar la voz elegida."),
                    false, kConfig, "Voz del sistema");
        const QString aur = voz_->auriculares();
        if (!aur.isEmpty()) {
            bool esta = false;
            for (const QVariant& g : voz_->salidasAudio())
                for (const QVariant& s : g.toMap().value("salidas").toList())
                    if (s.toMap().value("id").toString() == aur) esta = true;
            if (!esta)
                agregar("voz-auriculares", tr("Los auriculares del operador no están conectados"),
                        tr("Se eligió una salida que no aparece: enchufala o elegí otra."), false, kConfig,
                        "Voz del sistema");
        }
    }

    // La voz del teleprompter (si está prendida y no puede leer).
    if (vozPrompter_ && vozPrompter_->activa()) {
        const QString e = vozPrompter_->estado();
        if (vozPrompter_->noPuedeLeer())
            agregar("voz-prompter", tr("La voz del teleprompter no puede leer"), e, false, kPrompter);
    }

    // Las traducciones activas que no están andando.
    if (traductor_) {
        if (traductor_->motor().startsWith("error"))
            agregar("traduccion-motor", tr("El traductor no arrancó"), traductor_->motor(), true, kConfig, "Traducción");
        for (const auto& t : traductor_->lista()) {
            if (!t.value("activa", true)) continue;
            const std::string est = t.value("estado", std::string());
            if (est == "lista" || est == "pausada" || est.rfind("error", 0) == 0) continue;
            agregar(QString::fromStdString("traduccion-" + t.value("id", std::string())),
                    tr("Traducción a %1 sin andar").arg(QString::fromStdString(t.value("a", std::string()))),
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
