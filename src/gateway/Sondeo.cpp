// SPDX-License-Identifier: GPL-2.0-or-later
// Sondeo.cpp — ver Sondeo.hpp
#include "Sondeo.hpp"

#include <algorithm>
#include <chrono>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ba::gateway {
namespace {

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kSinSock = INVALID_SOCKET;
void cerrarSock(Sock s) { closesocket(s); }
#else
using Sock = int;
constexpr Sock kSinSock = -1;
void cerrarSock(Sock s) { ::close(s); }
#endif

/// ¿El socket está listo (para escribir o leer) antes de `ms`?
bool listo(Sock s, bool escribir, int ms) {
    fd_set fs;
    FD_ZERO(&fs);
    FD_SET(s, &fs);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return ::select(static_cast<int>(s) + 1, escribir ? nullptr : &fs, escribir ? &fs : nullptr, nullptr, &tv) > 0;
}

} // namespace

bool contestaHttp(uint16_t puerto, const char* ruta, int topeMs) {
    #ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return true; // sin red no se puede saber: no se lo mata
#endif
    bool ok = false;
    const Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != kSinSock) {
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
#else
        ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        const int uno = 1; // un ba-web que cierra a mitad no tiene que voltear al gateway (SIGPIPE)
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &uno, sizeof uno);
#endif
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(puerto);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(topeMs);
        const auto queda = [&hasta]() {
            return static_cast<int>(std::max<long long>(
                0, std::chrono::duration_cast<std::chrono::milliseconds>(hasta - std::chrono::steady_clock::now()).count()));
        };
        ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
        if (listo(s, true, queda())) {
            const std::string pedido = std::string("GET ") + ruta + " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
#ifdef MSG_NOSIGNAL
            constexpr int kSinSenal = MSG_NOSIGNAL;
#else
            constexpr int kSinSenal = 0;
#endif
            if (::send(s, pedido.data(), static_cast<int>(pedido.size()), kSinSenal) == static_cast<int>(pedido.size())) {
                std::string r;
                char buf[256];
                while (r.size() < 12 && listo(s, false, queda())) {
                    const int n = static_cast<int>(::recv(s, buf, sizeof buf, 0));
                    if (n <= 0) break;
                    r.append(buf, static_cast<size_t>(n));
                }
                ok = r.rfind("HTTP/1.", 0) == 0 && r.size() >= 12 && r.compare(9, 3, "200") == 0;
            }
        }
        cerrarSock(s);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return ok;
}

} // namespace ba::gateway
