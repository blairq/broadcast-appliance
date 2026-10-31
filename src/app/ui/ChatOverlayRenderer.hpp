// ============================================================================
// src/app/ui/ChatOverlayRenderer.hpp — Dibuja la fuente "Chat en vivo"
//
// Junta los mensajes que la app ya tiene (chat del vivo de YouTube, que lee
// YouTubeViewModel, y chat de la sala, que llega en el snapshot de B), los
// dibuja con QPainter sobre fondo transparente (burbujas con nombre en color)
// y entrega el cuadro a cada instancia de la fuente (ChatOverlaySource), con
// su propio tamaño y estilo. Solo redibuja cuando algo cambio.
//
// DOS HILOS. Juntar los mensajes es leer modelos de la interfaz: eso queda en
// la interfaz (cada 400 ms), y deja una COPIA. Filtrar, desvanecer los viejos
// (fade_sec), dibujar y entregar corre en su hilo («chat-aire», ver
// emision/PintorAire.hpp): con la interfaz trabada, el chat al aire sigue
// desvaneciendo lo que tiene.
// ============================================================================
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <mutex>
#include <vector>

namespace ba::app::fuentes {
class PintorAire;
}

namespace ba::app {

class YouTubeViewModel;
class ConferenceViewModel;
class TwitchViewModel;
class FacebookViewModel;

class ChatOverlayRenderer : public QObject {
    Q_OBJECT
   public:
    struct Message {
        QString key;    ///< unico (para saber si cambio algo)
        QString source; ///< youtube | twitch | facebook | room
        QString color;  ///< color propio del autor (Twitch), "" = el de la plataforma
        QString author;
        QString text;
        qint64 atMs = 0;
        QString role; ///< owner | moderator | member | "" (YouTube)
    };

    ChatOverlayRenderer(YouTubeViewModel& yt, ConferenceViewModel& conf, TwitchViewModel& tw,
                        FacebookViewModel& fb, QObject* parent = nullptr);
    ~ChatOverlayRenderer() override;

    /// Dibuja un cuadro (lo usan tambien los tests y el MCP para mostrarlo).
    static QImage render(const std::vector<Message>& msgs, int width, int height, int fontSize, int bgOpacity,
                         bool showNames, bool tagSource);
    std::vector<Message> collect() const;
    /// Destacar un mensaje (por su `key`, de collect()) para que salga en los
    /// overlays en modo «solo destacados». false si no está entre los recientes.
    bool destacar(const QString& key);
    void quitarDestacado(const QString& key); ///< "" = todos
    QStringList destacados() const { return QStringList(destacados_.begin(), destacados_.end()); }

   private:
    /// En la interfaz: junta los mensajes y deja la copia para el hilo que pinta.
    void recoger();
    /// En el hilo que pinta: filtra, desvanece, dibuja y entrega lo que cambió.
    void pintar();
    YouTubeViewModel& yt_;
    ConferenceViewModel& conf_;
    TwitchViewModel& tw_;
    FacebookViewModel& fb_;
    QTimer timer_;
    QSet<QString> destacados_; // keys de los mensajes destacados (interfaz)
    /// La copia que pinta el otro hilo (bajo fotoM_).
    std::mutex fotoM_;
    std::vector<Message> foto_;
    QSet<QString> fotoDestacados_;
    QHash<quint64, QString> lastSig_; // instancia -> firma de lo ultimo entregado (solo el hilo que pinta)
    std::unique_ptr<fuentes::PintorAire> pintor_; ///< último: se apaga primero
};

} // namespace ba::app
