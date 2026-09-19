// ============================================================================
// src/app/term/TerminalView.cpp — ver TerminalView.hpp
// ============================================================================

#include "term/TerminalView.hpp"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>

extern "C" {
#include <vterm_keycodes.h>
}

#include <algorithm>

namespace ba::app::term {

TerminalView::TerminalView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, false);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(false);
    setAntialiasing(true);
    setOpaquePainting(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    font_.setFamilies({"JetBrains Mono", "DejaVu Sans Mono", "Menlo", "Consolas", "monospace"});
    font_.setStyleHint(QFont::Monospace);
    font_.setPixelSize(15);
    font_.setKerning(false);
    relayout();
}

void TerminalView::setSession(QObject* s) {
    auto* ts = qobject_cast<TerminalSession*>(s);
    if (ts == session_) return;
    if (session_) disconnect(session_, nullptr, this, nullptr);
    session_ = ts;
    if (session_) {
        connect(session_, &TerminalSession::contentChanged, this, [this]() {
            // Si el usuario esta mirando el historial, se queda donde esta.
            update();
        });
        connect(session_, &TerminalSession::runningChanged, this, [this]() { update(); });
    }
    relayout();
    emit sessionChanged();
    update();
}

void TerminalView::setFontSize(int px) {
    px = std::clamp(px, 8, 40);
    if (px == font_.pixelSize()) return;
    font_.setPixelSize(px);
    relayout();
    emit fontChanged();
}

void TerminalView::setFontFamily(const QString& f) {
    QStringList fam{f};
    fam << "JetBrains Mono" << "DejaVu Sans Mono" << "monospace";
    font_.setFamilies(fam);
    relayout();
    emit fontChanged();
}

void TerminalView::relayout() {
    const QFontMetricsF fm(font_);
    cellW_ = fm.horizontalAdvance(QLatin1Char('M'));
    cellH_ = std::ceil(fm.height() * 1.12);
    ascent_ = fm.ascent() + (cellH_ - fm.height()) / 2;
    if (session_ && width() > 0 && height() > 0) {
        const int cols = static_cast<int>((width() - 2 * pad_) / cellW_);
        const int rows = static_cast<int>((height() - 2 * pad_) / cellH_);
        session_->resize(cols, rows);
    }
    update();
}

void TerminalView::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (n.size() != o.size()) relayout();
}

QPoint TerminalView::cellAt(const QPointF& p) const {
    if (!session_) return {};
    const int col = std::clamp(static_cast<int>((p.x() - pad_) / cellW_), 0, session_->cols() - 1);
    const int row = std::clamp(static_cast<int>((p.y() - pad_) / cellH_), 0, session_->rows() - 1);
    return {col, row};
}

bool TerminalView::selected(int row, int col) const {
    if (!selActive_) return false;
    // Seleccion guardada en coordenadas de "linea absoluta" (fila - scroll).
    const int abs = row - scroll_;
    QPoint a = selA_;
    QPoint b = selB_;
    if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x())) std::swap(a, b);
    if (abs < a.y() || abs > b.y()) return false;
    if (abs == a.y() && col < a.x()) return false;
    if (abs == b.y() && col > b.x()) return false;
    return true;
}

