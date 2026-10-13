// src/app/asamblea/Votaciones.cpp — las votaciones de la asamblea (ver el .hpp)

#include "asamblea/Votaciones.hpp"

#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/Quorum.hpp"
#include "asamblea/SesionRemota.hpp"
#include "control/BusEventos.hpp"

#include <QPointer>

namespace ba::app::asamblea {

Votaciones::Votaciones(SesionRemota& sesion, ConferenciaDcn& dcn, Quorum& quorum, control::BusEventos& bus,
                       QObject* parent)
    : QObject(parent), sesion_(sesion), dcn_(dcn), quorum_(quorum), bus_(bus) {
    // El resultado oficial de la central: con los votos de los celulares si los hubo.
    dcn_.setAQuorum([this](const json& r) {
        const bool votaronCelulares = !sesion_.votacion().value("votos", json::array()).empty();
        return cargarOficial(votaronCelulares ? combinada(r) : r);
    });
    // La votación de la central también en los celulares (abrir, pausar, cerrar).
    connect(&dcn_, &ConferenciaDcn::cambio, this, &Votaciones::seguirCentral);
}

void Votaciones::accion(const std::string& acc, const json& args, const std::string& quien, Listo listo) {
    const auto avisar = [listo](const std::string& e) {
        if (listo) listo(e);
    };
    if (dcn_.conectada() && acc != "a_quorum") {
        // Con la central, la votación la lleva la central (los celulares la siguen).
        if (acc == "abrir") {
            QPointer<Votaciones> yo(this);
            dcn_.control("votacion",
                         {{"accion", "preparar"},
                          {"tema", args.value("tema", std::string())},
                          {"menu", args.value("menu", std::string("si_no_abstencion"))},
                          {"quien", quien}},
                         [yo, quien, avisar](bool ok, const json&, const std::string& e) {
                             if (!yo) return;
                             if (!ok) return avisar("Central: " + e);
                             yo->dcn_.control("votacion", {{"accion", "abrir"}, {"quien", quien}},
                                              [avisar](bool ok2, const json&, const std::string& e2) {
                                                  avisar(ok2 ? std::string() : "Central: " + e2);
                                              });
                         });
        } else {
            dcn_.control("votacion", {{"accion", acc}, {"quien", quien}},
                         [avisar](bool ok, const json&, const std::string& e) { avisar(ok ? std::string() : "Central: " + e); });
        }
        return;
    }
    if (acc == "abrir") {
        avisar(abrirEnCelulares(args.value("tema", std::string()), args.value("menu", std::string("si_no_abstencion")),
                                args.value("segundos", sesion_.votoSegundos())));
    } else if (acc == "pausar" || acc == "reanudar") {
        pausarEnCelulares(acc == "pausar");
        avisar({});
    } else if (acc == "cerrar") {
        cerrarEnCelulares();
        avisar({});
    } else if (acc == "a_quorum") {
        const json r = dcn_.conectada() ? dcn_.pasarAQuorum() : celularesAQuorum();
        avisar(r.value("error", std::string()));
    } else {
        avisar("acción de votación desconocida: " + acc);
    }
}

std::string Votaciones::abrirEnCelulares(const std::string& temaPedido, const std::string& menu, int segundos) {
    std::string tema = temaPedido;
    tema.erase(0, tema.find_first_not_of(" \t"));
    tema.erase(tema.find_last_not_of(" \t") + 1);
    if (tema.empty()) return "falta el tema";
    // Una tras otra: la que sigue a la última abierta en los celulares.
    const json v = sesion_.votacion();
    const int n = v.value("origen", std::string()) == "app" ? v.value("numero", 0) + 1 : 1;
    return sesion_.abrirVotacion(n, tema, SesionRemota::opcionesDeMenu(menu), segundos, "app");
}

void Votaciones::cerrarEnCelulares() {
    sesion_.cerrarVotacion();
    sesion_.mostrarResultado(sesion_.resultadoCombinado(json::array()).value("totales", json::object()));
}

void Votaciones::pausarEnCelulares(bool pausa) { sesion_.pausarVotacion(pausa); }

Votaciones::json Votaciones::celularesAQuorum(const std::string& asunto) {
    const json v = sesion_.votacion();
    if (v.value("estado", std::string()) != "cerrada") return {{"error", "cerrala primero"}};
    const json c = sesion_.resultadoCombinado(json::array());
    const json t = c.value("totales", json::object());
    const json r = cargarOficial({{"asunto", asunto.empty() ? v.value("tema", std::string()) : asunto},
                                  {"afirmativos", t.value("afirmativo", 0)},
                                  {"negativos", t.value("negativo", 0)},
                                  {"abstenciones", t.value("abstencion", 0)},
                                  {"presentes", sesion_.presentes()},
                                  {"nominal", c.value("nominal", json::array())},
                                  {"origen", "celulares"}});
    if (r.contains("error")) return r;
    return {{"resultado", r}};
}

Votaciones::json Votaciones::cargarOficial(const json& resultado) {
    const json r = quorum_.cargarOficial(resultado);
    if (!r.contains("error")) bus_.publicar("sesion/votacion", r, true);
    return r;
}

Votaciones::json Votaciones::combinada(const json& rCentral) const {
    json r = rCentral;
    const json c = sesion_.resultadoCombinado(rCentral.value("nominal", json::array()));
    const json t = c.value("totales", json::object());
    r["nominal"] = c.value("nominal", json::array());
    r["afirmativos"] = t.value("afirmativo", 0);
    r["negativos"] = t.value("negativo", 0);
    r["abstenciones"] = t.value("abstencion", 0);
    r["origen"] = rCentral.value("origen", std::string("sala")) + " + celulares";
    if (!c.value("observaciones", json::array()).empty()) r["observaciones"] = c["observaciones"];
    return r;
}

void Votaciones::seguirCentral() {
    const json v = dcn_.estado().value("votacion", json::object());
    const std::string est = v.value("estado", std::string());
    const json actual = sesion_.votacion();
    const std::string remoto = actual.value("estado", std::string());
    const bool deLaCentral = actual.value("origen", std::string()) == "central";
    const int numero = v.value("numero", 0);
    if (est == "abierta" && (remoto == "sin" || remoto == "cerrada" || !deLaCentral || actual.value("numero", -1) != numero)) {
        sesion_.abrirVotacion(numero, v.value("tema", std::string()), SesionRemota::opcionesDeMenu(v.value("menu", std::string())),
                              sesion_.votoSegundos(), "central");
    } else if (deLaCentral && est == "pausada" && remoto == "abierta") {
        sesion_.pausarVotacion(true);
    } else if (deLaCentral && est == "abierta" && remoto == "pausada") {
        sesion_.pausarVotacion(false);
    } else if (deLaCentral && est == "cerrada" && (remoto == "abierta" || remoto == "pausada")) {
        sesion_.cerrarVotacion();
        const json c = sesion_.resultadoCombinado(v.value("nominal", json::array()));
        sesion_.mostrarResultado(c.value("totales", json::object()));
    }
}

QString Votaciones::abrirEnCelularesUi(const QString& tema, const QString& menu, int segundos) {
    return QString::fromStdString(abrirEnCelulares(tema.toStdString(), menu.toStdString(), segundos));
}

} // namespace ba::app::asamblea
