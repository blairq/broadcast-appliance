// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pagina: lo del navegador que la QML del marco web necesita (TableroWeb.qml), sin pasar por la
// app: la pantalla completa (Fullscreen API; en iPhone/iPad, donde no anda, se avisa que se
// agregue a la pantalla de inicio) y lo que se recuerda en ESTE navegador (localStorage: p. ej.
// si los monitores están plegados). Lo que se recuerda por aparato (la disposición, la salida)
// va al registro del aparato en la app, no acá.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

class Pagina : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pantallaCompleta READ pantallaCompleta NOTIFY pantallaCompletaChanged)
   public:
    using QObject::QObject;
    static Pagina& la();
    bool pantallaCompleta() const { return completa_; }
    /// Entrar o salir de la pantalla completa. "" si se pudo; si no, por qué (para avisar).
    Q_INVOKABLE QString alternarPantallaCompleta();
    Q_INVOKABLE QString preferencia(const QString& clave) const;
    Q_INVOKABLE void guardarPreferencia(const QString& clave, const QString& valor);
    /// Lo avisa la página (fullscreenchange).
    void setPantallaCompleta(bool si) {
        if (si == completa_) return;
        completa_ = si;
        emit pantallaCompletaChanged();
    }

   signals:
    void pantallaCompletaChanged();

   private:
    bool completa_ = false;
};
