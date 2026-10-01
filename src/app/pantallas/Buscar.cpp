// ============================================================================
// src/app/pantallas/Buscar.cpp — ver Buscar.hpp
// ============================================================================
#include "pantallas/Buscar.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

#if defined(BA_HAVE_MDNS)
#include <microdns/microdns.h>
#include <microdns/rr.h>
#endif

namespace ba::app::pantallas {

#if !defined(BA_HAVE_MDNS)

bool sePuedeBuscar() { return false; }

std::vector<Encontrado> buscar(int, std::string* error) {
    if (error != nullptr)
        *error = "Este programa se compiló sin búsqueda en la red (mDNS): agregá el televisor a mano "
                 "poniendo su dirección IP.";
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

} // namespace

bool sePuedeBuscar() { return true; }

std::vector<Encontrado> buscar(int segundos, std::string* error) {
    mdns_ctx* ctx = nullptr;
    if (mdns_init(&ctx, MDNS_ADDR_IPV4, MDNS_PORT) < 0) {
        if (error != nullptr)
            *error = "No pude abrir el mDNS para buscar en la red (¿hay otro programa usando el puerto "
                     "5353, o el firewall lo bloquea?).";
        return {};
    }
    Busqueda b;
    b.vence = ahoraMs() + static_cast<std::int64_t>(segundos) * 1000;
    const char* nombres[] = {kServicio};
    // `interval` 1: repregunta cada segundo, porque una sola pregunta se puede
    // perder (es UDP a multicast) y un televisor dormido tarda en contestar.
    const int r = mdns_listen(ctx, nombres, 1, RR_PTR, 1, parar, recibir, &b);
    mdns_destroy(ctx);
    if (r < 0 && b.porServicio.empty()) {
        char buf[128] = {0};
        mdns_strerror(r, buf, sizeof(buf));
        if (error != nullptr) *error = std::string("La búsqueda en la red falló: ") + buf;
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
