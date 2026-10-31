// ============================================================================
// src/app/arranque/EnergiaAire.cpp — ver EnergiaAire.hpp
// ============================================================================
#include "arranque/EnergiaAire.hpp"

#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

namespace ba::app {

namespace {

platform::PoliticaEnergia politica() {
    platform::PoliticaEnergia p;
    p.porcentaje = ba::ajustes::entero("energia.corte.porcentaje", 20);
    p.minutos = ba::ajustes::entero("energia.corte.minutos", 10);
    p.cerrarGrabaciones = ba::ajustes::siNo("energia.corte.cerrarGrabaciones", true);
    return p;
}

const char* nombre(platform::AccionEnergia a) {
    switch (a) {
        case platform::AccionEnergia::Avisar: return "avisar";
        case platform::AccionEnergia::CerrarGrabaciones: return "cerrar_grabaciones";
        default: return "nada";
    }
}

bool igual(const platform::EstadoEnergia& a, const platform::EstadoEnergia& b) {
    return a.conocido == b.conocido && a.hayBateria == b.hayBateria && a.enBateria == b.enBateria &&
           a.porcentaje == b.porcentaje && a.minutos == b.minutos && a.fuente == b.fuente;
}

} // namespace

EnergiaAire::EnergiaAire(Acciones acciones, QObject* parent) : QObject(parent), acciones_(std::move(acciones)) {}

void EnergiaAire::vigilar(int cadaMs) {
    timer_.setInterval(cadaMs);
    connect(&timer_, &QTimer::timeout, this, [this]() { evaluar(platform::leerEnergia()); }, Qt::UniqueConnection);
    timer_.start();
    evaluar(platform::leerEnergia());
}

void EnergiaAire::evaluar(const platform::EstadoEnergia& e) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    const platform::AccionEnergia a = platform::decidirEnergia(e, politica());
    const bool cambio_ = !igual(e, ultimo_) || a != accion_;
    if (e.enBateria && !ultimo_.enBateria)
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "energia", 0,
                                   llenar(traducir("energia", "corte de luz: el equipo anda con %1 (%2 %)"),
                                          {e.fuente == "ups" ? traducir("energia", "el UPS") : traducir("energia", "la batería"),
                                           std::to_string(e.porcentaje)}));
    if (a == platform::AccionEnergia::CerrarGrabaciones && !acciones_.emergencia()) {
        const std::vector<std::string> cerradas = acciones_.cerrarGrabaciones();
        const std::string texto =
            llenar(traducir("energia", "Queda poca batería (%1 %, %2 min): se cerraron en orden %3 grabación(es); la transmisión "
                                       "sigue. Si el equipo se apaga, al volver se retoma todo."),
                   {std::to_string(e.porcentaje), e.minutos >= 0 ? std::to_string(e.minutos) : std::string("?"),
                    std::to_string(cerradas.size())});
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "energia", 0, texto);
        emit aviso(QString::fromStdString(texto), QStringLiteral("error"));
    } else if (e.conocido && !e.enBateria && acciones_.emergencia()) {
        acciones_.retomarGrabaciones();
        const std::string texto = traducir("energia", "Volvió la luz: las grabaciones que se cerraron por el corte vuelven a grabar.");
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "energia", 0, texto);
        emit aviso(QString::fromStdString(texto), QStringLiteral("ok"));
    }
    ultimo_ = e;
    accion_ = a;
    if (cambio_) emit cambio();
}

nlohmann::json EnergiaAire::estado() const {
    const platform::PoliticaEnergia p = politica();
    return {{"conocido", ultimo_.conocido},
            {"hay_bateria", ultimo_.hayBateria},
            {"en_bateria", ultimo_.enBateria},
            {"porcentaje", ultimo_.porcentaje},
            {"minutos", ultimo_.minutos},
            {"fuente", ultimo_.fuente},
            {"accion", nombre(accion_)},
            {"emergencia", acciones_.emergencia()},
            {"umbral", {{"porcentaje", p.porcentaje}, {"minutos", p.minutos}, {"cerrar_grabaciones", p.cerrarGrabaciones}}}};
}

} // namespace ba::app
