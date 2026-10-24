// ============================================================================
// src/app/term/TerminalView.hpp — Dibuja una TerminalSession en QML
//
// `TerminalView { session: Studio.agent.session }`. Grilla de celdas pintada
// con QPainter (corridas del mismo estilo en un solo drawText), cursor,
// seleccion con el raton (copia al soltar y con Ctrl+Shift+C), pegado
// (Ctrl+Shift+V / Shift+Insert / boton del medio), rueda (historial local, o
// eventos al programa si pidio raton), teclado completo con acentos (metodo
// de entrada) y ajuste de columnas/filas al tamaño del item.
// ============================================================================
#pragma once

#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>

#include "term/TerminalSession.hpp"

namespace ba::app::term {

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset NOTIFY scrollChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
   public:
    explicit TerminalView(QQuickItem* parent = nullptr);

    QObject* session() const { return session_; }
    void setSession(QObject* s);
    int fontSize() const { return font_.pixelSize(); }
    void setFontSize(int px);
    QString fontFamily() const { return font_.family(); }
    void setFontFamily(const QString& f);
    int scrollOffset() const { return scroll_; }
    bool hasSelection() const { return selActive_; }

    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void scrollToBottom();

    void paint(QPainter* p) override;

   signals:
    void sessionChanged();
    void fontChanged();
    void scrollChanged();
    void selectionChanged();

   protected:
    void geometryChange(const QRectF& n, const QRectF& o) override;
    void keyPressEvent(QKeyEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    /// Sin botón apretado: solo si el programa lo pidió (modo 1003; opencode resalta lo que
    /// está debajo del puntero y con eso sabe qué se clickea).
    void hoverMoveEvent(QHoverEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

   private:
    void relayout();
    QPoint cellAt(const QPointF& p) const;
    int mods(Qt::KeyboardModifiers m) const;
    bool selected(int row, int col) const;
    bool forwardMouse(Qt::KeyboardModifiers m) const;

    QPointer<TerminalSession> session_;
    QFont font_;
    qreal cellW_ = 8;
    qreal cellH_ = 16;
    qreal ascent_ = 12;
    int scroll_ = 0;
    bool selecting_ = false;
    QPoint ultimaCelda_{-1, -1}; ///< la última celda que se le contó al programa (hoverMoveEvent)
    bool selActive_ = false;
    QPoint selA_;
    QPoint selB_;
    int selScroll_ = 0;
    qreal pad_ = 10;
};

} // namespace ba::app::term
