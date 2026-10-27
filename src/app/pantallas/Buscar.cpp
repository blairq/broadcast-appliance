// ============================================================================
// src/app/pantallas/Buscar.cpp — ver Buscar.hpp
// ============================================================================
#include "pantallas/Buscar.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

#if defined(BA_HAVE_MDNS)
#include <microdns/microdns.h>
#include <microdns/rr.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QUdpSocket>
#endif

namespace ba::app::pantallas {

#if !defined(BA_HAVE_MDNS)

bool sePuedeBuscar() { return false; }

std::vector<Encontrado> buscar(int, std::string* error) {
    if (error != nullptr)
        *error = ba::i18n::traducir("pantallas", "Este programa se compiló sin búsqueda en la red (mDNS): agregá el televisor "
                                                 "a mano poniendo su dirección IP.");
    return {};
}

#else

namespace {

constexpr const char* kServicio = "_googlecast._tcp.local";

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Lo que se va juntando de cada aparato mientras llegan sus registros.
struct Parcial {
    std::string nombre, modelo, host, destino;
    int puerto = 0;
};

struct Busqueda {
    std::map<std::string, Parcial> porServicio;
    /// destino del SRV (la "máquina") -> IP, que llega en un registro aparte.
    std::map<std::string, std::string> ips;
    std::int64_t vence = 0;
};

/// El nombre del servicio sin el sufijo: "TV del living._googlecast._tcp.local"
/// -> "TV del living". Es el id, y también el nombre si no hay TXT `fn`.
std::string nombreCorto(const std::string& completo) {
    const std::string suf = std::string(".") + kServicio;
    if (completo.size() > suf.size() && completo.compare(completo.size() - suf.size(), suf.size(), suf) == 0)
        return completo.substr(0, completo.size() - suf.size());
    return completo;
}

bool parar(void* cookie) { return ahoraMs() > static_cast<Busqueda*>(cookie)->vence; }

void recibir(void* cookie, int status, const struct rr_entry* entries) {
    if (status < 0) return;
    auto* b = static_cast<Busqueda*>(cookie);
    for (const rr_entry* e = entries; e != nullptr; e = e->next) {
        if (e->name == nullptr) continue;
        const std::string nombre = e->name;
        switch (e->type) {
            case RR_SRV: {
                Parcial& p = b->porServicio[nombreCorto(nombre)];
                p.puerto = e->data.SRV.port;
                if (e->data.SRV.target != nullptr) p.destino = e->data.SRV.target;
                break;
            }
            case RR_TXT: {
                Parcial& p = b->porServicio[nombreCorto(nombre)];
                for (const rr_data_txt* t = e->data.TXT; t != nullptr; t = t->next) {
                    const std::string kv = t->txt;
                    // "fn=TV del living" (friendly name), "md=Chromecast" (modelo).
                    if (kv.rfind("fn=", 0) == 0)
                        p.nombre = kv.substr(3);
                    else if (kv.rfind("md=", 0) == 0)
                        p.modelo = kv.substr(3);
                }
                break;
            }
            case RR_A: b->ips[nombre] = e->data.A.addr_str; break;
            default: break; // el PTR no aporta nada que no traigan los otros
        }
    }
}

// --- Sin el puerto 5353: la pregunta «unicast heredada» (RFC 6762 §6.7) ----------
// libmicrodns escucha en el 5353, y en la Mac (y en una PC con otro programa de
// mDNS) ese puerto ya es del sistema: no se puede abrir. Preguntando desde un
// puerto cualquiera, cada aparato contesta DIRECTO a ese puerto, con todo lo que
// sabe (PTR, SRV, TXT y A). No necesita ningún puerto fijo ni permisos.

/// Un nombre de DNS desde `pos` (con punteros de compresión). Avanza `pos` si `avanzar`.
std::string leerNombre(const QByteArray& m, int& pos, int saltos = 0) {
    std::string out;
    int p = pos;
    bool salto = false;
    while (p < m.size() && saltos < 16) {
        const auto largo = static_cast<unsigned char>(m[p]);
        if (largo == 0) {
            ++p;
            break;
        }
        if ((largo & 0xC0) == 0xC0) {
            if (p + 1 >= m.size()) break;
            int destino = ((largo & 0x3F) << 8) | static_cast<unsigned char>(m[p + 1]);
            if (!salto) pos = p + 2;
            salto = true;
            const std::string resto = leerNombre(m, destino, saltos + 1);
            if (!resto.empty()) out += (out.empty() ? "" : ".") + resto;
            return out;
        }
        if (p + 1 + largo > m.size()) break;
        out += (out.empty() ? "" : ".") + m.mid(p + 1, largo).toStdString();
        p += 1 + largo;
    }
    if (!salto) pos = p;
    return out;
}

int leer16(const QByteArray& m, int p) {
    return (static_cast<unsigned char>(m[p]) << 8) | static_cast<unsigned char>(m[p + 1]);
}

void leerRespuesta(const QByteArray& m, Busqueda& b) {
    if (m.size() < 12) return;
    const int preguntas = leer16(m, 4);
    const int registros = leer16(m, 6) + leer16(m, 8) + leer16(m, 10);
    int pos = 12;
    for (int i = 0; i < preguntas && pos < m.size(); ++i) {
        leerNombre(m, pos);
        pos += 4;
    }
    for (int i = 0; i < registros && pos + 10 <= m.size(); ++i) {
        const std::string nombre = leerNombre(m, pos);
        if (pos + 10 > m.size()) return;
        const int tipo = leer16(m, pos);
        const int largo = leer16(m, pos + 8);
        const int datos = pos + 10;
        if (datos + largo > m.size()) return;
        switch (tipo) {
            case 33: { // SRV: prioridad, peso, puerto, destino
                if (largo < 7) break;
                Parcial& p = b.porServicio[nombreCorto(nombre)];
                p.puerto = leer16(m, datos + 4);
                int d = datos + 6;
                p.destino = leerNombre(m, d);
                break;
            }
            case 16: { // TXT: cadenas con su largo adelante
                Parcial& p = b.porServicio[nombreCorto(nombre)];
                for (int q = datos; q < datos + largo;) {
                    const int l = static_cast<unsigned char>(m[q]);
                    const std::string kv = m.mid(q + 1, l).toStdString();
                    if (kv.rfind("fn=", 0) == 0) p.nombre = kv.substr(3);
                    else if (kv.rfind("md=", 0) == 0) p.modelo = kv.substr(3);
                    q += 1 + l;
                }
                break;
            }
            case 1: // A
                if (largo == 4)
                    b.ips[nombre] = QHostAddress(static_cast<quint32>((static_cast<unsigned char>(m[datos]) << 24) |
                                                                      (static_cast<unsigned char>(m[datos + 1]) << 16) |
                                                                      (static_cast<unsigned char>(m[datos + 2]) << 8) |
                                                                      static_cast<unsigned char>(m[datos + 3])))
                                        .toString()
                                        .toStdString();
                break;
            default: break;
        }
        pos = datos + largo;
    }
}

bool buscarUnicast(int segundos, Busqueda& b) {
    QUdpSocket s;
    if (!s.bind(QHostAddress::AnyIPv4, 0)) return false;
    // La pregunta: id ≠ 0 (así se reconoce la unicast heredada), una pregunta PTR.
    QByteArray q;
    const auto id = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 0xFFFF));
    q.append(static_cast<char>(id >> 8)).append(static_cast<char>(id & 0xFF));
    q.append(QByteArray("\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10));
    for (const char* parte : {"_googlecast", "_tcp", "local"}) {
        q.append(static_cast<char>(std::strlen(parte))).append(parte);
    }
    q.append(QByteArray("\x00\x00\x0c\x00\x01", 5)); // fin del nombre, PTR, IN
    const QHostAddress grupo("224.0.0.251");
    QElapsedTimer t;
    t.start();
    qint64 ultimaPregunta = -1000;
    while (t.elapsed() < segundos * 1000) {
        // Repregunta cada segundo: es UDP, y un televisor dormido tarda en contestar.
        if (t.elapsed() - ultimaPregunta >= 1000) {
            s.writeDatagram(q, grupo, 5353);
            ultimaPregunta = t.elapsed();
        }
        if (s.waitForReadyRead(200))
            while (s.hasPendingDatagrams()) {
                QByteArray r(static_cast<int>(s.pendingDatagramSize()), '\0');
                s.readDatagram(r.data(), r.size());
                leerRespuesta(r, b);
            }
    }
    return true;
}

} // namespace

bool sePuedeBuscar() { return true; }

std::vector<Encontrado> buscar(int segundos, std::string* error) {
    Busqueda b;
    b.vence = ahoraMs() + static_cast<std::int64_t>(segundos) * 1000;
    int r = 0;
    mdns_ctx* ctx = nullptr;
    if (mdns_init(&ctx, MDNS_ADDR_IPV4, MDNS_PORT) < 0) {
        // El 5353 está tomado (en la Mac, siempre: es del sistema): la pregunta unicast heredada.
        if (!buscarUnicast(segundos, b)) {
            if (error != nullptr)
                *error = ba::i18n::traducir("pantallas", "No pude buscar en la red (¿el firewall bloquea el mDNS?): agregá el "
                                                         "televisor a mano con su dirección IP.");
            return {};
        }
    } else {
        const char* nombres[] = {kServicio};
        // `interval` 1: repregunta cada segundo, porque una sola pregunta se puede
        // perder (es UDP a multicast) y un televisor dormido tarda en contestar.
        r = mdns_listen(ctx, nombres, 1, RR_PTR, 1, parar, recibir, &b);
        mdns_destroy(ctx);
    }
    if (r < 0 && b.porServicio.empty()) {
        char buf[128] = {0};
        mdns_strerror(r, buf, sizeof(buf));
        if (error != nullptr) *error = ba::i18n::llenar(ba::i18n::traducir("pantallas", "La búsqueda en la red falló: %1"), {buf});
        return {};
    }

    std::vector<Encontrado> out;
    for (auto& [id, p] : b.porServicio) {
        Encontrado e;
        e.id = id;
        e.nombre = p.nombre.empty() ? id : p.nombre;
        e.modelo = p.modelo;
        e.puerto = p.puerto;
        // La IP llega en un registro aparte, a nombre de la máquina.
        const auto it = b.ips.find(p.destino);
        e.host = it == b.ips.end() ? std::string() : it->second;
        // Sin dirección o sin puerto no se le puede hablar: mejor no mostrarlo
        // que mostrar algo que no va a andar.
        if (e.host.empty() || e.puerto <= 0) continue;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const Encontrado& a, const Encontrado& c) { return a.nombre < c.nombre; });
    return out;
}

#endif

} // namespace ba::app::pantallas
