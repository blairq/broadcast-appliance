// ============================================================================
// src/app/term/TerminalSession.hpp — Terminal (libvterm) + programa (Pty)
//
// Interpreta la salida del programa con libvterm (xterm: colores de 24 bits,
// pantalla alternativa, raton, pegado entre corchetes) y guarda el historial
// que se va por arriba. TerminalView la dibuja; la sesion vive en el
// AgentViewModel, asi que cambiar de pestaña no corta nada.
// ============================================================================
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>

#include <mutex>

#include <deque>
#include <memory>
#include <vector>

#include "term/Pty.hpp"

struct VTerm;
struct VTermScreen;
struct VTermState;

namespace ba::app::term {

struct Cell {
    char32_t ch = U' '; ///< primer codigo del grafema (0 = mitad derecha de uno ancho)
    QString extra;      ///< codigos combinados (acentos, emoji compuestos)
    QRgb fg = 0;
    QRgb bg = 0;
    bool defaultBg = true;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    quint8 width = 1;
};

class TerminalSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int scrollback READ scrollbackSize NOTIFY contentChanged)
   public:
    explicit TerminalSession(QObject* parent = nullptr);
    ~TerminalSession() override;

    bool start(const Pty::Options& opt, QString* error = nullptr);
    void stop();
    bool running() const;
    QString title() const { return title_; }

    // --- Pantalla ------------------------------------------------------------------
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void resize(int cols, int rows);

    /// Borra la pantalla Y el historial. Es para cuando lo que hay en pantalla
    /// ya no es de nadie: al cambiar de agente, lo que dejó el anterior no es
    /// «lo de antes», es de otro programa.
    void limpiar();

    /// Mete bytes en el emulador como si los hubiera escrito el programa. Es
    /// para reproducir una pantalla rota a partir de un volcado (BA_TERM_DUMP):
    /// una terminal se rompe por una secuencia puntual entre miles, y mirando
    /// el resultado no hay forma de saber cual fue.
    void alimentarParaPruebas(const QByteArray& bytes);
    /// Fila `row` de la vista con `scroll` lineas de historial hacia arriba
    /// (row < 0 no existe; row >= rows no existe).
    void line(int row, int scroll, std::vector<Cell>& out) const;
    int scrollbackSize() const { return static_cast<int>(scrollback_.size()); }
    bool cursorVisible() const { return cursorVisible_; }
    int cursorRow() const { return cursorRow_; }
    int cursorCol() const { return cursorCol_; }
    bool altScreen() const { return altScreen_; }
    /// El programa pidio eventos de raton.
    bool mouseMode() const { return mouseMode_ != 0; }
    /// El programa quiere también el movimiento sin botón apretado (modo 1003, VTERM_PROP_MOUSE_MOVE).
    bool mouseMoveMode() const { return mouseMode_ == 3; } // VTERM_PROP_MOUSE_MOVE (vterm.h no se incluye acá)
    QRgb defaultFg() const { return defaultFg_; }
    QRgb defaultBg() const { return defaultBg_; }
    /// Texto entre dos posiciones (fila de la vista con `scroll`).
    QString text(int r0, int c0, int r1, int c1, int scroll) const;

    // --- Entrada -------------------------------------------------------------------
    void key(int vtermKey, int mods);
    void unichar(char32_t c, int mods);
    void mouseButton(int row, int col, int button, bool pressed, int mods);
    void mouseMove(int row, int col, int mods);
    /// Pegar (con corchetes si el programa los pidio).
    void paste(const QString& text);
    /// Escribe un mensaje en el prompt del programa y lo manda (Enter). Lo usa
    /// el sistema de alertas para "despertar" al agente.
    void submit(const QString& text);
    void focus(bool in);
    /// Bytes crudos al programa.
    void writeRaw(const QByteArray& data);

   signals:
    void contentChanged();
    void runningChanged();
    void titleChanged();
    void finished(int exitCode);
    void bell();

   private:
    friend struct SessionCallbacks; // callbacks de libvterm (TerminalSession.cpp)

    /// Igual que line(), pero con vtLock_ ya tomado.
    void lineLocked(int row, int scroll, std::vector<Cell>& out) const;

    /// libvterm y scrollback_ se tocan desde DOS hilos: el de la GUI (entrada
    /// del PTY, teclado, raton, resize) y el de render de QML (paint -> line()
    /// y text(), porque TerminalView es un QQuickPaintedItem). Sin esto se leen
    /// celdas a medio escribir y salen caracteres al azar.
    mutable std::mutex vtLock_;
    void convert(const void* vtermCell, Cell& out) const;
    void flushOutput();
    void scheduleRepaint();

    VTerm* vt_ = nullptr;
    VTermScreen* screen_ = nullptr;
    VTermState* state_ = nullptr;
    std::unique_ptr<Pty> pty_;
    int rows_ = 30;
    int cols_ = 100;
    std::deque<std::vector<Cell>> scrollback_;
    std::size_t maxScrollback_ = 10000;
    bool cursorVisible_ = true;
    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool altScreen_ = false;
    int mouseMode_ = 0;
    QString title_;
    QString titleBuf_;
    QRgb defaultFg_ = 0;
    QRgb defaultBg_ = 0;
    QTimer repaint_;
};

} // namespace ba::app::term
