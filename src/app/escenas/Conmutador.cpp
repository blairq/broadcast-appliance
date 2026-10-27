// src/app/escenas/Conmutador.cpp — el conmutador de escenas (ver el .hpp)

#include "escenas/Conmutador.hpp"

#include "i18n/I18n.hpp"

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"

#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <fstream>

namespace ba::app::escenas {

Conmutador::Conmutador(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    std::ifstream f(rt_.dataRoot() / "Config" / "transiciones.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_object()) {
        transicionesEntrada_ = j.value("entrada", nlohmann::json::object());
        for (const auto& x : j.value("afv", nlohmann::json::array()))
            if (x.is_string()) afv_.insert(QString::fromStdString(x.get<std::string>()));
        // El nivel al que vuelve cada fuente AFV cuando entra (el del operador): sin
        // esto, tras reiniciar el primer fundido subía a 0 dB.
        const nlohmann::json niveles = j.value("afv_niveles", nlohmann::json::object()); // .items() de un temporal: UB
        for (const auto& [f, v] : niveles.items())
            if (v.is_number()) afvVolumen_[QString::fromStdString(f)] = v.get<float>();
    }
}

std::string Conmutador::alAire() const { return rt_.engine().currentScene(); }

std::string Conmutador::cortar(const std::string& escena, const std::string& tipoPedido, int msPedido, const char* via) {
    const std::string antes = alAire();
    std::string tipo = tipoPedido;
    int ms = msPedido;
    // Sin pedido explícito: la transición de entrada de la escena (si tiene).
    if (tipo.empty() && transicionesEntrada_.contains(escena)) {
        const auto& e = transicionesEntrada_[escena];
        tipo = e.value("tipo", std::string());
        ms = e.value("ms", -1);
    }
    const bool ok = tipo.empty() ? rt_.setScene(escena) : rt_.setSceneCon(escena, tipo, ms);
    if (!ok)
        return rt_.lastError().empty()
                   ? ba::i18n::llenar(ba::i18n::traducir("aire", "no se pudo poner «%1» al aire"), {escena})
                   : rt_.lastError();
    // Cada cambio del aire al registro, con por dónde vino: «la cambia y vuelve sola»
    // se ve acá (quién la devolvió y cuándo).
    ba::diag::registro().linea(ba::diag::Nivel::Info, "aire", 0,
                               tipo.empty() ? ba::i18n::llenar(ba::i18n::traducir("aire", "al aire: «%1» (salió «%2», por %3)"),
                                                               {escena, antes, via})
                                            : ba::i18n::llenar(ba::i18n::traducir("aire", "al aire: «%1» (salió «%2», por %3, %4)"),
                                                               {escena, antes, via, tipo}));
    aplicarAfv(escena, tipo == "cut" ? 0 : (ms < 0 ? rt_.config().transition.durationMs : ms));
    rt_.save(); // la escena al aire vuelve igual tras un corte de luz
    emit alAireCambio(QString::fromStdString(escena));
    return {};
}

std::string Conmutador::poner(const std::string& escena, const std::string& tipo, int ms) {
    return cortar(escena, tipo, ms, ba::i18n::traducir("aire", "corte directo").c_str());
}

std::string Conmutador::armar(const std::string& escena) {
    if (escena == alAire())
        // Armar lo que ya está al aire deja el TAKE sin sentido (cortaría a lo mismo).
        return ba::i18n::llenar(ba::i18n::traducir("aire", "«%1» ya está al aire: armá otra para el próximo take."), {escena});
    const auto nombres = rt_.engine().sceneNames();
    if (!escena.empty() && std::find(nombres.begin(), nombres.end(), escena) == nombres.end())
        return ba::i18n::llenar(ba::i18n::traducir("aire", "no existe la escena «%1»"), {escena});
    setArmadaYAvisar(escena);
    return {};
}

std::string Conmutador::take(const std::string& tipo, int ms) {
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada: elegí una en el bus de preview.");
    const std::string entra = armada_;
    const std::string sale = alAire();
    if (std::string e = cortar(entra, tipo, ms, "TAKE"); !e.empty()) return e;
    // El plano que sale del aire queda armado: en una mesa, después de un take
    // el preview tiene lo anterior y volver es un solo golpe.
    setArmadaYAvisar(sale);
    return {};
}

std::string Conmutador::tbar(double t, bool soltar) {
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada para la T-bar.");
    const std::string entra = armada_;
    const std::string sale = alAire();
    bool llego = false;
    if (!rt_.engine().transicionManual(entra, static_cast<float>(t), soltar, &llego)) return rt_.engine().lastError();
    if (llego) {
        rt_.config().currentScene = entra;
        aplicarAfv(entra, 0);
        rt_.save();
        emit alAireCambio(QString::fromStdString(entra));
        setArmadaYAvisar(sale);
    }
    return {};
}

void Conmutador::setArmadaYAvisar(const std::string& escena) {
    if (escena == armada_) return;
    armada_ = escena;
    emit armadaCambio(QString::fromStdString(escena));
}

void Conmutador::aplicarAfv(const std::string& destino, int ms) {
    if (afv_.isEmpty()) return;
    bool guardarNiveles = false;
    const auto enDestino = rt_.engine().sceneSources(destino);
    for (const QString& f : afv_) {
        const std::string n = f.toStdString();
        const bool entra = std::find(enDestino.begin(), enDestino.end(), n) != enDestino.end();
        const float actual = rt_.engine().sourceVolume(n);
        if (!entra && actual > 0.001f && afvVolumen_.value(f, -1.0f) != actual) {
            afvVolumen_[f] = actual;
            guardarNiveles = true;
        }
        const float objetivo = entra ? afvVolumen_.value(f, 1.0f) : 0.0f;
        // Fundido en pasos de 30 ms, con la duración de la transición.
        const int pasos = std::max(1, ms / 30);
        for (int i = 1; i <= pasos; ++i) {
            const float v = actual + (objetivo - actual) * static_cast<float>(i) / static_cast<float>(pasos);
            QTimer::singleShot(i * 30, this, [this, n, v]() { rt_.engine().setSourceVolume(n, v); });
        }
    }
    if (guardarNiveles) guardar();
}

std::string Conmutador::setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms) {
    static const std::vector<std::string> tipos{"", "cut", "fade", "swipe", "slide", "fade_to_color"};
    if (std::find(tipos.begin(), tipos.end(), tipo) == tipos.end())
        return ba::i18n::traducir("aire", "tipo: cut, fade, swipe, slide o fade_to_color (vacío = la global)");
    if (tipo.empty())
        transicionesEntrada_.erase(escena);
    else
        transicionesEntrada_[escena] = {{"tipo", tipo}, {"ms", ms}};
    guardar();
    return {};
}

