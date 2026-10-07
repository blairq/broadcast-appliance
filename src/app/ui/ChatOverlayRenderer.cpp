// ============================================================================
// src/app/ui/ChatOverlayRenderer.cpp — ver ChatOverlayRenderer.hpp
// ============================================================================

#include "ui/ChatOverlayRenderer.hpp"

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#include "emision/ChatOverlaySource.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"

namespace ba::app {

ChatOverlayRenderer::ChatOverlayRenderer(YouTubeViewModel& yt, ConferenceViewModel& conf, TwitchViewModel& tw,
                                         FacebookViewModel& fb, QObject* parent)
    : QObject(parent), yt_(yt), conf_(conf), tw_(tw), fb_(fb) {
    connect(&timer_, &QTimer::timeout, this, &ChatOverlayRenderer::tick);
    timer_.start(400);
}

std::vector<ChatOverlayRenderer::Message> ChatOverlayRenderer::collect() const {
    std::vector<Message> out;
    for (const QJsonValue& v : yt_.chatMessages()) {
        const QJsonObject m = v.toObject();
        Message msg;
        msg.key = "yt:" + m.value("id").toString();
        msg.source = "youtube";
        msg.author = m.value("author").toString();
        msg.text = m.value("text").toString();
        msg.atMs =
            QDateTime::fromString(m.value("publishedAt").toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
        msg.role = m.value("owner").toBool()       ? "owner"
                   : m.value("moderator").toBool() ? "moderator"
                   : m.value("member").toBool()    ? "member"
                                                   : "";
        out.push_back(msg);
    }
    for (const QJsonValue& v : tw_.chatMessages()) {
        const QJsonObject m = v.toObject();
        Message msg;
        msg.key = "tw:" + m.value("id").toString();
        msg.source = "twitch";
        msg.author = m.value("author").toString();
        msg.text = m.value("text").toString();
        msg.color = m.value("color").toString();
        msg.atMs = static_cast<qint64>(m.value("atMs").toDouble());
        msg.role = m.value("broadcaster").toBool()  ? "owner"
                   : m.value("moderator").toBool()  ? "moderator"
                   : m.value("subscriber").toBool() ? "member"
                                                    : "";
        out.push_back(msg);
    }
    for (const QJsonValue& v : fb_.chatMessages()) {
        const QJsonObject m = v.toObject();
        Message msg;
        msg.key = "fb:" + m.value("id").toString();
        msg.source = "facebook";
        msg.author = m.value("author").toString();
        msg.text = m.value("text").toString();
        msg.atMs = static_cast<qint64>(m.value("atMs").toDouble());
        out.push_back(msg);
    }
    // El chat de la sala llega por eventos (no siempre en el snapshot): el
    // modelo de chat de la sala tiene todo lo recibido.
    for (const QJsonValue& v : static_cast<JsonListModel*>(conf_.chat())->items()) {
        const QJsonObject m = v.toObject();
        if (m.value("to").toString() != "all") continue; // lo privado no sale al aire
        Message msg;
        msg.key = "room:" + QString::number(m.value("seq").toInteger());
        msg.source = "room";
        msg.author = m.value("fromId").toString() == "host" ? "Produccion" : m.value("fromName").toString();
        msg.text = m.value("text").toString();
        msg.atMs = static_cast<qint64>(m.value("atMs").toDouble());
        out.push_back(msg);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Message& a, const Message& b) { return a.atMs < b.atMs; });
    return out;
}

QImage ChatOverlayRenderer::render(const std::vector<Message>& msgs, int width, int height, int fontSize,
                                   int bgOpacity, bool showNames, bool tagSource) {
    QImage img(width, height, QImage::Format_ARGB32); // BGRA en memoria, alfa recto
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    QFont body;
    body.setFamilies({"Inter", "Noto Sans", "DejaVu Sans", "Sans"});
    body.setPixelSize(fontSize);
    QFont name = body;
    name.setBold(true);
    name.setPixelSize(std::max(10, fontSize * 8 / 10));
    const int pad = std::max(6, fontSize / 2);
    const int gap = std::max(4, fontSize / 3);
    const int textW = width - 4 * pad;
    int y = height - pad;
    // De abajo hacia arriba: el mas nuevo abajo, los viejos se van por arriba.
    for (auto it = msgs.rbegin(); it != msgs.rend() && y > 0; ++it) {
        const QFontMetrics fb(body);
        const QFontMetrics fn(name);
        const QRect textRect = fb.boundingRect(QRect(0, 0, textW, 10000), Qt::TextWordWrap, it->text);
        const int nameH = showNames ? fn.height() : 0;
        const int h = textRect.height() + nameH + 2 * pad;
        const int top = y - h;
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(pad, top, width - 2 * pad, h), pad, pad);
        p.fillPath(bubble, QColor(10, 10, 14, bgOpacity * 255 / 100));
        if (showNames) {
            QColor c = it->source == "room"       ? QColor(52, 211, 153)
                       : it->source == "twitch"   ? QColor(169, 112, 255)
                       : it->source == "facebook" ? QColor(80, 150, 255)
                                                  : QColor(255, 110, 100);
            if (!it->color.isEmpty() && QColor(it->color).isValid()) c = QColor(it->color).lighter(115);
            if (it->role == "owner")
                c = QColor(255, 214, 0);
            else if (it->role == "moderator")
                c = QColor(110, 150, 255);
            else if (it->role == "member")
                c = QColor(60, 200, 90);
            p.setFont(name);
            p.setPen(c);
            QString label = it->author;
            if (tagSource) {
                const QString icon = it->source == "room"       ? "● "
                                     : it->source == "twitch"   ? "◆ "
                                     : it->source == "facebook" ? "f · "
                                                                : "▶ ";
                label = icon + label;
            }
            p.drawText(QRect(2 * pad, top + pad, textW, nameH), Qt::AlignLeft | Qt::AlignVCenter,
                       fn.elidedText(label, Qt::ElideRight, textW));
        }
        p.setFont(body);
        p.setPen(QColor(245, 245, 248));
        p.drawText(QRect(2 * pad, top + pad + nameH, textW, textRect.height()), Qt::TextWordWrap, it->text);
        y = top - gap;
    }
    return img;
}

