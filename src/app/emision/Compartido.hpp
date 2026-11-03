// ============================================================================
// src/app/emision/Compartido.hpp — un campo de la foto que se comparte entre fotos
//
// La foto del motor (emision/FotoMotor.hpp) se publica entera cada vez que algo cambia
// (casi siempre: lo que sale, el estado). Lo que crece con los invitados (las escenas y sus
// fuentes, los canales de la mezcla, la vista del video, los retornos…) no se copia con
// ella: cada uno vive en su `Compartido<T>`, un puntero compartido a un valor que NADIE
// cambia después de publicado. Copiar la foto copia punteros; la foto nueva y la anterior
// comparten lo que no se rearmó (compartición estructural).
//
// PARA LEER es como el valor: `foto.escenas.size()`, `for (const auto& e : foto.escenas)`,
// `foto.video.value("x", …)`, `foto.fuentesVideo.find(n)`, pasarlo a quien espera un
// `const T&` (se convierte solo). Las comparaciones miran primero el puntero: dos fotos que
// comparten el campo son iguales sin recorrerlo.
//
// PARA ESCRIBIR (solo el dueño, sobre su foto de trabajo):
//   - asignarle un valor nuevo (`f.escenas = std::move(nuevas)`): si es IGUAL al que tenía,
//     se queda con el de antes (el mismo puntero: la publicación lo ve igual sin recorrerlo);
//   - o editarlo en el lugar (`f.video["x"] = …`, `push_back`, `editar()`): si el valor está
//     compartido con una foto publicada, se copia antes (copiar al escribir); si no, se
//     edita el mismo. Lo publicado no cambia nunca.
//
// Hilos: como la foto. El dueño escribe la suya de trabajo (la única que tiene un valor sin
// compartir: nadie más puede tomar una referencia nueva mientras tanto); las publicadas son
// constantes y se leen desde cualquier hilo.
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace ba::app {

template <class T>
class Compartido {
   public:
    using value_type = T;

    Compartido() : p_(vacio()) {}
    /// Con un valor inicial (`Compartido<json>::de(json::object())`): sin un constructor
    /// desde T, así `f.destinos = {…}` no duda entre asignar un T y construir otro Compartido.
    static Compartido de(T v) {
        Compartido c;
        c.p_ = std::make_shared<T>(std::move(v));
        return c;
    }
    Compartido(const Compartido&) = default;
    Compartido(Compartido&&) noexcept = default;
    Compartido& operator=(const Compartido&) = default;
    Compartido& operator=(Compartido&&) noexcept = default;

    /// Un valor nuevo: si es igual al que tenía, se queda con el de antes (el mismo puntero).
    Compartido& operator=(T v) {
        if (*p_ == v) return *this;
        p_ = std::make_shared<T>(std::move(v));
        return *this;
    }

    // --- Leer ------------------------------------------------------------------------
    const T& get() const { return *p_; }
    operator const T&() const { return *p_; }
    const T& operator*() const { return *p_; }
    const T* operator->() const { return p_.get(); }
    /// ¿Comparten el mismo valor (sin mirarlo)?
    bool mismo(const Compartido& o) const { return p_ == o.p_; }
    /// Cuántas fotos (y la de trabajo) comparten el valor: para el test.
    long usos() const { return p_.use_count(); }

    auto begin() const { return std::as_const(*p_).begin(); }
    auto end() const { return std::as_const(*p_).end(); }
    auto cbegin() const { return std::as_const(*p_).cbegin(); }
    auto cend() const { return std::as_const(*p_).cend(); }
    auto size() const { return p_->size(); }
    bool empty() const { return p_->empty(); }
    decltype(auto) front() const { return std::as_const(*p_).front(); }
    decltype(auto) back() const { return std::as_const(*p_).back(); }
    template <class K>
    decltype(auto) operator[](K&& k) const {
        return std::as_const(*p_)[std::forward<K>(k)];
    }
    template <class K>
    decltype(auto) at(K&& k) const {
        return std::as_const(*p_).at(std::forward<K>(k));
    }
    template <class K>
    auto find(K&& k) const {
        return std::as_const(*p_).find(std::forward<K>(k));
    }
    template <class K>
    auto count(K&& k) const {
        return p_->count(std::forward<K>(k));
    }
    template <class K>
    bool contains(K&& k) const {
        return p_->contains(std::forward<K>(k));
    }
    // Lo de nlohmann::json (solo se instancia si se usa).
    template <class... A>
    decltype(auto) value(A&&... a) const {
        return p_->value(std::forward<A>(a)...);
    }
    auto items() const { return std::as_const(*p_).items(); }
    template <class U>
    U get() const {
        return p_->template get<U>();
    }
    template <class... A>
    auto dump(A&&... a) const {
        return p_->dump(std::forward<A>(a)...);
    }
    bool is_object() const { return p_->is_object(); }
    bool is_array() const { return p_->is_array(); }
    bool is_null() const { return p_->is_null(); }
    bool is_string() const { return p_->is_string(); }
    bool is_number() const { return p_->is_number(); }
    bool is_boolean() const { return p_->is_boolean(); }

    // --- Escribir (el dueño, sobre su foto de trabajo) -------------------------------
    /// El valor para editarlo en el lugar: si está compartido, se copia antes.
    T& editar() {
        if (p_.use_count() != 1) p_ = std::make_shared<T>(*p_);
        return *p_;
    }
    template <class K>
    decltype(auto) operator[](K&& k) {
        return editar()[std::forward<K>(k)];
    }
    // (Sin plantilla: `f.escenas.push_back({…})` con llaves tiene que deducir el tipo.)
    void push_back(const typename T::value_type& v) { editar().push_back(v); }
    void push_back(typename T::value_type&& v) { editar().push_back(std::move(v)); }
    template <class... A>
    decltype(auto) emplace_back(A&&... a) {
        return editar().emplace_back(std::forward<A>(a)...);
    }

    friend bool operator==(const Compartido& a, const Compartido& b) { return a.p_ == b.p_ || *a.p_ == *b.p_; }
    friend bool operator==(const Compartido& a, const T& b) { return *a.p_ == b; }

   private:
    /// Todos los vacíos de un tipo comparten uno (una foto nueva no reserva nada).
    static const std::shared_ptr<T>& vacio() {
        static const std::shared_ptr<T> v = std::make_shared<T>();
        return v;
    }
    std::shared_ptr<T> p_;
};

/// Para nlohmann::json: un Compartido se escribe como su valor (`json{{"x", foto.sala.audio}}`).
template <class J, class T>
void to_json(J& j, const Compartido<T>& c) {
    j = c.get();
}

} // namespace ba::app