void TerminalView::paint(QPainter* p) {
    const QRgb bgDefault = session_ ? session_->defaultBg() : qRgb(13, 16, 22);
    p->fillRect(boundingRect(), QColor::fromRgb(bgDefault));
    if (!session_) return;
    const int rows = session_->rows();
    const int cols = session_->cols();
    std::vector<Cell> line;
    QFont bold = font_;
    bold.setBold(true);
    QFont italic = font_;
    italic.setItalic(true);
    QFont boldItalic = bold;
    boldItalic.setItalic(true);
    const QRgb selBg = qRgb(0x3a, 0x4a, 0x6b);
    for (int r = 0; r < rows; ++r) {
        session_->line(r, scroll_, line);
        const qreal y = pad_ + r * cellH_;
        // Fondos (corridas del mismo color).
        int c = 0;
        while (c < cols) {
            const QRgb bg = selected(r, c) ? selBg : line[static_cast<std::size_t>(c)].bg;
            int e = c + 1;
            while (e < cols && (selected(r, e) ? selBg : line[static_cast<std::size_t>(e)].bg) == bg)
                ++e;
            if (bg != bgDefault)
                p->fillRect(QRectF(pad_ + c * cellW_, y, (e - c) * cellW_, cellH_), QColor::fromRgb(bg));
            c = e;
        }
        // Texto (corridas del mismo estilo; los anchos van solos).
        c = 0;
        while (c < cols) {
            const Cell& first = line[static_cast<std::size_t>(c)];
            if (first.ch == 0 ||
                (first.ch == U' ' && first.extra.isEmpty() && !first.underline && !first.strike)) {
                ++c;
                continue;
            }
            QString run;
            int e = c;
            while (e < cols) {
                const Cell& x = line[static_cast<std::size_t>(e)];
                if (x.fg != first.fg || x.bold != first.bold || x.italic != first.italic ||
                    x.underline != first.underline || x.strike != first.strike || x.ch == 0)
                    break;
                if (x.width > 1 || !x.extra.isEmpty() || x.ch > 0x2500) {
                    if (e == c) {
                        run += QString::fromUcs4(&x.ch, 1) + x.extra;
                        e += x.width;
                    }
                    break;
                }
                run += QString::fromUcs4(&x.ch, 1);
                ++e;
            }
            p->setFont(first.bold ? (first.italic ? boldItalic : bold) : (first.italic ? italic : font_));
            p->setPen(QColor::fromRgb(first.fg));
            const qreal x0 = pad_ + c * cellW_;
            if (run.size() == 1 || e - c == 1 || run.toUcs4().size() == 1) {
                p->drawText(QPointF(x0, y + ascent_), run);
            } else {
                // Monoespaciado exacto: cada caracter en su celda (evita derivas
                // con fuentes de reemplazo para simbolos).
                const auto u = run.toUcs4();
                for (int i = 0; i < u.size(); ++i)
                    p->drawText(QPointF(x0 + i * cellW_, y + ascent_),
                                QString::fromUcs4(reinterpret_cast<const char32_t*>(&u[i]), 1));
            }
            if (first.underline)
                p->drawLine(QPointF(x0, y + cellH_ - 2), QPointF(pad_ + e * cellW_, y + cellH_ - 2));
            if (first.strike)
                p->drawLine(QPointF(x0, y + cellH_ / 2), QPointF(pad_ + e * cellW_, y + cellH_ / 2));
            c = std::max(e, c + 1);
        }
    }
    // Cursor (solo en la pantalla actual, no mirando historial).
    if (scroll_ == 0 && session_->running() && session_->cursorVisible()) {
        const QRectF cr(pad_ + session_->cursorCol() * cellW_, pad_ + session_->cursorRow() * cellH_, cellW_,
                        cellH_);
        const QColor cc(0xff, 0x8a, 0x3d); // naranja de la marca
        if (hasActiveFocus()) {
            QColor fill = cc;
            fill.setAlpha(200);
            p->fillRect(cr, fill);
            session_->line(session_->cursorRow(), 0, line);
            const Cell& under =
                line[static_cast<std::size_t>(std::clamp(session_->cursorCol(), 0, cols - 1))];
            if (under.ch != 0 && under.ch != U' ') {
                p->setFont(under.bold ? bold : font_);
                p->setPen(QColor::fromRgb(bgDefault));
                p->drawText(QPointF(cr.x(), cr.y() + ascent_), QString::fromUcs4(&under.ch, 1));
            }
        } else {
            p->setPen(QPen(cc, 1.2));
            p->drawRect(cr.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
    // Indicador de historial.
    if (scroll_ > 0) {
        const QString t =
            QString("↑ historial (%1 lineas) — rueda abajo o cualquier tecla para volver").arg(scroll_);
        QFont f = font_;
        f.setPixelSize(std::max(11, font_.pixelSize() - 3));
        p->setFont(f);
        const QFontMetricsF fm(f);
        const QRectF box(width() - fm.horizontalAdvance(t) - 28, 8, fm.horizontalAdvance(t) + 18,
                         fm.height() + 8);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 138, 61, 210));
        p->drawRoundedRect(box, 8, 8);
        p->setPen(Qt::black);
        p->drawText(box, Qt::AlignCenter, t);
    }
    if (!session_->running()) {
        QFont f = font_;
        f.setPixelSize(font_.pixelSize() + 1);
        p->setFont(f);
        p->setPen(QColor(150, 160, 175));
        p->drawText(boundingRect().adjusted(0, 0, 0, -12), Qt::AlignHCenter | Qt::AlignBottom,
                    "— el agente no esta corriendo —");
    }
}

int TerminalView::mods(Qt::KeyboardModifiers m) const {
    int out = VTERM_MOD_NONE;
    if (m & Qt::ShiftModifier) out |= VTERM_MOD_SHIFT;
    if (m & Qt::AltModifier) out |= VTERM_MOD_ALT;
    if (m & Qt::ControlModifier) out |= VTERM_MOD_CTRL;
    return out;
}

void TerminalView::scrollToBottom() {
    if (scroll_ == 0) return;
    scroll_ = 0;
    emit scrollChanged();
    update();
}

void TerminalView::copySelection() {
    if (!session_ || !selActive_) return;
    QPoint a = selA_;
    QPoint b = selB_;
    // Filas absolutas -> filas de la vista con scroll 0 (pueden ser negativas: historial).
    const QString t = session_->text(a.y(), a.x(), b.y(), b.x(), 0);
    QGuiApplication::clipboard()->setText(t);
    if (QGuiApplication::clipboard()->supportsSelection())
        QGuiApplication::clipboard()->setText(t, QClipboard::Selection);
}

void TerminalView::pasteClipboard() {
    if (!session_) return;
    const QString t = QGuiApplication::clipboard()->text();
    if (!t.isEmpty()) {
        scrollToBottom();
        session_->paste(t);
    }
}

void TerminalView::keyPressEvent(QKeyEvent* e) {
    if (!session_) return;
    const auto m = e->modifiers();
    const bool ctrlShift = (m & Qt::ControlModifier) && (m & Qt::ShiftModifier);
    if (ctrlShift && e->key() == Qt::Key_C) {
        copySelection();
        return;
    }
    if ((ctrlShift && e->key() == Qt::Key_V) || ((m & Qt::ShiftModifier) && e->key() == Qt::Key_Insert)) {
        pasteClipboard();
        return;
    }
    if ((m & Qt::ShiftModifier) && (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
        const int step = session_->rows() - 2;
        scroll_ =
            std::clamp(scroll_ + (e->key() == Qt::Key_PageUp ? step : -step), 0, session_->scrollbackSize());
        emit scrollChanged();
        update();
        return;
    }
    if (ctrlShift && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal)) {
        setFontSize(fontSize() + 1);
        return;
    }
    if (ctrlShift && e->key() == Qt::Key_Minus) {
        setFontSize(fontSize() - 1);
        return;
    }
    scrollToBottom();
    if (selActive_) {
        selActive_ = false;
        emit selectionChanged();
    }
    const int md = mods(m);
    int k = VTERM_KEY_NONE;
    switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            k = e->key() == Qt::Key_Enter && (m & Qt::KeypadModifier) ? VTERM_KEY_KP_ENTER : VTERM_KEY_ENTER;
            break;
        case Qt::Key_Tab: k = VTERM_KEY_TAB; break;
        case Qt::Key_Backtab: session_->key(VTERM_KEY_TAB, md | VTERM_MOD_SHIFT); return;
        case Qt::Key_Backspace: k = VTERM_KEY_BACKSPACE; break;
        case Qt::Key_Escape: k = VTERM_KEY_ESCAPE; break;
        case Qt::Key_Up: k = VTERM_KEY_UP; break;
        case Qt::Key_Down: k = VTERM_KEY_DOWN; break;
        case Qt::Key_Left: k = VTERM_KEY_LEFT; break;
        case Qt::Key_Right: k = VTERM_KEY_RIGHT; break;
        case Qt::Key_Insert: k = VTERM_KEY_INS; break;
        case Qt::Key_Delete: k = VTERM_KEY_DEL; break;
        case Qt::Key_Home: k = VTERM_KEY_HOME; break;
        case Qt::Key_End: k = VTERM_KEY_END; break;
        case Qt::Key_PageUp: k = VTERM_KEY_PAGEUP; break;
        case Qt::Key_PageDown: k = VTERM_KEY_PAGEDOWN; break;
        default:
            if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F12)
                k = VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1);
            break;
    }
    if (k != VTERM_KEY_NONE) {
        session_->key(k, md);
        return;
    }
    // Ctrl+letra / Ctrl+simbolo: libvterm arma el caracter de control.
    if ((m & Qt::ControlModifier) && e->key() >= Qt::Key_Space && e->key() <= Qt::Key_AsciiTilde) {
        char32_t c = static_cast<char32_t>(e->key());
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        session_->unichar(c, md & ~VTERM_MOD_SHIFT);
        return;
    }
    const QString text = e->text();
    if (!text.isEmpty()) {
        for (const char32_t c : text.toUcs4()) {
            if (c < 0x20 && c != 0x1b) continue;
            session_->unichar(c, md & ~VTERM_MOD_SHIFT);
        }
        return;
    }
    QQuickPaintedItem::keyPressEvent(e);
}

