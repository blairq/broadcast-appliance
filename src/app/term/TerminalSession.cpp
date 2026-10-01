// ============================================================================
// src/app/term/TerminalSession.cpp — ver TerminalSession.hpp
// ============================================================================

#include "term/TerminalSession.hpp"

extern "C" {
#include <vterm.h>
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace ba::app::term {

namespace {

// Paleta de 16 colores (base "One Dark", afinada para fondo casi negro).
constexpr QRgb kPalette[16] = {
    0xff1b1f27, 0xffe06c75, 0xff98c379, 0xffe5c07b, 0xff61afef, 0xffc678dd, 0xff56b6c2, 0xffc8ccd4,
    0xff5c6370, 0xffff7a85, 0xffb5e890, 0xffffd68a, 0xff7cc2ff, 0xffdd9cf0, 0xff7fd4df, 0xffffffff,
};
constexpr QRgb kDefaultFg = 0xffdfe3ea;
constexpr QRgb kDefaultBg = 0xff0d1016;

VTermColor rgb(QRgb c) {
    VTermColor v;
    vterm_color_rgb(&v, static_cast<uint8_t>(qRed(c)), static_cast<uint8_t>(qGreen(c)),
                    static_cast<uint8_t>(qBlue(c)));
    return v;
}

void outputCb(const char* s, size_t len, void* user) {
    auto* pty = static_cast<Pty*>(user);
    if (pty != nullptr) pty->write(QByteArray(s, static_cast<qsizetype>(len)));
}

} // namespace

struct SessionCallbacks {
    static int damage(VTermRect, void* user) {
        static_cast<TerminalSession*>(user)->scheduleRepaint();
        return 1;
    }
    static int moverect(VTermRect, VTermRect, void* user) {
        static_cast<TerminalSession*>(user)->scheduleRepaint();
        return 1;
    }
    static int movecursor(VTermPos pos, VTermPos, int visible, void* user) {
        auto* s = static_cast<TerminalSession*>(user);
        s->cursorRow_ = pos.row;
        s->cursorCol_ = pos.col;
        s->cursorVisible_ = visible != 0;
        s->scheduleRepaint();
        return 1;
    }
    static int settermprop(VTermProp prop, VTermValue* val, void* user) {
        auto* s = static_cast<TerminalSession*>(user);
        switch (prop) {
            case VTERM_PROP_CURSORVISIBLE: s->cursorVisible_ = val->boolean != 0; break;
            case VTERM_PROP_ALTSCREEN: s->altScreen_ = val->boolean != 0; break;
            case VTERM_PROP_MOUSE: s->mouseMode_ = val->number; break;
            case VTERM_PROP_TITLE: {
                if (val->string.initial) s->titleBuf_.clear();
                s->titleBuf_ += QString::fromUtf8(val->string.str, static_cast<qsizetype>(val->string.len));
                if (val->string.final && s->titleBuf_ != s->title_) {
                    s->title_ = s->titleBuf_;
                    emit s->titleChanged();
                }
                break;
            }
            default: break;
        }
        s->scheduleRepaint();
        return 1;
    }
    static int bell(void* user) {
        emit static_cast<TerminalSession*>(user)->bell();
        return 1;
    }
    static int pushline(int cols, const VTermScreenCell* cells, void* user) {
        auto* s = static_cast<TerminalSession*>(user);
        std::vector<Cell> line(static_cast<std::size_t>(cols));
        for (int i = 0; i < cols; ++i)
            s->convert(&cells[i], line[static_cast<std::size_t>(i)]);
        // Sin espacios de relleno a la derecha (ahorra memoria).
        while (!line.empty() && line.back().ch == U' ' && line.back().defaultBg &&
               line.back().extra.isEmpty())
            line.pop_back();
        s->scrollback_.push_back(std::move(line));
        if (s->scrollback_.size() > s->maxScrollback_) s->scrollback_.pop_front();
        return 1;
    }
    static int popline(int cols, VTermScreenCell* cells, void* user) {
        auto* s = static_cast<TerminalSession*>(user);
        if (s->scrollback_.empty()) return 0;
        const std::vector<Cell> line = std::move(s->scrollback_.back());
        s->scrollback_.pop_back();
        for (int i = 0; i < cols; ++i) {
            VTermScreenCell& c = cells[i];
            std::memset(&c, 0, sizeof(c));
            c.width = 1;
            vterm_state_get_default_colors(s->state_, &c.fg, &c.bg);
            if (i < static_cast<int>(line.size())) {
                const Cell& src = line[static_cast<std::size_t>(i)];
                c.chars[0] = src.ch == 0 ? static_cast<uint32_t>(-1) : src.ch;
                c.width = static_cast<char>(src.width);
                c.attrs.bold = src.bold;
                c.attrs.italic = src.italic;
                c.fg = rgb(src.fg);
                if (!src.defaultBg) c.bg = rgb(src.bg);
            } else {
                c.chars[0] = ' ';
            }
        }
        return 1;
    }
    static int sbclear(void* user) {
        static_cast<TerminalSession*>(user)->scrollback_.clear();
        return 1;
    }
};

namespace {
const VTermScreenCallbacks kScreenCallbacks = {
    SessionCallbacks::damage,      SessionCallbacks::moverect, SessionCallbacks::movecursor,
    SessionCallbacks::settermprop, SessionCallbacks::bell,     nullptr,
    SessionCallbacks::pushline,    SessionCallbacks::popline,  SessionCallbacks::sbclear,
};
} // namespace

TerminalSession::TerminalSession(QObject* parent) : QObject(parent), pty_(std::make_unique<Pty>()) {
    vt_ = vterm_new(rows_, cols_);
    vterm_set_utf8(vt_, 1);
    screen_ = vterm_obtain_screen(vt_);
    state_ = vterm_obtain_state(vt_);
    vterm_screen_set_callbacks(screen_, &kScreenCallbacks, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, true);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCROLL);
    const VTermColor fg = rgb(kDefaultFg);
    const VTermColor bg = rgb(kDefaultBg);
    vterm_state_set_default_colors(state_, &fg, &bg);
    for (int i = 0; i < 16; ++i) {
        const VTermColor c = rgb(kPalette[i]);
        vterm_state_set_palette_color(state_, i, &c);
    }
    vterm_state_set_bold_highbright(state_, 1);
    vterm_screen_reset(screen_, 1);
    defaultFg_ = kDefaultFg;
    defaultBg_ = kDefaultBg;
    vterm_output_set_callback(vt_, outputCb, pty_.get());

    repaint_.setSingleShot(true);
    repaint_.setInterval(12);
    connect(&repaint_, &QTimer::timeout, this, &TerminalSession::contentChanged);
    connect(pty_.get(), &Pty::output, this, [this](const QByteArray& data) {
        // Con BA_TERM_DUMP=<archivo> se guarda TAL CUAL lo que escribe el
        // programa. Es la unica forma de reproducir despues, byte por byte,
        // lo que hizo que la pantalla quedara mal: una terminal se rompe por
        // una secuencia puntual entre millones, y mirando el resultado no se
        // sabe cual fue.
        if (const char* vol = std::getenv("BA_TERM_DUMP"); vol != nullptr && vol[0] != 0) {
            if (std::FILE* f = std::fopen(vol, "ab"); f != nullptr) {
                std::fwrite(data.constData(), 1, static_cast<std::size_t>(data.size()), f);
                std::fclose(f);
            }
        }
        {
            const std::lock_guard<std::mutex> lk(vtLock_);
            vterm_input_write(vt_, data.constData(), static_cast<size_t>(data.size()));
            vterm_screen_flush_damage(screen_);
        }
        scheduleRepaint();
    });
    connect(pty_.get(), &Pty::finished, this, [this](int code) {
        emit runningChanged();
        emit finished(code);
        scheduleRepaint();
    });
}

TerminalSession::~TerminalSession() {
    pty_->blockSignals(true);
    pty_->terminate();
    vterm_free(vt_);
}

bool TerminalSession::start(const Pty::Options& opt, QString* error) {
    stop();
    // Pantalla limpia para la sesion nueva (el historial queda).
    {
        const std::lock_guard<std::mutex> lk(vtLock_);
        vterm_screen_reset(screen_, 1);
    }
    Pty::Options o = opt;
    o.cols = cols_;
    o.rows = rows_;
    const bool ok = pty_->start(o, error);
    emit runningChanged();
    scheduleRepaint();
    return ok;
}

void TerminalSession::stop() {
    if (pty_->running()) pty_->terminate();
}

bool TerminalSession::running() const { return pty_->running(); }

void TerminalSession::resize(int cols, int rows) {
    cols = std::max(cols, 20);
    rows = std::max(rows, 5);
    if (cols == cols_ && rows == rows_) return;
    {
        const std::lock_guard<std::mutex> lk(vtLock_);
        cols_ = cols;
        rows_ = rows;
        vterm_set_size(vt_, rows_, cols_);
        vterm_screen_flush_damage(screen_);
    }
    pty_->resize(cols_, rows_);
    scheduleRepaint();
}

void TerminalSession::limpiar() {
    {
        const std::lock_guard<std::mutex> lk(vtLock_);
        vterm_screen_reset(screen_, 1);
        scrollback_.clear();
    }
    cursorRow_ = 0;
    cursorCol_ = 0;
    scheduleRepaint();
}

void TerminalSession::alimentarParaPruebas(const QByteArray& bytes) {
    const std::lock_guard<std::mutex> lk(vtLock_);
    vterm_input_write(vt_, bytes.constData(), static_cast<size_t>(bytes.size()));
    vterm_screen_flush_damage(screen_);
}

void TerminalSession::convert(const void* vtermCell, Cell& out) const {
    VTermScreenCell c = *static_cast<const VTermScreenCell*>(vtermCell);
    // libvterm marca la mitad derecha de un caracter ancho con 0xFFFFFFFF.
    if (c.chars[0] == static_cast<uint32_t>(-1))
        out.ch = 0;
    else
        out.ch = c.chars[0] == 0 ? U' ' : c.chars[0];
    out.extra.clear();
    // Los combinantes cuelgan de un caracter: sin caracter base no hay nada de
    // que colgar. Ademas de la inicializacion de arriba, esto corta el mismo
    // error si vuelve a entrar por otro lado (el historial, una version nueva
    // de libvterm): un cuadrito de basura sobre un espacio en blanco es de las
    // cosas que cuesta relacionar con su causa.
    if (c.chars[0] != 0 && c.chars[0] != static_cast<uint32_t>(-1)) {
        for (int i = 1; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i] != 0; ++i)
            out.extra += QString::fromUcs4(reinterpret_cast<const char32_t*>(&c.chars[i]), 1);
    }
    out.width = static_cast<quint8>(c.width <= 0 ? 1 : c.width);
    out.bold = c.attrs.bold;
    out.italic = c.attrs.italic;
    out.underline = c.attrs.underline != 0;
    out.strike = c.attrs.strike;
    const bool defFg = VTERM_COLOR_IS_DEFAULT_FG(&c.fg);
    const bool defBg = VTERM_COLOR_IS_DEFAULT_BG(&c.bg);
    vterm_screen_convert_color_to_rgb(screen_, &c.fg);
    vterm_screen_convert_color_to_rgb(screen_, &c.bg);
    out.fg = defFg ? defaultFg_ : qRgb(c.fg.rgb.red, c.fg.rgb.green, c.fg.rgb.blue);
    out.bg = defBg ? defaultBg_ : qRgb(c.bg.rgb.red, c.bg.rgb.green, c.bg.rgb.blue);
    out.defaultBg = defBg;
    if (c.attrs.reverse) {
        std::swap(out.fg, out.bg);
        out.defaultBg = false;
    }
    if (c.attrs.conceal) out.fg = out.bg;
}

