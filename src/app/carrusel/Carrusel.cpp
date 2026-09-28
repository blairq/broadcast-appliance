// ============================================================================
// src/app/carrusel/Carrusel.cpp — ver Carrusel.hpp
// ============================================================================
#include "carrusel/Carrusel.hpp"

#include <algorithm>

namespace ba::carrusel {
namespace {

using nlohmann::json;

std::string textoDe(const json& j, const char* clave) {
    if (!j.contains(clave) || !j[clave].is_string()) return {};
    return j[clave].get<std::string>();
}

} // namespace

const Lamina* Carrusel::lamina() const {
    if (laminas_.empty()) return nullptr;
    const auto i = static_cast<std::size_t>(actual_);
    return i < laminas_.size() ? &laminas_[i] : nullptr;
}

bool Carrusel::agregar(Lamina l) {
    if (l.archivo.empty() || laminas_.size() >= kMax) return false;
    // La misma imagen dos veces en la tira es casi siempre un descuido al
    // armarla (se arrastro la carpeta dos veces), y despues al operador le
    // aparecen dos tarjetas identicas y no sabe cual tocar.
    const bool ya = std::any_of(laminas_.begin(), laminas_.end(),
                                [&l](const Lamina& o) { return o.archivo == l.archivo; });
    if (ya) return false;
    laminas_.push_back(std::move(l));
    return true;
}

bool Carrusel::quitar(std::size_t i) {
    if (i >= laminas_.size()) return false;
    laminas_.erase(laminas_.begin() + static_cast<std::ptrdiff_t>(i));
    if (laminas_.empty()) {
        actual_ = 0;
        return true;
    }
    // Si se saco la que estaba al aire, queda parado en la que ocupo su lugar
    // —no en la anterior—: es lo que el operador espera cuando saca una del
    // medio, seguir hacia adelante.
    if (actual_ >= static_cast<int>(laminas_.size())) actual_ = static_cast<int>(laminas_.size()) - 1;
    return true;
}

bool Carrusel::mover(std::size_t de, std::size_t a) {
    if (de >= laminas_.size() || a >= laminas_.size() || de == a) return false;
    // La que estaba al aire SIGUE al aire aunque se haya movido de lugar:
    // reordenar la tira no es cambiar de lamina.
    const std::string enAire = lamina() != nullptr ? lamina()->archivo : std::string();
    Lamina l = laminas_[de];
    laminas_.erase(laminas_.begin() + static_cast<std::ptrdiff_t>(de));
    laminas_.insert(laminas_.begin() + static_cast<std::ptrdiff_t>(a), std::move(l));
    if (!enAire.empty())
        for (std::size_t i = 0; i < laminas_.size(); ++i)
            if (laminas_[i].archivo == enAire) { actual_ = static_cast<int>(i); break; }
    return true;
}

void Carrusel::vaciar() {
    laminas_.clear();
    actual_ = 0;
}

bool Carrusel::ir(int i) {
    if (laminas_.empty() || i < 0 || i >= static_cast<int>(laminas_.size())) return false;
    actual_ = i;
    return true;
}

void Carrusel::siguiente() {
    if (laminas_.empty()) return;
    // Da la vuelta: en un bucle de fondo que corre toda la tarde, frenar en la
    // ultima seria dejar la pantalla clavada sin que nadie se entere.
    actual_ = (actual_ + 1) % static_cast<int>(laminas_.size());
}

void Carrusel::anterior() {
    if (laminas_.empty()) return;
    const int n = static_cast<int>(laminas_.size());
    actual_ = (actual_ - 1 + n) % n;
}

void Carrusel::setAvanceMs(int ms) {
    // Menos de medio segundo no es un carrusel, es un parpadeo; y un negativo
    // seria un temporizador que no para nunca.
    avanceMs_ = ms <= 0 ? 0 : std::max(500, ms);
}

bool Carrusel::etiquetar(const std::string& archivo, const std::string& etiquetas) {
    for (Lamina& l : laminas_)
        if (l.archivo == archivo) {
            l.etiquetas = etiquetas;
            // Marcada AUNQUE las etiquetas salgan vacias: si no, una imagen que
            // no dio nada se reintenta para siempre, y son llamadas a un modelo.
            l.analizada = true;
            return true;
        }
    return false;
}

const Lamina* Carrusel::proximaSinAnalizar() const {
    for (const Lamina& l : laminas_)
        if (!l.analizada) return &l;
    return nullptr;
}

json Carrusel::aJson() const {
    json ls = json::array();
    for (const Lamina& l : laminas_)
        ls.push_back(json{{"archivo", l.archivo},
                          {"titulo", l.titulo},
                          {"etiquetas", l.etiquetas},
                          {"analizada", l.analizada}});
    return json{{"nombre", nombre_},
                {"fuente", fuente_},
                {"avanceMs", avanceMs_},
                {"actual", actual_},
                {"laminas", ls}};
}

Carrusel Carrusel::deJson(const json& j) {
    Carrusel c;
    if (!j.is_object()) return c;
    c.nombre_ = textoDe(j, "nombre");
    c.fuente_ = textoDe(j, "fuente");
    if (j.contains("avanceMs") && j["avanceMs"].is_number_integer())
        c.setAvanceMs(j["avanceMs"].get<int>());
    if (j.contains("laminas") && j["laminas"].is_array())
        for (const json& jl : j["laminas"]) {
            if (!jl.is_object()) continue;
            Lamina l;
            l.archivo = textoDe(jl, "archivo");
            l.titulo = textoDe(jl, "titulo");
            l.etiquetas = textoDe(jl, "etiquetas");
            l.analizada = jl.contains("analizada") && jl["analizada"].is_boolean()
                              ? jl["analizada"].get<bool>()
                              : !l.etiquetas.empty();
            c.agregar(std::move(l));
        }
    // Se lee DESPUES de las laminas y se valida contra las que quedaron: un
    // `actual` guardado cuando la tira era mas larga apuntaria afuera.
    if (j.contains("actual") && j["actual"].is_number_integer()) c.ir(j["actual"].get<int>());
    return c;
}

} // namespace ba::carrusel
