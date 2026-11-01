// test_chat_hilo.cpp — un hilo del chat de la sala como modelo filtrado (ui/ChatHilo)
//
//   La sala ("all") y el privado con alguien (sus dos direcciones). Un mensaje nuevo
//   entra solo en el hilo que corresponde (sin que la vista vuelva a pedir la lista), y
//   cambiar de hilo cambia lo que se ve.

#include <QCoreApplication>
#include <QStandardItemModel>

#include <cstdio>

#include "ui/ChatHilo.hpp"

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

constexpr int kPara = Qt::UserRole + 1;
constexpr int kDe = Qt::UserRole + 2;

class Chat : public QStandardItemModel {
   public:
    QHash<int, QByteArray> roleNames() const override { return {{kPara, "to"}, {kDe, "fromId"}}; }
    void mensaje(const QString& para, const QString& de) {
        auto* i = new QStandardItem;
        i->setData(para, kPara);
        i->setData(de, kDe);
        appendRow(i);
    }
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using ba::app::ChatHilo;
    CHECK(ChatHilo::esDelHilo("", "all", "ana"), "lo de la sala va a la sala");
    CHECK(!ChatHilo::esDelHilo("", "ana", "host"), "un privado no va a la sala");
    CHECK(ChatHilo::esDelHilo("ana", "ana", "host") && ChatHilo::esDelHilo("ana", "host", "ana"),
          "el privado con ana: lo que le mandamos y lo que contestó");
    CHECK(!ChatHilo::esDelHilo("ana", "all", "ana"), "lo que ana dijo a la sala no es del privado");

    Chat chat;
    chat.mensaje("all", "ana");
    chat.mensaje("ana", "host");
    ChatHilo sala;
    sala.setFuente(&chat);
    ChatHilo privado;
    privado.setFuente(&chat);
    privado.setHilo("ana");
    CHECK(sala.rowCount() == 1 && privado.rowCount() == 1, "cada vista con su hilo, del mismo chat");
    chat.mensaje("host", "ana");
    chat.mensaje("all", "beto");
    CHECK(privado.rowCount() == 2 && sala.rowCount() == 2, "lo nuevo entra solo en su hilo");
    sala.setHilo("beto");
    CHECK(sala.rowCount() == 0, "cambiar de hilo cambia lo que se ve");
    sala.setHilo("");
    CHECK(sala.rowCount() == 2, "y volver a la sala");
    if (g_fallas == 0) std::printf("test_chat_hilo: todo OK\n");
    return g_fallas == 0 ? 0 : 1;
}