void TerminalSession::line(int row, int scroll, std::vector<Cell>& out) const {
    const std::lock_guard<std::mutex> lk(vtLock_);
    lineLocked(row, scroll, out);
}

void TerminalSession::lineLocked(int row, int scroll, std::vector<Cell>& out) const {
    out.assign(static_cast<std::size_t>(cols_), Cell{U' ', {}, defaultFg_, defaultBg_});
    const int sb = static_cast<int>(scrollback_.size());
    const int virt = row - scroll; // < 0: historial
    if (virt < 0) {
        const int idx = sb + virt;
        if (idx < 0 || idx >= sb) return;
        const auto& l = scrollback_[static_cast<std::size_t>(idx)];
        for (std::size_t i = 0; i < l.size() && i < out.size(); ++i)
            out[i] = l[i];
        return;
    }
    if (virt >= rows_) return;
    for (int col = 0; col < cols_; ++col) {
        // EN CERO ANTES DE PEDIRLA, y no es paranoia. `vterm_screen_get_cell`
        // copia `chars` solo hasta el primer 0 INCLUSIVE y deja el resto como
        // estaba: para una celda vacia escribe chars[0]=0 y corta, sin tocar
        // chars[1..5]. Reusando la misma variable en toda la fila, cada celda
        // en blanco heredaba los caracteres COMBINANTES de la ultima celda que
        // si tenia (⚠️ y ⏸️ traen un selector de variacion en chars[1]), y de
        // ahi en adelante la fila entera se dibujaba con una capa de glifos
        // encima. Se veia como bandas de basura sobre las zonas vacias, y al
        // copiarlas salian renglones en blanco —porque el texto ESTABA bien:
        // lo que sobraba era lo que le colgabamos al dibujar—.
        VTermScreenCell c{};
        if (vterm_screen_get_cell(screen_, VTermPos{virt, col}, &c)) {
            convert(&c, out[static_cast<std::size_t>(col)]);
        }
    }
}