void TerminalView::inputMethodEvent(QInputMethodEvent* e) {
    // Teclas muertas y metodos de entrada (á, ñ, ü...): llega el texto final.
    if (session_ && !e->commitString().isEmpty()) {
        scrollToBottom();
        for (const char32_t c : e->commitString().toUcs4())
            session_->unichar(c, VTERM_MOD_NONE);
    }
    e->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery q) const {
    if (q == Qt::ImCursorRectangle && session_)
        return QRectF(pad_ + session_->cursorCol() * cellW_, pad_ + session_->cursorRow() * cellH_, cellW_,
                      cellH_);
    if (q == Qt::ImEnabled) return true;
    if (q == Qt::ImHints) return static_cast<int>(Qt::ImhNoPredictiveText);
    return QQuickPaintedItem::inputMethodQuery(q);
}

bool TerminalView::forwardMouse(Qt::KeyboardModifiers m) const {
    // Con Shift se selecciona aunque el programa pida el raton (como xterm).
    return session_ && session_->mouseMode() && !(m & Qt::ShiftModifier) && scroll_ == 0;
}

void TerminalView::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus(Qt::MouseFocusReason);
    if (!session_) return;
    const QPoint c = cellAt(e->position());
    if (e->button() == Qt::MiddleButton) {
        const QString t = QGuiApplication::clipboard()->text(QGuiApplication::clipboard()->supportsSelection()
                                                                 ? QClipboard::Selection
                                                                 : QClipboard::Clipboard);
        if (!t.isEmpty()) session_->paste(t);
        return;
    }
    if (forwardMouse(e->modifiers())) {
        session_->mouseButton(c.y(), c.x(), e->button() == Qt::RightButton ? 3 : 1, true,
                              mods(e->modifiers()));
        return;
    }
    if (e->button() == Qt::LeftButton) {
        selecting_ = true;
        selA_ = selB_ = QPoint(c.x(), c.y() - scroll_);
        if (selActive_) {
            selActive_ = false;
            emit selectionChanged();
        }
        update();
    }
}

