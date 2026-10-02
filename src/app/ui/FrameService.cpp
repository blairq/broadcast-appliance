// ============================================================================
// src/app/ui/FrameService.cpp — ver FrameService.hpp
// ============================================================================

#include "ui/FrameService.hpp"

#include "BroadcastEngine.hpp"
#include "ipc/SpoutTransport.hpp"

namespace ba::app {

FrameService::FrameService(BroadcastEngine& engine, int guests, QObject* parent)
    : QObject(parent), engine_(engine) {
    slots_["program"];
    slots_["scene"];
    setGuests(guests);
}

FrameService::~FrameService() = default;

QStringList FrameService::keys() const {
    // Lo que se puede elegir para fondos, monitores y ventanas de salida. Los
    // previews del lobby ("lobby:k") y el feed propio ("operator") tambien se
    // pueden mostrar por clave, pero no se ofrecen en los menus.
    QStringList out;
    out << "program";
    // Segunda, pegada al programa, porque son los dos buses de la mesa: lo que
    // sale y lo que va a salir. Se miran juntos o no se miran.
    out << "armada";
    for (int i = 1; i <= guests_; ++i) {
        out << QString("guest:%1").arg(i);
    }
    for (int i = 1; i <= guests_; ++i) {
        out << QString("share:%1").arg(i);
    }
    return out;
}

QString FrameService::label(const QString& key) const {
    const auto nameOr = [this](int slot, const QString& fallback) {
        const QString n = slot >= 1 && slot <= names_.size() ? names_.at(slot - 1) : QString();
        return n.isEmpty() ? fallback : n;
    };
    if (key == "program") return "Programa";
    if (key == "armada") return "Armada (preview)";
    if (key == "operator") return "Tu cámara";
    if (key.startsWith("guest:")) return nameOr(key.mid(6).toInt(), "Invitado " + key.mid(6));
    if (key.startsWith("share:")) return nameOr(key.mid(6).toInt(), "Invitado " + key.mid(6)) + " — pantalla";
    if (key.startsWith("lobby:")) return "Lobby " + key.mid(6);
    if (key.startsWith("backstage:")) return "Fila " + key.mid(10);
    if (key.startsWith("entrada:")) return key.mid(8);
    return key;
}

void FrameService::setArmada(const QString& escena) {
    if (escena == armada_) return;
    armada_ = escena;
    // 960 de ancho: alcanza para un monitor de tablero o un fondo, y no cuesta
    // como renderizar 1080p de una escena que ni siquiera esta al aire.
    engine_.enableScenePreview("armada", escena.toStdString(), 960);
    if (escena.isEmpty()) slots_.erase("armada");
}

void FrameService::mirarEntrada(const QString& entrada, bool mirar) {
    if (entrada.isEmpty()) return;
    int& n = entradas_[entrada];
    const std::string canal = "entrada:" + entrada.toStdString();
    if (mirar) {
        // 960 de ancho, como la armada: para apuntar alcanza y sobra.
        if (n++ == 0) engine_.enableScenePreview(canal, entrada.toStdString(), 960);
        return;
    }
    if (n > 0 && --n == 0) {
        engine_.disableScenePreview(canal);
        entradas_.erase(entrada);
        entradaBufs_.erase(entrada);
        slots_.erase("entrada:" + entrada);
    }
}

void FrameService::setGuests(int guests) {
    guests_ = guests;
    emit keysChanged();
}

void FrameService::setGuestNames(const QStringList& names) {
    if (names == names_) return;
    names_ = names;
    emit keysChanged(); // las etiquetas cambian
}

std::wstring FrameService::channelOf(const QString& key) {
    if (key == "operator") return L"BA.Operator";
    const std::wstring n = key.section(':', 1).toStdWString();
    if (key.startsWith("guest:")) return L"BA.Guest." + n;
    if (key.startsWith("share:")) return L"BA.GuestShare." + n;
    if (key.startsWith("lobby:")) return L"BA.Lobby." + n;
    if (key.startsWith("backstage:")) return L"BA.Backstage." + n;
    return {};
}

bool FrameService::latest(const QString& key, QImage& out, std::uint64_t& serial) const {
    auto it = slots_.find(key);
    if (it == slots_.end() || it->second.serial == serial || it->second.image.isNull()) {
        return false;
    }
    out = it->second.image; // copia implicita (COW): barata
    serial = it->second.serial;
    return true;
}

void FrameService::poll() {
    bool any = false;
    // Programa: vista previa del motor (BGRA, ya escalada por libobs).
    int w = 0;
    int h = 0;
    if (engine_.latestPreview(programBuf_, w, h, programSerial_)) {
        Slot& s = slots_["program"];
        s.image = QImage(programBuf_.data(), w, h, w * 4, QImage::Format_ARGB32).copy();
        ++s.serial;
        any = true;
    }
    // La escena ARMADA: la que va a salir con el proximo take, renderizada
    // aunque no este al aire. Canal propio del motor, aparte del editor: son
    // dos escenas distintas al mismo tiempo y con un solo canal se pisaban
    // —abrir el editor apagaba el monitor del director—.
    if (!armada_.isEmpty() && engine_.renderScenePreview("armada", armadaBuf_, w, h)) {
        Slot& s = slots_["armada"];
        s.image = QImage(armadaBuf_.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
        ++s.serial;
        any = true;
    }
    // Editor de escenas: la escena que se esta editando, renderizada aunque no
    // este al aire (la activa StudioViewModel::setScenePreview).
    if (engine_.renderScenePreview(sceneBuf_, w, h)) {
        Slot& s = slots_["scene"];
        s.image = QImage(sceneBuf_.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
        ++s.serial;
        any = true;
    }
    ++vueltas_;
    // Entradas que pidió alguien (mirarEntrada). Si la entrada se borró y
    // volvió, el canal se vuelve a tomar.
    for (auto& [entrada, n] : entradas_) {
        const std::string canal = "entrada:" + entrada.toStdString();
        if (engine_.scenePreviewName(canal).empty()) {
            if (vueltas_ % 30 != 0) continue; // una vez por segundo, no a 30 Hz
            engine_.enableScenePreview(canal, entrada.toStdString(), 960);
        }
        auto& buf = entradaBufs_[entrada];
        if (engine_.renderScenePreview(canal, buf, w, h)) {
            Slot& s = slots_["entrada:" + entrada];
            s.image = QImage(buf.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
            ++s.serial;
            any = true;
        }
    }
    // Canales de memoria compartida (con resuscripcion si el escritor cambia):
    // invitados y sus pantallas (B), previews del lobby (B), feed propio (A).
    std::vector<QString> wanted;
    for (int i = 1; i <= guests_; ++i) {
        wanted.push_back(QString("guest:%1").arg(i));
        wanted.push_back(QString("share:%1").arg(i));
    }
    for (int k = 1; k <= lobbySlots_; ++k) {
        wanted.push_back(QString("lobby:%1").arg(k));
    }
    // Previews de la fila de oradores (B publica hasta 8; los que no existen
    // simplemente no se suscriben).
    for (int k = 1; k <= kBackstageSlots; ++k) {
        wanted.push_back(QString("backstage:%1").arg(k));
    }
    wanted.push_back("operator");
    for (const QString& key : wanted) {
        Slot& s = slots_[key];
        if (!s.receiver) {
            s.receiver = std::make_unique<ba::ipc::SpoutReceiver>();
        }
        auto& r = *s.receiver;
        if (r.stale()) {
            r = ba::ipc::SpoutReceiver{};
        }
        if (r.width() == 0) {
            const std::wstring name = channelOf(key);
            if (name.empty() || !r.subscribe(name.c_str())) {
                continue;
            }
            s.buf.assign(r.frameBytes(), 0);
        }
        ba::ipc::FrameMeta meta{};
        if (r.hasData() && r.receive(s.buf.data(), s.buf.size(), meta)) {
            s.image =
                QImage(s.buf.data(), r.width(), r.height(), r.width() * 4, QImage::Format_ARGB32).copy();
            ++s.serial;
            any = true;
        }
    }
    if (any) {
        emit framesUpdated();
    }
}

} // namespace ba::app