QString TerminalSession::text(int r0, int c0, int r1, int c1, int scroll) const {
    const std::lock_guard<std::mutex> lk(vtLock_);
    if (r1 < r0 || (r1 == r0 && c1 < c0)) {
        std::swap(r0, r1);
        std::swap(c0, c1);
    }
    QString out;
    std::vector<Cell> l;
    for (int r = r0; r <= r1; ++r) {
        lineLocked(r, scroll, l);
        const int from = r == r0 ? c0 : 0;
        const int to = r == r1 ? c1 : cols_ - 1;
        QString s;
        for (int c = std::max(0, from); c <= to && c < static_cast<int>(l.size()); ++c) {
            if (l[static_cast<std::size_t>(c)].ch == 0) continue;
            const char32_t ch = l[static_cast<std::size_t>(c)].ch;
            s += QString::fromUcs4(&ch, 1) + l[static_cast<std::size_t>(c)].extra;
        }
        while (s.endsWith(' '))
            s.chop(1);
        out += s;
        if (r != r1) out += '\n';
    }
    return out;
}

void TerminalSession::key(int vtermKey, int mods) {
    vterm_keyboard_key(vt_, static_cast<VTermKey>(vtermKey), static_cast<VTermModifier>(mods));
}

void TerminalSession::unichar(char32_t c, int mods) {
    vterm_keyboard_unichar(vt_, c, static_cast<VTermModifier>(mods));
}