void TerminalView::mouseMoveEvent(QMouseEvent* e) {
    if (!session_) return;
    const QPoint c = cellAt(e->position());
    if (forwardMouse(e->modifiers()) && !selecting_) {
        session_->mouseMove(c.y(), c.x(), mods(e->modifiers()));
        return;
    }
    if (selecting_) {
        selB_ = QPoint(c.x(), c.y() - scroll_);
        if (!selActive_ && selB_ != selA_) {
            selActive_ = true;
            emit selectionChanged();
        }
        update();
    }
}

void TerminalView::mouseReleaseEvent(QMouseEvent* e) {
    if (!session_) return;
    if (forwardMouse(e->modifiers()) && !selecting_) {
        const QPoint c = cellAt(e->position());
        session_->mouseButton(c.y(), c.x(), e->button() == Qt::RightButton ? 3 : 1, false,
                              mods(e->modifiers()));
        return;
    }
    if (selecting_) {
        selecting_ = false;
        if (selActive_) copySelection();
    }
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!session_ || forwardMouse(e->modifiers())) return;
    // Doble click: selecciona la palabra.
    const QPoint c = cellAt(e->position());
    std::vector<Cell> l;
    session_->line(c.y(), scroll_, l);
    const auto isWord = [&](int x) {
        if (x < 0 || x >= static_cast<int>(l.size())) return false;
        const char32_t ch = l[static_cast<std::size_t>(x)].ch;
        return ch != U' ' && ch != 0 &&
               !QString::fromUcs4(&ch, 1).contains(QRegularExpression("[\\s()\\[\\]{}<>\"'`,;|]"));
    };
    int a = c.x();
    int b = c.x();
    while (isWord(a - 1))
        --a;
    while (isWord(b + 1))
        ++b;
    selA_ = QPoint(a, c.y() - scroll_);
    selB_ = QPoint(b, c.y() - scroll_);
    selActive_ = true;
    selecting_ = false;
    emit selectionChanged();
    copySelection();
    update();
}

void TerminalView::wheelEvent(QWheelEvent* e) {
    if (!session_) return;
    const int steps = e->angleDelta().y() / 40;
    if (steps == 0) return;
    if (session_->mouseMode() && scroll_ == 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        const QPoint c = cellAt(e->position());
        for (int i = 0; i < std::abs(steps); ++i)
            session_->mouseButton(c.y(), c.x(), steps > 0 ? 4 : 5, true, mods(e->modifiers()));
        return;
    }
    if (session_->altScreen() && scroll_ == 0) {
        // Pantalla alternativa sin raton: flechas (como "alternate scroll").
        for (int i = 0; i < std::abs(steps); ++i)
            session_->key(steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return;
    }
    scroll_ = std::clamp(scroll_ + steps, 0, session_->scrollbackSize());
    emit scrollChanged();
    update();
}

void TerminalView::focusInEvent(QFocusEvent* e) {
    QQuickPaintedItem::focusInEvent(e);
    if (session_) session_->focus(true);
    update();
}

void TerminalView::focusOutEvent(QFocusEvent* e) {
    QQuickPaintedItem::focusOutEvent(e);
    if (session_) session_->focus(false);
    update();
}

} // namespace ba::app::term
