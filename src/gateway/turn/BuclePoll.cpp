// ============================================================================
// src/gateway/turn/BuclePoll.cpp — ver BuclePoll.hpp
// ============================================================================
#include "turn/BuclePoll.hpp"

#include <future>
#include <utility>

#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef WSAPOLLFD PollFd;
#define BA_FD_INVALIDO INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef struct pollfd PollFd;
#define BA_FD_INVALIDO (-1)
#endif

namespace ba::gateway::turn {

namespace {

void cerrar(Fd f) {
    if (f == static_cast<Fd>(BA_FD_INVALIDO)) return;
#ifdef _WIN32
    closesocket(f);
#else
    ::close(f);
#endif
}

Fd despertadorNuevo() {
    Fd f = static_cast<Fd>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (f == static_cast<Fd>(BA_FD_INVALIDO)) return f;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    socklen_t l = sizeof(sa);
    if (bind(f, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 ||
        getsockname(f, reinterpret_cast<sockaddr*>(&sa), &l) != 0 ||
        connect(f, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        cerrar(f);
        return static_cast<Fd>(BA_FD_INVALIDO);
    }
#ifdef _WIN32
    u_long si = 1;
    ioctlsocket(f, FIONBIO, &si);
#else
    fcntl(f, F_SETFL, fcntl(f, F_GETFL, 0) | O_NONBLOCK);
#endif
    return f;
}

} // namespace

BuclePoll::BuclePoll(std::string nombre) : nombre_(std::move(nombre)) {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    despertador_ = despertadorNuevo();
    hilo_ = std::thread([this] {
        ba::platform::nombrarHilo(nombre_.c_str());
        ba::platform::nucleos::alGrupoGateway();
        bucle();
    });
    id_ = hilo_.get_id();
}

BuclePoll::~BuclePoll() {
    parar();
    cerrar(despertador_);
#ifdef _WIN32
    WSACleanup();
#endif
}

void BuclePoll::parar() {
    if (!corriendo_.exchange(false)) return;
    despertarHilo();
    if (hilo_.joinable()) hilo_.join();
}

bool BuclePoll::enEsteHilo() const { return std::this_thread::get_id() == hilo_.get_id(); }

void BuclePoll::vigilar(Fd fd, int eventos, Aviso aviso) {
    Vigilado v;
    v.eventos = eventos;
    v.aviso = std::make_shared<Aviso>(std::move(aviso));
    vigilados_[fd] = std::move(v);
}

void BuclePoll::cambiar(Fd fd, int eventos) {
    auto it = vigilados_.find(fd);
    if (it != vigilados_.end()) it->second.eventos = eventos;
}

void BuclePoll::dejar(Fd fd) { vigilados_.erase(fd); }

void BuclePoll::despertarHilo() {
    if (despertador_ == static_cast<Fd>(BA_FD_INVALIDO) || despierto_.exchange(true)) return;
    const char b = 1;
    ::send(despertador_, &b, 1, 0);
}

void BuclePoll::enHilo(std::function<void()> f) {
    {
        std::lock_guard<std::mutex> l(m_);
        tareas_.push_back(std::move(f));
    }
    despertarHilo();
}

void BuclePoll::enHiloLuego(int ms, std::function<void()> f) {
    {
        std::lock_guard<std::mutex> l(m_);
        luego_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), std::move(f));
    }
    despertarHilo();
}

void BuclePoll::enHiloEsperando(const std::function<void()>& f) {
    if (enEsteHilo() || !corriendo_.load()) {
        f();
        return;
    }
    std::promise<void> listo;
    auto fut = listo.get_future();
    enHilo([&f, &listo] {
        f();
        listo.set_value();
    });
    fut.wait();
}

void BuclePoll::bucle() {
    std::vector<PollFd> fds;
    std::vector<Fd> cuales;
    while (corriendo_.load()) {
        fds.clear();
        cuales.clear();
        {
            PollFd p{};
            p.fd = despertador_;
            p.events = POLLIN;
            fds.push_back(p);
            cuales.push_back(despertador_);
        }
        for (const auto& [fd, v] : vigilados_) {
            PollFd p{};
            p.fd = fd;
            p.events = short(((v.eventos & kLeer) ? POLLIN : 0) | ((v.eventos & kEscribir) ? POLLOUT : 0));
            fds.push_back(p);
            cuales.push_back(fd);
        }
        int espera = 100;
        {
            std::lock_guard<std::mutex> l(m_);
            if (!tareas_.empty()) espera = 0;
            if (!luego_.empty()) {
                const auto falta = std::chrono::duration_cast<std::chrono::milliseconds>(luego_.begin()->first -
                                                                                          std::chrono::steady_clock::now())
                                       .count();
                if (falta < espera) espera = falta < 0 ? 0 : int(falta);
            }
        }
#ifdef _WIN32
        const int r = WSAPoll(fds.data(), ULONG(fds.size()), espera);
#else
        const int r = ::poll(fds.data(), nfds_t(fds.size()), espera);
#endif
        if (r > 0) {
            if (fds[0].revents != 0) {
                char basura[64];
                while (::recv(despertador_, basura, int(sizeof(basura)), 0) > 0) {
                }
                despierto_ = false;
            }
            for (std::size_t i = 1; i < fds.size(); ++i) {
                const short re = fds[i].revents;
                if (re == 0) continue;
                // Pudo dejarse de vigilar en un aviso anterior de esta misma vuelta.
                auto it = vigilados_.find(cuales[i]);
                if (it == vigilados_.end()) continue;
                int ev = 0;
                if (re & (POLLIN | POLLHUP | POLLERR)) ev |= kLeer;
                if (re & POLLOUT) ev |= kEscribir;
                if (re & (POLLHUP | POLLERR)) ev |= it->second.eventos & kEscribir;
                const std::shared_ptr<Aviso> aviso = it->second.aviso; // por si se deja adentro
                if (ev != 0 && aviso && *aviso) (*aviso)(ev);
            }
        }
        std::deque<std::function<void()>> listas;
        std::vector<std::function<void()>> vencidas;
        {
            std::lock_guard<std::mutex> l(m_);
            listas.swap(tareas_);
            const auto ahora = std::chrono::steady_clock::now();
            while (!luego_.empty() && luego_.begin()->first <= ahora) {
                vencidas.push_back(std::move(luego_.begin()->second));
                luego_.erase(luego_.begin());
            }
        }
        for (auto& t : listas) t();
        for (auto& t : vencidas) t();
    }
    // Lo que quedaba listo (p. ej. un enHiloEsperando que llegó tarde).
    std::deque<std::function<void()>> listas;
    {
        std::lock_guard<std::mutex> l(m_);
        listas.swap(tareas_);
        luego_.clear();
    }
    for (auto& t : listas) t();
}

} // namespace ba::gateway::turn
