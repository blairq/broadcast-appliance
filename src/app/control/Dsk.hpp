// SPDX-License-Identifier: GPL-2.0-or-later
//
// Dsk: las cuatro capas globales encima del programa (BroadcastEngine::dsk*):
// un logo o mosca, el recuadro del intérprete de LSA, un zócalo fijo, un
// reloj. Cambiar de escena no las toca. Acá vive lo que el motor no sabe:
// el nombre de cada capa, qué tiene, si es FIJA (la de LSA: bajarla pide
// confirmación y no se baja sola), el fundido y si estaba arriba (se restaura
// al volver a abrir el programa). Config/dsk.json.
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app {
class BroadcastEngine;
}

namespace ba::app::control {

class Dsk : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList capas READ capasUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    Dsk(const std::filesystem::path& datos, BroadcastEngine& engine, QObject* parent = nullptr);

    /// [{capa, nombre, contenido[], recuadro, fijo, transicion_ms, visible}].
    json capas() const;
    /// Define la capa (1..4). `def`: {nombre?, contenido?[] (escenas o fuentes), recuadro?, fijo?, transicion_ms?}.
    std::string definir(int capa, const json& def);
    /// Sube o baja. Bajar una fija necesita `confirmada` (la confirmación de una persona).
    std::string mostrar(int capa, bool visible, bool confirmada);
    /// La plantilla «Intérprete LSA»: la `entrada` en un recuadro abajo a la derecha, fija.
    std::string plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano);
    /// Al arrancar: vuelve a armar las capas y sube las que estaban arriba (sin fundido).
    void restaurar();
    /// Avisa al bus (lo engancha la app).
    void setAlCambiar(std::function<void(const json&)> fn) { alCambiar_ = std::move(fn); }

    QVariantList capasUi() const;
    Q_INVOKABLE QString definirUi(int capa, const QVariantMap& def);
    Q_INVOKABLE QString mostrarUi(int capa, bool visible, bool confirmada);
    Q_INVOKABLE QString plantillaLsaUi(int capa, const QString& entrada);
    Q_INVOKABLE bool fija(int capa) const;

   signals:
    void cambio();

   private:
    void cargar();
    void guardar() const;
    json* buscar(int capa);
    void avisar();
    std::filesystem::path archivo_;
    BroadcastEngine& engine_;
    json capas_ = json::array();
    std::function<void(const json&)> alCambiar_;
};

} // namespace ba::app::control
