// ============================================================================
// src/app/graficos/Vinculos.hpp — que el rótulo se escriba solo
//
// El rotulador sirve para escribir a mano. Esto es lo otro: decir UNA VEZ de
// dónde sale cada campo, y que se mantenga solo.
//
//     «en el zócalo, el nombre sale del orador que está al aire y el cargo
//      también; cuando cambie el orador, actualizalo y disparalo»
//
// Un vínculo es exactamente eso, escrito:
//
//     { "fuente": "Zócalo", "grafico": "zocalo",
//       "valores": { "nombre": "{orador.nombre}", "cargo": "{orador.cargo}" },
//       "dispara": true }
//
// POR QUÉ NO HAY UNA LISTA DE DISPARADORES. Se podría decir «cuando cambie el
// orador», «cuando cambie la escena», y habría que mantener esa lista para
// siempre. Pero el disparador verdadero es siempre el mismo: que lo que el
// vínculo RESUELVE haya cambiado. Si el nombre del orador es otro, el rótulo
// está mal; si es el mismo, no hay nada que hacer aunque haya cambiado media
// docena de cosas alrededor. Se revisa una vez por segundo y se dispara solo
// cuando el resultado cambió.
//
// LA FUENTE SE NOMBRA, NO SE NUMERA. Un identificador interno de una fuente de
// escena cambia cada vez que se abre el programa; el nombre no.
// Un vínculo guardado tiene que seguir valiendo mañana.
//
// Los ORÍGENES (lo que se puede poner entre llaves) salen de un contexto que
// arma quien nos crea: la sala sabe quién está al aire, el guion sabe quién
// sigue. Acá solo se resuelven, y `origenes()` los devuelve CON SU VALOR
// ACTUAL para que la ayuda nunca mienta sobre lo que hay.
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace ba::app::graficos {

class Emisor;

class Vinculos : public QObject {
    Q_OBJECT

   public:
    /// :param archivo: dónde se guardan (Config/vinculos.json).
    Vinculos(std::filesystem::path archivo, Emisor& emisor, QObject* padre = nullptr);
    ~Vinculos() override;

    /// Quién arma el contexto del que salen los orígenes. Sin esto, un vínculo
    /// no resuelve nada y se queda quieto (que es lo correcto: es preferible
    /// un rótulo viejo a uno inventado).
    void setContexto(std::function<QJsonObject()> fn);

    /// Los vínculos guardados, con lo que resuelve cada uno ahora mismo.
    QJsonArray lista() const;

    /// Agrega o reemplaza uno. Devuelve el error, o vacío si salió bien; el id
    /// queda en `idOut`.
    QString poner(const QJsonObject& v, QString* idOut = nullptr);
    bool quitar(const QString& id);
    /// Lo prende o lo apaga sin borrarlo: en vivo, apagar es más seguro que
    /// borrar y volver a escribir.
    bool activar(const QString& id, bool activo);

    /// Resuelve un vínculo contra el contexto de ahora, sin disparar nada.
    /// Vacío en `error` = se pudo resolver entero.
    QJsonObject resolver(const QJsonObject& v, QString* error = nullptr) const;

    /// Lo que se puede poner entre llaves, con su valor de ahora.
    QJsonObject origenes() const;

    /// Revisa todos y dispara los que hayan cambiado. Se llama sola.
    void revisar();

    /// Dispara uno a mano, cambió o no.
    QString disparar(const QString& id);

   signals:
    void cambio();
    /// Un vínculo salió al aire solo, o no pudo.
    void actuo(const QString& id, const QString& texto, bool error);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
