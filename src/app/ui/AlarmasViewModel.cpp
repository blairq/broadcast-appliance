// src/app/ui/AlarmasViewModel.cpp — ver el .hpp

#include "ui/AlarmasViewModel.hpp"

#include "agent/AlarmaSonora.hpp"
#include "agent/EntradasAlarmas.hpp"
#include "emision/AvisoFueraDeUi.hpp"
#include "i18n/I18n.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlEngine>

namespace ba::app {

using json = nlohmann::json;

AlarmasViewModel::AlarmasViewModel(agent::AlertInputs deLaInterfaz, agent::AlertInputs directas,
                                   const std::filesystem::path& datos, QObject* parent)
    : QObject(parent),
      entradas_(std::make_unique<agent::EntradasAlarmas>(std::move(deLaInterfaz), std::move(directas))),
      sonido_(std::make_unique<agent::AlarmaSonora>(datos)) {
    entradas_->refrescar();
    motor_ = std::make_unique<agent::AlertEngine>(entradas_->paraElVigia(), (datos / "Config" / "alertas.json").string());
    // Los notificadores, en el hilo del vigía (conexión directa): salen aunque la interfaz
    // no ande. Lo demás, en la interfaz (este objeto vive en ella: la conexión es encolada).
    connect(motor_.get(), &agent::AlertEngine::fired, motor_.get(), [this](const json& ev) { enHiloDelVigia(ev); },
            Qt::DirectConnection);
    connect(motor_.get(), &agent::AlertEngine::fired, this, [this](const json& ev) { enLaInterfaz(ev); });
    // La vista se entera por el modelo.
    connect(motor_.get(), &agent::AlertEngine::alarmasCambio, this, &AlarmasViewModel::releer);
    releer();
    motor_->start(250);
}

AlarmasViewModel::~AlarmasViewModel() = default;

void AlarmasViewModel::refrescarFoto() { entradas_->refrescar(); }

QObject* AlarmasViewModel::sonido() const {
    // Sin padre y devuelto a QML: que no se lo quede su recolector (se borraba dos veces al
    // cerrar). Es de acá.
    QQmlEngine::setObjectOwnership(sonido_.get(), QQmlEngine::CppOwnership);
    return sonido_.get();
}

void AlarmasViewModel::releer() {
    const json a = motor_->activas();
    bool critica = false;
    for (const auto& e : a)
        if (e.value("sonido", std::string()) == "critico" && !e.value("silenciada", false)) critica = true;
    activas_ = QJsonDocument::fromJson(QByteArray::fromStdString(a.dump(-1, ' ', false, json::error_handler_t::replace)))
                   .array()
                   .toVariantList();
    critica_ = critica;
    sonido_->criticaActiva(critica_);
    emit cambio();
}

int AlarmasViewModel::reconocer(qint64 id) { return motor_->reconocer(static_cast<std::uint64_t>(id)); }

bool AlarmasViewModel::silenciar(const QString& disparador, bool si) {
    return !motor_->update(disparador.toStdString(), json{{"silenciada", si}}).contains("error");
}

void AlarmasViewModel::enHiloDelVigia(const json& ev) {
    AvisoFueraDeUi* aviso = aviso_.load();
    if (aviso == nullptr) return;
    AvisoFueraDeUi::Alarma a;
    const json& rid = ev["ruleId"];
    a.id = "alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump());
    a.titulo = "⚠ " + ev.value("ruleName", ba::i18n::traducir("alertas", "Disparador"));
    a.texto = ev.value("llmText", std::string());
    a.controlWeb = false; // el control web la recibe por el bus (enLaInterfaz)
    a.notificarTodos = false;
    for (const auto& n : ev.value("notificar", json::array()))
        if (n.is_string()) a.notificar.push_back(n.get<std::string>());
    if (a.notificar.empty()) return;
    QPointer<AlarmasViewModel> vista(this);
    a.resultado = [vista](const std::string& id, bool ok, const std::string& d) {
        if (ok || !vista) return;
        QMetaObject::invokeMethod(vista, [vista, id, d]() {
            if (vista)
                emit vista->notify(tr("El aviso por «%1» no salió: %2").arg(QString::fromStdString(id), QString::fromStdString(d)),
                                   "warn");
        });
    };
    aviso->avisar(std::move(a));
}

void AlarmasViewModel::enLaInterfaz(const json& ev) {
    if (publicar_) {
        const json& rid = ev["ruleId"];
        publicar_("alarma/" + (rid.is_string() ? rid.get<std::string>() : rid.dump(-1, ' ', false, json::error_handler_t::replace)),
                  {{"nombre", ev.value("ruleName", std::string())},
                   {"tipo", ev.value("type", std::string())},
                   {"texto", ev.value("llmText", std::string())},
                   {"reconocer", ev.value("reconocer", false)},
                   {"id", ev.value("id", 0ULL)}});
    }
    for (const auto& n : ev.value("notify", json::array()))
        if (n == "ui") emit notify("⚠ " + QString::fromStdString(ev.value("text", "")), "warn");
    // Una alarma silenciada sigue en la lista (con su color), pero no suena.
    if (!ev.value("silenciada", false)) sonido_->sonar(QString::fromStdString(ev.value("sonido", std::string("ninguno"))));
}

} // namespace ba::app
