// ============================================================================
// src/app/emision/IndiceCanales.hpp — los canales de la mezcla de la foto, por su nombre
//
// FotoMotor::MezclaAudio::canal (y volumen, mudo) recorre la lista de canales: llamado por
// cada canal (la matriz, las tiras del mezclador) es O(N²) con cada foto. Esto los busca en
// O(1): el índice se rearma solo cuando la foto trae OTROS canales (otro puntero en su
// Compartido; si la parte Mezcla cambió pero los canales no, sirve el de antes) y guarda
// los que indexó, así los punteros no se van mientras tanto.
//
// Hilos: el de quien lo tiene (la interfaz, con la foto de la ventanilla).
// ============================================================================
#pragma once

#include "emision/FotoMotor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ba::app {

/// Los canales de `ahora` que no son iguales al de su MISMO lugar en `antes` (los índices,
/// en orden): lo que hay que mirar de una foto a la otra. Sin copiar nada; con los mismos
/// canales (el mismo puntero de la foto), ninguno sin comparar.
inline std::vector<std::size_t> canalesCambiados(const Compartido<std::vector<FotoMotor::CanalAudio>>& antes,
                                                 const Compartido<std::vector<FotoMotor::CanalAudio>>& ahora) {
    std::vector<std::size_t> out;
    if (antes.mismo(ahora)) return out;
    const std::vector<FotoMotor::CanalAudio>& a = antes.get();
    const std::vector<FotoMotor::CanalAudio>& n = ahora.get();
    for (std::size_t i = 0; i < n.size(); ++i)
        if (i >= a.size() || !(a[i] == n[i])) out.push_back(i);
    return out;
}

/// ¿Cambiaron los nombres (o la cantidad) de los canales? (Las filas de una lista por canal.)
inline bool otrosNombres(const Compartido<std::vector<FotoMotor::CanalAudio>>& antes,
                         const Compartido<std::vector<FotoMotor::CanalAudio>>& ahora) {
    if (antes.mismo(ahora)) return false;
    const std::vector<FotoMotor::CanalAudio>& a = antes.get();
    const std::vector<FotoMotor::CanalAudio>& n = ahora.get();
    if (a.size() != n.size()) return true;
    for (std::size_t i = 0; i < n.size(); ++i)
        if (a[i].nombre != n[i].nombre) return true;
    return false;
}

class IndiceCanales {
   public:
    using Canal = FotoMotor::CanalAudio;

    /// El canal `nombre` de `m` (nullptr si no está). Con nombres repetidos, el primero
    /// (como MezclaAudio::canal).
    const Canal* canal(const FotoMotor::MezclaAudio& m, const std::string& nombre) {
        if (!de_.mismo(m.canales)) rearmar(m);
        const auto it = idx_.find(nombre);
        return it != idx_.end() ? it->second : nullptr;
    }
    /// Como MezclaAudio::volumen y mudo, sin recorrer la lista.
    float volumen(const FotoMotor::MezclaAudio& m, const std::string& nombre, const std::string& salida = {}) {
        const Canal* c = canal(m, nombre);
        if (c == nullptr) return 1.0f;
        const auto it = c->volumen.find(m.salida(salida));
        return it != c->volumen.end() ? it->second : 1.0f;
    }
    bool mudo(const FotoMotor::MezclaAudio& m, const std::string& nombre, const std::string& salida = {}) {
        const Canal* c = canal(m, nombre);
        if (c == nullptr) return false;
        const auto it = c->mudo.find(m.salida(salida));
        return it != c->mudo.end() && it->second;
    }
    /// ¿Es el índice de estos canales (no hace falta rearmarlo)?
    bool de(const FotoMotor::MezclaAudio& m) const { return de_.mismo(m.canales); }
    /// Cuántas veces se rearmó (para el test: una por foto con otros canales, no una por canal).
    std::uint64_t rearmados() const { return rearmados_; }

   private:
    void rearmar(const FotoMotor::MezclaAudio& m) {
        de_ = m.canales;
        idx_.clear();
        idx_.reserve(de_.size());
        for (const Canal& c : de_) idx_.emplace(c.nombre, &c);
        ++rearmados_;
    }

    Compartido<std::vector<Canal>> de_; ///< los indexados (vivos mientras se usan los punteros)
    std::unordered_map<std::string, const Canal*> idx_;
    std::uint64_t rearmados_ = 0;
};

} // namespace ba::app
