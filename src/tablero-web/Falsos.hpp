// SPDX-License-Identifier: GPL-2.0-or-later
//
// Los tipos de la app que en el navegador no tienen con qué andar (la terminal del
// asistente, el monitor del bus de eventos, los osciloscopios de video): las mismas
// propiedades que ven los QML (como en tests/test_qml_carga.cpp), sin nada adentro. Las
// tarjetas del dashboard no los usan; están para que un control compartido que los nombra
// compile igual en la web.
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>

class TerminalWeb : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* session MEMBER session_ NOTIFY cambio)
    Q_PROPERTY(int fontSize MEMBER fontSize_ NOTIFY cambio)
    Q_PROPERTY(QString fontFamily MEMBER fontFamily_ NOTIFY cambio)
    Q_PROPERTY(int scrollOffset READ cero NOTIFY cambio)
    Q_PROPERTY(bool hasSelection READ no NOTIFY cambio)
   public:
    Q_INVOKABLE void copySelection() {}
    Q_INVOKABLE void pasteClipboard() {}
    Q_INVOKABLE void scrollToBottom() {}
    int cero() const { return 0; }
    bool no() const { return false; }
   signals:
    void cambio();

   private:
    QObject* session_ = nullptr;
    int fontSize_ = 12;
    QString fontFamily_;
};

class MonitorBusWeb : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QObject* bus MEMBER bus_ NOTIFY cambio)
    Q_PROPERTY(QString filtro MEMBER filtro_ NOTIFY cambio)
    Q_PROPERTY(bool conMediciones MEMBER conMediciones_ NOTIFY cambio)
    Q_PROPERTY(bool activo MEMBER activo_ NOTIFY cambio)
    Q_PROPERTY(QVariantList recientes MEMBER recientes_ NOTIFY cambio)
   public:
    int rowCount(const QModelIndex& = {}) const override { return 0; }
    QVariant data(const QModelIndex&, int) const override { return {}; }
   signals:
    void cambio();

   private:
    QObject* bus_ = nullptr;
    QString filtro_;
    bool conMediciones_ = false, activo_ = false;
    QVariantList recientes_;
};

class ScopeWeb : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* imagen MEMBER imagen_ NOTIFY cambio)
    Q_PROPERTY(QString fuente MEMBER fuente_ NOTIFY cambio)
    Q_PROPERTY(QString tipo MEMBER tipo_ NOTIFY cambio)
    Q_PROPERTY(int ancho MEMBER ancho_ NOTIFY cambio)
    Q_PROPERTY(int alto MEMBER alto_ NOTIFY cambio)
    Q_PROPERTY(bool activo MEMBER activo_ NOTIFY cambio)
    Q_PROPERTY(QVariantMap dato MEMBER dato_ NOTIFY cambio)
   signals:
    void cambio();

   private:
    QObject* imagen_ = nullptr;
    QString fuente_, tipo_;
    int ancho_ = 0, alto_ = 0;
    bool activo_ = false;
    QVariantMap dato_;
};