void TerminalSession::mouseButton(int row, int col, int button, bool pressed, int mods) {
    const std::lock_guard<std::mutex> lk(vtLock_);
    vterm_mouse_move(vt_, row, col, static_cast<VTermModifier>(mods));
    vterm_mouse_button(vt_, button, pressed, static_cast<VTermModifier>(mods));
}

void TerminalSession::mouseMove(int row, int col, int mods) {
    const std::lock_guard<std::mutex> lk(vtLock_);
    vterm_mouse_move(vt_, row, col, static_cast<VTermModifier>(mods));
}

void TerminalSession::paste(const QString& text) {
    const std::lock_guard<std::mutex> lk(vtLock_);
    vterm_keyboard_start_paste(vt_);
    QString t = text;
    t.replace("\r\n", "\r").replace('\n', '\r');
    for (const char32_t c : t.toUcs4())
        vterm_keyboard_unichar(vt_, c, VTERM_MOD_NONE);
    vterm_keyboard_end_paste(vt_);
}

void TerminalSession::submit(const QString& text) {
    if (!running()) return;
    paste(text);
    // Un instante despues del pegado: algunas TUIs procesan el Enter aparte.
    QTimer::singleShot(120, this, [this]() {
        const std::lock_guard<std::mutex> lk(vtLock_);
        vterm_keyboard_key(vt_, VTERM_KEY_ENTER, VTERM_MOD_NONE);
    });
}

void TerminalSession::focus(bool in) {
    const std::lock_guard<std::mutex> lk(vtLock_);
    if (in)
        vterm_state_focus_in(state_);
    else
        vterm_state_focus_out(state_);
}

void TerminalSession::writeRaw(const QByteArray& data) { pty_->write(data); }

void TerminalSession::scheduleRepaint() {
    if (!repaint_.isActive()) repaint_.start();
}

} // namespace ba::app::term
