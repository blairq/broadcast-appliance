// src/app/tests/test_descargas.cpp — la cola única de descargas (net/Descargas)
//
// Sin internet: un servidor HTTP de acá mismo (QTcpServer en 127.0.0.1).
//   - lo que ya está en la cola o bajándose no se encola de nuevo; lo que ya
//     está (yaEsta) tampoco;
//   - de a dos en total (también dos del mismo tipo);
//   - cancelar lo que espera y lo que anda; reintentar;
//   - una descarga cortada a la mitad sigue donde quedó (Range, 206) y el
//     archivo final es el mismo, byte por byte.

#include "net/Descargas.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

using ba::app::net::Descargas;
namespace fs = std::filesystem;

namespace {
int g_fallas = 0;
void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}
bool esperar(const std::function<bool()>& f, int ms) {
    QElapsedTimer t;
    t.start();
    while (!f()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
std::string estadoDe(const std::string& id) {
    for (const QVariant& v : Descargas::la().lista()) {
        const QVariantMap m = v.toMap();
        if (m["id"].toString().toStdString() == id) return m["estado"].toString().toStdString();
    }
    return "";
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    Descargas::iniciar(fs::path(tmp.path().toStdString()));
    Descargas& d = Descargas::la();

    // --- Duplicados, por tipo, cancelar ------------------------------------------------
    std::atomic<int> corriendoA{0}, maxA{0}, corriendo{0}, maxTotal{0};
    const auto trabajoLento = [&](std::atomic<int>* delTipo, std::atomic<int>* maxDelTipo) {
        return [&, delTipo, maxDelTipo](Descargas::Contexto& ctx) -> std::string {
            const int n = ++corriendo;
            if (n > maxTotal) maxTotal = n;
            if (delTipo != nullptr) {
                const int m = ++*delTipo;
                if (m > *maxDelTipo) *maxDelTipo = m;
            }
            for (int i = 0; i < 30 && !ctx.cancelado(); ++i) {
                ctx.avance(i / 30.0, "andando");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (delTipo != nullptr) --*delTipo;
            --corriendo;
            return ctx.cancelado() ? "cancelada" : "";
        };
    };
    const auto pedido = [&](const std::string& id, const std::string& tipo) {
        Descargas::Pedido p;
        p.id = id;
        p.tipo = tipo;
        p.titulo = id;
        p.trabajo = trabajoLento(tipo == "a" ? &corriendoA : nullptr, &maxA);
        return p;
    };
    check(d.pedir(pedido("a1", "a")) == Descargas::Resultado::Encolada, "a1 se encola");
    check(d.pedir(pedido("a2", "a")) == Descargas::Resultado::Encolada, "a2 se encola");
    check(d.pedir(pedido("a3", "a")) == Descargas::Resultado::Encolada, "a3 se encola");
    check(d.pedir(pedido("b1", "b")) == Descargas::Resultado::Encolada, "b1 se encola");
    check(d.pedir(pedido("c1", "c")) == Descargas::Resultado::Encolada, "c1 se encola");
    const auto r = d.pedir(pedido("a2", "a"));
    check(r == Descargas::Resultado::YaEnCola || r == Descargas::Resultado::Bajando, "a2 otra vez: no se repite");
    {
        Descargas::Pedido p = pedido("z", "z");
        p.yaEsta = [] { return true; };
        check(d.pedir(p) == Descargas::Resultado::YaEsta, "lo que ya está no se baja");
    }
    d.cancelar("a3");
    check(estadoDe("a3") == "cancelado", "a3 cancelada mientras esperaba");
    check(esperar([&] { return d.activas() == 0; }, 10000), "la cola se vacía");
    check(maxA.load() == 2, "dos del mismo tipo a la vez");
    check(maxTotal.load() <= 2, "de a dos en total");
    check(estadoDe("a1") == "listo" && estadoDe("b1") == "listo" && estadoDe("c1") == "listo", "terminan bien");
    check(d.reintentar("a3"), "a3 se reintenta");
    check(esperar([&] { return estadoDe("a3") == "listo"; }, 5000), "a3 termina al reintentar");
    {
        bool termino = false, okFinal = true;
        Descargas::Pedido p = pedido("larga", "a");
        p.alTerminar = [&](bool ok, const std::string&) {
            termino = true;
            okFinal = ok;
        };
        d.pedir(p);
        esperar([&] { return estadoDe("larga") == "bajando"; }, 3000);
        d.cancelar("larga");
        check(esperar([&] { return termino; }, 5000) && !okFinal && estadoDe("larga") == "cancelado",
              "cancelar la que anda: termina como cancelada (alTerminar ok=false)");
    }

    // --- Seguir una descarga cortada ------------------------------------------------------
    QByteArray cuerpo;
    for (int i = 0; i < 400000; ++i) cuerpo.append(static_cast<char>((i * 7919) % 251));
    std::atomic<int> pedidos{0};
    std::atomic<bool> vioRango{false};
    QTcpServer srv;
    srv.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&srv, &QTcpServer::newConnection, [&]() {
        QTcpSocket* s = srv.nextPendingConnection();
        auto buf = std::make_shared<QByteArray>();
        QObject::connect(s, &QTcpSocket::readyRead, s, [&, s, buf]() {
            buf->append(s->readAll());
            if (!buf->contains("\r\n\r\n")) return;
            const int n = ++pedidos;
            qint64 desde = 0;
            const int i = buf->indexOf("Range: bytes=");
            if (i >= 0) {
                vioRango = true;
                desde = buf->mid(i + 13, buf->indexOf('-', i) - i - 13).toLongLong();
            }
            if (n == 1) { // la primera: la mitad y se corta
                s->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(cuerpo.size()) +
                         "\r\nConnection: close\r\n\r\n");
                s->write(cuerpo.left(cuerpo.size() / 2));
                s->flush();
                s->disconnectFromHost();
                return;
            }
            const QByteArray resto = cuerpo.mid(desde);
            s->write((desde > 0 ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(desde) +
                                      "-" + QByteArray::number(cuerpo.size() - 1) + "/" +
                                      QByteArray::number(cuerpo.size()) + "\r\n"
                                : QByteArray("HTTP/1.1 200 OK\r\n")) +
                     "Content-Length: " + QByteArray::number(resto.size()) + "\r\nConnection: close\r\n\r\n");
            s->write(resto);
            s->flush();
            s->disconnectFromHost();
        });
        QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
    const std::string url = "http://127.0.0.1:" + std::to_string(srv.serverPort()) + "/archivo.bin";
    const fs::path destino = fs::path(tmp.path().toStdString()) / "bajado" / "archivo.bin";
    Descargas::Pedido p;
    p.id = "archivo";
    p.tipo = "prueba";
    p.titulo = "archivo";
    p.trabajo = [&](Descargas::Contexto& ctx) { return ctx.bajar(url, destino); };
    d.pedir(p);
    check(esperar([&] { return estadoDe("archivo") == "error"; }, 10000), "la primera se corta a la mitad (error)");
    std::error_code ec;
    const auto parte = fs::exists(destino.string() + ".parte", ec) ? fs::file_size(destino.string() + ".parte", ec) : 0;
    check(parte > 0 && parte < static_cast<std::uintmax_t>(cuerpo.size()), "queda la parte bajada");
    check(d.reintentar("archivo"), "se reintenta");
    check(esperar([&] { return estadoDe("archivo") == "listo"; }, 10000), "la segunda termina");
    check(vioRango.load(), "la segunda pidió desde donde quedó (Range)");
    QByteArray bajado;
    {
        std::ifstream f(destino, std::ios::binary);
        bajado = QByteArray::fromStdString(std::string(std::istreambuf_iterator<char>(f), {}));
    }
    check(bajado == cuerpo, "el archivo final es igual, byte por byte");
    check(fs::exists(fs::path(tmp.path().toStdString()) / "Runtime" / "descargas.json", ec), "queda el historial");

    std::printf(g_fallas == 0 ? "== test_descargas: OK ==\n" : "== test_descargas: %d fallas ==\n", g_fallas);
    std::fflush(stdout);
    std::_Exit(g_fallas == 0 ? 0 : 1); // la cola es de toda la vida del programa
}
