// ============================================================================
// src/app/emision/PedidosSalida.hpp — lo PEDIDO de cada salida (un solo dueño)
//
// Qué pidió alguien (el operador, el MCP, la línea, el corte de luz) que salga de cada
// salida: si graba, si transmite su principal, qué destinos de destinos.json. Es lo que
// guarda el documento de aire y lo que vuelve tras un corte. Lo REAL (lo que de verdad
// sale) lo dice el motor y lo muestra la foto; acá no se adivina.
//
// Grabar y transmitir son órdenes que corren en el hilo de salidas del motor: pedirlas
// deja la salida «Pendiente» con el número de la orden, y el resultado que manda el motor
// (BroadcastEngine::ResultadoOrden) la deja en «Si» o, si falló, en «No» (ya no está
// pedida: el documento deja de decir que graba y tras un corte no se retoma algo que nunca
// grabó). Solo cuenta el resultado de la ÚLTIMA orden: uno viejo (se paró y se volvió a
// pedir) no pisa lo de ahora. La por omisión es una salida más (sin caso aparte).
//
// Lo usa AppRuntime (su dueño). Seguro entre hilos: lo leen el vigía de las alarmas y el
// hilo de salidas (el resultado). Sin Qt.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ba::app::salidas {

class Pedidos {
   public:
    enum class Estado { No, Pendiente, Si }; ///< Pendiente: encolada, el motor todavía no dijo
    struct DeSalida {
        Estado graba = Estado::No, transmite = Estado::No;
        std::uint64_t ordenGraba = 0, ordenTransmite = 0; ///< la última orden: solo su resultado cuenta
        std::set<std::string> destinos; ///< los de destinos.json que salen con ella
        bool cerradaPorEnergia = false;  ///< la cerró el corte de luz: sigue pedida (vuelve con la energía)
        bool grabaPedida() const { return graba != Estado::No || cerradaPorEnergia; }
        bool transmitePedida() const { return transmite != Estado::No; }
    };

    /// Una copia de lo pedido de `id` (nada pedido si no hay).
    DeSalida de(const std::string& id) const {
        const std::lock_guard<std::mutex> l(mu_);
        const auto it = salidas_.find(id);
        return it == salidas_.end() ? DeSalida{} : it->second;
    }
    /// Pide grabar (o transmitir) en `id`: queda Pendiente. Devuelve el número de la orden.
    std::uint64_t pedir(const std::string& id, bool grabar) {
        const std::lock_guard<std::mutex> l(mu_);
        DeSalida& p = salidas_[id];
        const std::uint64_t n = ++proxima_;
        (grabar ? p.graba : p.transmite) = Estado::Pendiente;
        (grabar ? p.ordenGraba : p.ordenTransmite) = n;
        return n;
    }
    /// Ya no se pide (lo pararon, o la orden ni se pudo encolar).
    void dejarDePedir(const std::string& id, bool grabar) {
        const std::lock_guard<std::mutex> l(mu_);
        const auto it = salidas_.find(id);
        if (it == salidas_.end()) return;
        (grabar ? it->second.graba : it->second.transmite) = Estado::No;
        (grabar ? it->second.ordenGraba : it->second.ordenTransmite) = 0;
    }
    /// Lo que dijo el motor de la orden `orden`. true si cambió algo (era la última orden y
    /// estaba pendiente): Si, o No si falló.
    bool resultado(const std::string& id, bool grabar, std::uint64_t orden, bool ok) {
        const std::lock_guard<std::mutex> l(mu_);
        const auto it = salidas_.find(id);
        if (it == salidas_.end()) return false;
        Estado& e = grabar ? it->second.graba : it->second.transmite;
        if ((grabar ? it->second.ordenGraba : it->second.ordenTransmite) != orden || e != Estado::Pendiente) return false;
        e = ok ? Estado::Si : Estado::No;
        return true;
    }
    /// El destino `destino` sale con la salida `id` (en ninguna otra).
    void ponerDestino(const std::string& id, const std::string& destino) {
        const std::lock_guard<std::mutex> l(mu_);
        for (auto& [sid, p] : salidas_) p.destinos.erase(destino);
        salidas_[id].destinos.insert(destino);
    }
    /// El destino ya no sale ("todos": ninguno).
    void sacarDestino(const std::string& destino) {
        const std::lock_guard<std::mutex> l(mu_);
        for (auto& [sid, p] : salidas_)
            if (destino == "todos")
                p.destinos.clear();
            else
                p.destinos.erase(destino);
    }
    /// El corte de luz cerró su grabación: sigue pedida hasta que vuelva la energía.
    void cerrarPorEnergia(const std::string& id) {
        const std::lock_guard<std::mutex> l(mu_);
        salidas_[id].cerradaPorEnergia = true;
    }
    /// Las que cerró el corte de luz (y dejan de estarlo: se vuelven a pedir).
    std::vector<std::string> tomarCerradasPorEnergia() {
        const std::lock_guard<std::mutex> l(mu_);
        std::vector<std::string> out;
        for (auto& [id, p] : salidas_)
            if (p.cerradaPorEnergia) {
                p.cerradaPorEnergia = false;
                out.push_back(id);
            }
        return out;
    }
    /// La salida se borró: lo suyo no vuelve.
    void olvidar(const std::string& id) {
        const std::lock_guard<std::mutex> l(mu_);
        salidas_.erase(id);
    }
    /// Cierre ordenado (o el rearme, que vuelve a pedir lo que había): nada pedido.
    void olvidarTodo() {
        const std::lock_guard<std::mutex> l(mu_);
        salidas_.clear();
    }

   private:
    mutable std::mutex mu_;
    std::map<std::string, DeSalida> salidas_;
    std::uint64_t proxima_ = 0;
};

} // namespace ba::app::salidas