nlohmann::json Conmutador::transicionEntrada(const std::string& escena) const {
    if (!transicionesEntrada_.contains(escena)) return nlohmann::json::object();
    return transicionesEntrada_[escena];
}

void Conmutador::setAfv(const std::string& fuente, bool sigue) {
    const QString f = QString::fromStdString(fuente);
    if (sigue)
        afv_.insert(f);
    else
        afv_.remove(f);
    guardar();
    emit afvCambio();
}

std::vector<std::string> Conmutador::afvFuentes() const {
    std::vector<std::string> out;
    for (const QString& f : afv_) out.push_back(f.toStdString());
    std::sort(out.begin(), out.end());
    return out;
}

void Conmutador::guardar() const {
    nlohmann::json afv = nlohmann::json::array();
    for (const std::string& f : afvFuentes()) afv.push_back(f);
    nlohmann::json niveles = nlohmann::json::object();
    for (auto it = afvVolumen_.begin(); it != afvVolumen_.end(); ++it)
        if (afv_.contains(it.key())) niveles[it.key().toStdString()] = it.value();
    const nlohmann::json j{{"entrada", transicionesEntrada_}, {"afv", afv}, {"afv_niveles", niveles}};
    // Aparte y reemplazar: un corte a mitad no deja el archivo trunco.
    QSaveFile o(QString::fromStdString((rt_.dataRoot() / "Config" / "transiciones.json").string()));
    if (!o.open(QIODevice::WriteOnly)) return;
    o.write(QByteArray::fromStdString(j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
    o.commit();
}

} // namespace ba::app::escenas
