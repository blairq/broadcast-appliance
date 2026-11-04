// ============================================================================
// src/app/emision/VigiaDisco.cpp — ver VigiaDisco.hpp
// ============================================================================

#include "emision/VigiaDisco.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

#include "Salidas.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "diag/Registro.hpp"
#include "diag/SalidaAsincrona.hpp"
#include "emision/EmisionSalidas.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/ArchivoConfig.hpp"
#include "platform/Volumen.hpp"

namespace ba::app {

using json = nlohmann::json;

namespace {
// Umbrales: entra en apuros con 3 s esperando al disco (o el registro trabado 3 s
// escribiendo); sale con todo al día durante 30 s. Se ajustan en Configuración →
// Grabación (ponerUmbrales). Para estar «al día», esto:
constexpr double kAtrasoBienS = 0.5;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void VigiaDisco::cargarModo() {
    const std::string m = config::leerObjeto(ba::persist::archivoConfig("disco.json"))
                              .value("priorizar_programa", std::string("preguntar"));
    modo_ = m == "auto" ? 1 : m == "nunca" ? 2 : 0;
}

void VigiaDisco::ponerUmbrales(double apurosS, int recuperadoS) {
    const double s = std::clamp(apurosS, 0.5, 60.0);
    apurosS_ = s;
    registroTrabadoMs_ = static_cast<std::int64_t>(s * 1000);
    bienDuranteMs_ = static_cast<std::int64_t>(std::clamp(recuperadoS, 5, 3600)) * 1000;
}

std::string VigiaDisco::modo() const {
    const int m = modo_.load();
    return m == 1 ? "auto" : m == 2 ? "nunca" : "preguntar";
}

std::string VigiaDisco::setModo(const std::string& modo) {
    if (modo != "preguntar" && modo != "auto" && modo != "nunca") return ba::i18n::traducir("emision", "modo: preguntar, auto o nunca");
    modo_ = modo == "auto" ? 1 : modo == "nunca" ? 2 : 0;
    config::guardar(ba::persist::archivoConfig("disco.json"), {{"priorizar_programa", modo}});
    // En apuros y ahora «auto»: ya.
    if (modo == "auto") m_.emision_->enSalidas("modoDisco", [this]() {
            if (apuros_ && !prioridad_) aplicarPrioridad(true, true);
            return std::string();
        });
    return {};
}

void VigiaDisco::priorizar(bool si) {
    m_.emision_->enSalidas("priorizarPrograma", [this, si]() {
        aplicarPrioridad(si, false);
        return std::string();
    });
}

void VigiaDisco::medirRestante() {
    const std::int64_t ahora = ahoraMs();
    if (restanteMs_ != 0 && ahora - restanteMs_ < 5000) return;
    restanteMs_ = ahora;
    namespace fs = std::filesystem;
    using ba::i18n::traducir;
    // Todo lo que escribe ahora, de todas las salidas: cada disco reparte su espacio
    // libre entre lo que va a él (la copia en el mismo disco lo gasta el doble de rápido).
    std::vector<ba::platform::EscrituraDisco> e;
    json mismo = json::array();
    for (const auto& s : m_.emision_->emisiones()) {
        const std::string de = s->porOmision() ? std::string() : " (" + s->id + ")";
        const bool graba = s->rec && s->rec->grabando(), copia = s->recCopia && s->recCopia->grabando();
        const fs::path dirRec = fs::path(s->recordPath).parent_path(), dirCopia = fs::path(s->recordCopyPath).parent_path();
        if (graba) e.push_back({traducir("emision", "grabación") + de, dirRec, static_cast<double>(s->kbpsRec)});
        if (copia) e.push_back({traducir("emision", "copia de la grabación") + de, dirCopia, static_cast<double>(s->kbpsRec)});
        // Las dos en el mismo disco: si el disco falla o se llena, se pierden las dos.
        if (graba && copia && ba::platform::mismoVolumen(dirRec, dirCopia))
            mismo.push_back({{"salida", s->id}, {"grabacion", dirRec.string()}, {"copia", dirCopia.string()}});
        if (s->clean && s->clean->grab && s->clean->grab->grabando())
            e.push_back({traducir("emision", "señal limpia a disco") + de, fs::path(s->clean->path).parent_path(), static_cast<double>(s->clean->kbps)});
    }
    for (const auto& [n, r] : m_.emision_->grabacionesAparte())
        if (r.grab && r.grab->grabando())
            e.push_back({ba::i18n::llenar(traducir("emision", "ISO %1"), {n}), fs::path(r.path).parent_path(), static_cast<double>(r.kbps)});
    restante_ = ba::platform::aJson(ba::platform::discoRestante(e));
    mismoVolumen_ = std::move(mismo);
}

json VigiaDisco::estado(bool grabaPrograma) const {
    return {{"apuros", apuros_},
            {"restante", restante_},
            {"mismo_volumen", mismoVolumen_},
            {"atraso_s", atraso_},
            {"registro_trabado_ms", registroTrabado_},
            {"prioridad", prioridad_},
            {"automatica", prioridadAuto_},
            {"modo", modo()},
            {"pausadas", pausadas_},
            {"graba_programa", grabaPrograma},
            {"desde_s", apuros_ ? (ahoraMs() - apurosDesdeMs_) / 1000 : 0}};
}

void VigiaDisco::aplicarPrioridad(bool si, bool automatica) {
    if (si == prioridad_) return;
    prioridad_ = si;
    prioridadAuto_ = si && automatica;
    pausadas_.clear();
    auto pausar = [&](motor::Grabacion* g, const std::string& nombre) {
        if (g == nullptr || !g->grabando()) return;
        g->pausar(si);
        if (si) pausadas_.push_back(nombre);
    };
    for (auto& [n, r] : m_.emision_->grabacionesAparte()) pausar(r.grab.get(), ba::i18n::llenar(ba::i18n::traducir("emision", "ISO %1"), {n}));
    // La grabación del programa de cada salida sigue; lo demás de cada una, en pausa.
    for (const auto& s : m_.emision_->emisiones()) {
        const std::string de = s->porOmision() ? std::string() : " (" + s->id + ")";
        if (s->replay && s->replay->rep) {
            s->replay->rep->pausar(si);
            if (si && s->replay->rep->andando()) pausadas_.push_back(ba::i18n::traducir("emision", "repetición") + de);
        }
        // La copia, solo si va al mismo disco (en otro no molesta, y es el respaldo).
        const std::string dirProg = s->recordPath.empty() ? std::string() : std::filesystem::path(s->recordPath).parent_path().string();
        if (s->recCopia && (!si || dirProg.empty() || ba::platform::mismoVolumen(dirProg, std::filesystem::path(s->recordCopyPath).parent_path())))
            pausar(s->recCopia.get(), ba::i18n::traducir("emision", "copia de la grabación") + de);
        if (s->clean && s->clean->grab) pausar(s->clean->grab.get(), ba::i18n::traducir("emision", "señal limpia a disco") + de);
    }
    ba::diag::retenerSalidaEstandar(si);
    if (si) pausadas_.push_back(ba::i18n::traducir("emision", "registro (en memoria)"));
    std::string lista;
    for (const auto& p : pausadas_) lista += (lista.empty() ? "" : ", ") + p;
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "disco", 0,
                              si ? ba::i18n::llenar(automatica ? ba::i18n::traducir("disco", "el disco, para la grabación del programa (solo): en pausa %1")
                                                               : ba::i18n::traducir("disco", "el disco, para la grabación del programa: en pausa %1"),
                                                    {lista})
                                 : ba::i18n::traducir("disco", "el disco vuelve a ser de todos: siguen las grabaciones pausadas y el registro"));
}

void VigiaDisco::vigilar() {
    using AvisoMotor = BroadcastEngine::AvisoMotor;
    const std::int64_t ahora = ahoraMs();
    double atraso = 0;
    auto mirar = [&](const motor::Grabacion* g) {
        if (g != nullptr && g->grabando() && !g->pausada()) atraso = std::max(atraso, g->atrasoS());
    };
    bool grabaPrograma = false;
    for (const auto& s : m_.emision_->emisiones()) {
        mirar(s->rec.get());
        mirar(s->recCopia.get());
        if (s->clean) mirar(s->clean->grab.get());
        if (s->replay && s->replay->rep) atraso = std::max(atraso, s->replay->rep->atrasoS());
        grabaPrograma = grabaPrograma || (s->rec && s->rec->grabando());
    }
    for (auto& [n, r] : m_.emision_->grabacionesAparte()) mirar(r.grab.get());
    const ba::diag::EstadoSalida reg = ba::diag::estadoSalidaEstandar();
    atraso_ = atraso;
    registroTrabado_ = reg.escribiendoMs;
    const double apurosS = apurosS_.load();
    const bool mal = atraso > apurosS || reg.escribiendoMs > registroTrabadoMs_.load();
    const bool bien = atraso < kAtrasoBienS && reg.escribiendoMs < 500;
    if (!apuros_ && mal) {
        apuros_ = true;
        apurosDesdeMs_ = ahora;
        bienDesdeMs_ = 0;
        char t[64];
        std::snprintf(t, sizeof t, "%.0f s", atraso);
        const std::string causa = atraso > apurosS ? ba::i18n::llenar(ba::i18n::traducir("emision", "lo grabado espera %1 para entrar al disco"), {t})
                                                   : ba::i18n::traducir("emision", "el registro no puede escribir");
        if (modo_.load() == 1 && grabaPrograma) {
            aplicarPrioridad(true, true);
            m_.avisar({"disco_apuros", AvisoMotor::Nivel::Error, ba::i18n::llenar(ba::i18n::traducir("emision", "El disco no da abasto (%1): se le dio el disco a la grabación del programa. "
                                                                                                                "Las ISO, la repetición y el registro siguen cuando el disco se recupere."),
                                                                                  {causa})});
        } else {
            m_.avisar({"disco_apuros", AvisoMotor::Nivel::Error, ba::i18n::llenar(
                grabaPrograma && modo_.load() == 0
                    ? ba::i18n::traducir("emision", "El disco no da abasto (%1). Conviene darle el disco a la grabación del programa (pausar ISO, repetición y registro): "
                                                    "el botón está en el aviso de arriba.")
                    : ba::i18n::traducir("emision", "El disco no da abasto (%1)."),
                {causa})});
        }
    } else if (apuros_) {
        if (!bien) {
            bienDesdeMs_ = 0;
        } else if (bienDesdeMs_ == 0) {
            bienDesdeMs_ = ahora;
        } else if (ahora - bienDesdeMs_ > bienDuranteMs_.load()) {
            apuros_ = false;
            const bool habia = prioridad_;
            aplicarPrioridad(false, false);
            m_.avisar({"disco_bien", AvisoMotor::Nivel::Bien,
                       habia ? ba::i18n::traducir("emision", "El disco anda bien de nuevo: siguen las ISO, la repetición y el registro.")
                             : ba::i18n::traducir("emision", "El disco anda bien de nuevo.")});
        }
    }
}

} // namespace ba::app
