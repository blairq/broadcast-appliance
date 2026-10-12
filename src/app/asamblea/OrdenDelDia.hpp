// SPDX-License-Identifier: GPL-2.0-or-later
//
// OrdenDelDia: los puntos de la sesión y el uso de la palabra.
//
// Cada punto {n, expediente, titulo, estado, inicio_ms, fin_ms, resultado}
// con estado pendiente | en_tratamiento | votado | comision | retirado. Se
// carga de texto (una línea por punto: «3. Expte. 123/24 – Título» o
// «3;123/24;Título») o de CSV. Poner uno «en tratamiento» cierra el anterior,
// lo publica en el bus (sesion/punto), lo deja para el zócalo ({punto}) y
// queda en los capítulos de la grabación y en el acta.
//
// El uso de la palabra se suma solo: cada vez que cambia el orador presencial
// al aire (bus oradores/al_aire), se cierra el tramo del anterior; los
// remotos se suman con el tiempo que lleva la sala (usedMs). Va al acta por
// orador y por punto. Config/orden-del-dia.json (la sesión en curso).
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::control {
class BusEventos;
}

namespace ba::app::asamblea {

class OrdenDelDia : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList puntos READ puntosUi NOTIFY cambio)
    Q_PROPERTY(QVariantMap actual READ actualUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    OrdenDelDia(const std::filesystem::path& datos, control::BusEventos& bus, QObject* parent = nullptr);

    /// Carga los puntos de `texto` (reemplaza los que había). Devuelve cuántos o el error.
    std::string cargar(const std::string& texto, int* cuantos);
    json puntos() const { return puntos_; }
    /// El punto en tratamiento ({} si ninguno).
    json actual() const;
    /// Cambia el estado (y el resultado). "" o el error.
    std::string punto(int n, const std::string& estado, const std::string& resultado);
    /// El próximo pendiente pasa a «en tratamiento» (el actual a «votado» si no tenía otro estado).
    std::string siguiente();
    /// Lo que se habló: [{nombre, banca?, segundos, por_punto: {n: segundos}}] (presenciales + `remotos`).
    json tiemposPalabra(const json& remotos) const;
    /// Capítulos para la grabación desde `recInicioMs`: [{seg, marca, titulo}].
    json capitulos(std::int64_t recInicioMs) const;
    /// Los mismos capítulos como texto para pegar en YouTube («mm:ss Punto n: título» por
    /// línea). Sin grabación (`recInicioMs` <= 0) no hay capítulos: "".
    std::string capitulosTexto(std::int64_t recInicioMs) const;
    /// El anexo del acta (markdown): orden del día y uso de la palabra.
    std::string anexoActa(const json& remotos) const;
    /// Empieza una sesión nueva (borra puntos y tiempos).
    void limpiar();

    QVariantList puntosUi() const;
    QVariantMap actualUi() const;
    Q_INVOKABLE QString cargarUi(const QString& texto);
    Q_INVOKABLE QString puntoUi(int n, const QString& estado, const QString& resultado = {});
    Q_INVOKABLE QString siguienteUi() { return QString::fromStdString(siguiente()); }
    Q_INVOKABLE void limpiarUi() { limpiar(); }

   signals:
    void cambio();

   private:
    void guardar() const;
    void publicar();
    void cerrarTramo(std::int64_t ahora);
    std::filesystem::path archivo_;
    control::BusEventos& bus_;
    json puntos_ = json::array();
    json tramos_ = json::array();  ///< [{nombre, banca, desde_ms, hasta_ms, punto}]
    json enCurso_;                 ///< el tramo abierto (orador presencial al aire)
    int sub_ = 0;
};

} // namespace ba::app::asamblea
