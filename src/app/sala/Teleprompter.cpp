// src/app/sala/Teleprompter.cpp — el teleprompter de la sala (ver el .hpp)

#include "sala/Teleprompter.hpp"

#include "i18n/I18n.hpp"

#include "ui/Discursos.hpp"

#include <algorithm>

namespace ba::app::sala {

Teleprompter::Teleprompter(ConferenceClient& sala) : sala_(sala) {}

void Teleprompter::mandar(const nlohmann::json& orden, Listo listo) {
    pedir(sala_, "POST", "/api/v1/prompter", orden, std::move(listo));
}

void Teleprompter::ver(Listo listo) { pedir(sala_, "GET", "/api/v1/prompter", {}, std::move(listo)); }

std::string Teleprompter::cargarDiscurso(const Discursos& docs, const std::string& rel, bool arrancar, Listo listo) {
    if (rel.empty()) return ba::i18n::traducir("sala", "No dice qué discurso.");
    bool ok = false;
    const QString texto = docs.leer(QString::fromStdString(rel), &ok);
    if (!ok) return ba::i18n::llenar(ba::i18n::traducir("sala", "No pude abrir «%1»."), {rel});
    nlohmann::json orden{{"titulo", rel}, {"texto", texto.toStdString()}};
    if (arrancar) orden["corriendo"] = true;
    mandar(orden, std::move(listo));
    return {};
}

void Teleprompter::cargarTexto(const std::string& titulo, const std::string& texto, Listo listo) {
    mandar({{"titulo", titulo}, {"texto", texto}}, std::move(listo));
}

void Teleprompter::arrancar(bool si, Listo listo) { mandar({{"corriendo", si}}, std::move(listo)); }
void Teleprompter::setPpm(int ppm, Listo listo) { mandar({{"ppm", std::clamp(ppm, 40, 400)}}, std::move(listo)); }
void Teleprompter::setTamano(int t, Listo listo) { mandar({{"tamano", std::clamp(t, 1, 10)}}, std::move(listo)); }
void Teleprompter::setEspejo(bool si, Listo listo) { mandar({{"espejo", si}}, std::move(listo)); }
void Teleprompter::irA(double posicion, Listo listo) { mandar({{"posicion", std::max(0.0, posicion)}}, std::move(listo)); }
void Teleprompter::irAParrafo(int n, Listo listo) { mandar({{"parrafo", std::max(0, n)}}, std::move(listo)); }
void Teleprompter::avisar(const std::string& texto, Listo listo) { mandar({{"aviso", texto}}, std::move(listo)); }
void Teleprompter::panico(Listo listo) { mandar({{"panico", true}}, std::move(listo)); }

} // namespace ba::app::sala
