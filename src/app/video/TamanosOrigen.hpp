// ============================================================================
// src/app/video/TamanosOrigen.hpp — los tamaños que LLEGAN de las entradas, al día por foto
//
// El tamaño que llega de cada entrada (gstreamer.origen de su estado, antes de los filtros)
// lo necesitan la superresolución (las variantes que faltan) y el MCP. Antes cada uno lo
// sacaba recorriendo TODAS las entradas y copiando el mapa entero de estados por cada una
// (VideoViewModel::estadoEntrada): O(N²) en cada foto del motor, con la interfaz trabada
// segundos con 200 invitados.
//
// Esto lo arma con cada foto mirando SOLO las entradas cuyo estado cambió (cada una se
// compara con la suya de la foto anterior, sin copiar nada) y dice si cambió la lista de
// tamaños distintos (lo único que le importa a quien escucha). Pura: se prueba sola
// (tests/test_trabajo_por_foto.cpp).
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::video {

class TamanosOrigen {
   public:
    using json = nlohmann::json;
    using Tamano = std::pair<int, int>;

    /// El tamaño que llega de una entrada (su estado); {0, 0} si no tiene.
    static Tamano deEstado(const json& st) {
        if (!st.is_object()) return {0, 0};
        const auto g = st.find("gstreamer");
        if (g == st.end() || !g->is_object()) return {0, 0};
        const auto o = g->find("origen");
        if (o == g->end() || !o->is_object()) return {0, 0};
        const auto w = o->find("ancho");
        const auto h = o->find("alto");
        if (w == o->end() || h == o->end() || !w->is_number() || !h->is_number()) return {0, 0};
        const int a = w->get<int>(), b = h->get<int>();
        return a > 0 && b > 0 ? Tamano{a, b} : Tamano{0, 0};
    }

    /// Los estados de la foto nueva (`ahora`: {entrada: estado}) contra los de la anterior
    /// (`antes`). `entradasCambiaron`: si cambió la lista de entradas (solo entonces se
    /// buscan las que se fueron). true si cambió la lista de tamaños distintos.
    bool releer(const json& ahora, const json& antes, bool entradasCambiaron) {
        recalculados_ = 0;
        bool cambio = false;
        if (ahora.is_object()) {
            for (auto it = ahora.begin(); it != ahora.end(); ++it) {
                if (antes.is_object()) {
                    const auto a = antes.find(it.key());
                    if (a != antes.end() && *a == *it) continue; // la misma: nada que recalcular
                }
                ++recalculados_;
                const Tamano t = deEstado(*it);
                const auto o = porEntrada_.find(it.key());
                if (t.first > 0) {
                    if (o == porEntrada_.end()) {
                        porEntrada_.emplace(it.key(), t);
                        cambio = true;
                    } else if (o->second != t) {
                        o->second = t;
                        cambio = true;
                    }
                } else if (o != porEntrada_.end()) {
                    porEntrada_.erase(o);
                    cambio = true;
                }
            }
        }
        // Las que se fueron (solo si cambió la lista de entradas o cuántas tienen estado).
        const std::size_t nAhora = ahora.is_object() ? ahora.size() : 0;
        const std::size_t nAntes = antes.is_object() ? antes.size() : 0;
        if (entradasCambiaron || nAhora != nAntes)
            for (auto o = porEntrada_.begin(); o != porEntrada_.end();) {
                if (!ahora.is_object() || !ahora.contains(o->first)) {
                    o = porEntrada_.erase(o);
                    cambio = true;
                } else {
                    ++o;
                }
            }
        if (!cambio) return false;
        std::vector<Tamano> distintos;
        for (const auto& [e, t] : porEntrada_)
            if (std::find(distintos.begin(), distintos.end(), t) == distintos.end()) distintos.push_back(t);
        if (distintos == distintos_) return false;
        distintos_ = std::move(distintos);
        return true;
    }

    /// Los tamaños distintos (ancho, alto) que llegan ahora.
    const std::vector<Tamano>& distintos() const { return distintos_; }
    /// Cuántos estados se recalcularon con la última foto (el test: con una que cambia, 1).
    int recalculados() const { return recalculados_; }

   private:
    std::map<std::string, Tamano> porEntrada_;
    std::vector<Tamano> distintos_;
    int recalculados_ = 0;
};

} // namespace ba::app::video