bool ChatOverlayRenderer::destacar(const QString& key) {
    for (const Message& m : collect())
        if (m.key == key) {
            destacados_.insert(key);
            return true;
        }
    return false;
}

void ChatOverlayRenderer::quitarDestacado(const QString& key) {
    if (key.isEmpty())
        destacados_.clear();
    else
        destacados_.remove(key);
}

void ChatOverlayRenderer::tick() {
    const std::vector<chatoverlay::Instance> list = chatoverlay::instances();
    if (list.empty()) return;
    const std::vector<Message> all = collect();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const chatoverlay::Instance& in : list) {
        std::vector<Message> msgs;
        for (const Message& m : all) {
            if ((m.source == "youtube" && !in.showYoutube) || (m.source == "twitch" && !in.showTwitch) ||
                (m.source == "facebook" && !in.showFacebook) || (m.source == "room" && !in.showRoom))
                continue;
            if (in.fadeSec > 0 && now - m.atMs > in.fadeSec * 1000LL) continue;
            if (in.soloDestacados && !destacados_.contains(m.key)) continue;
            msgs.push_back(m);
        }
        if (static_cast<int>(msgs.size()) > in.maxMessages)
            msgs.erase(msgs.begin(), msgs.end() - in.maxMessages);
        QString sig = QString::number(in.settingsVersion) + ":" + QString::number(in.width) + "x" +
                      QString::number(in.height);
        for (const Message& m : msgs)
            sig += "|" + m.key;
        if (lastSig_.value(in.id) == sig) continue;
        lastSig_.insert(in.id, sig);
        const QImage frame = render(msgs, in.width, in.height, in.fontSize, in.bgOpacity, in.showNames,
                                    (in.showYoutube + in.showTwitch + in.showFacebook + in.showRoom) > 1);
        chatoverlay::push(in.id, frame.constBits(), frame.width(), frame.height());
    }
}

} // namespace ba::app
