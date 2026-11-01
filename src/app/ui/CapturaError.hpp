#pragma once
// CapturaError.hpp — el error de una acción, para quien la llama desde C++.
//
//   Las acciones de los modelos (las Q_INVOKABLE void o bool) avisan su error por la señal
//   del modelo (`notify`/`aviso`): la vista lo muestra. Un solo contrato. Quien llama desde
//   C++ y necesita el texto (el MCP lo devuelve al cliente, las pruebas lo miran) lo captura
//   mientras dura la llamada:
//
//       const QString e = errorDe([&] { pantallas->fijar(id, true); });
//
//   Mientras hay una captura en este hilo, el primer error que avisa una acción va a la
//   captura y no a la vista (quien llamó decide qué hacer con él). Solo lo sincrónico: lo
//   que falla después (en el fondo) llega por la señal como siempre.

#include <QString>

#include <utility>

namespace ba::app {

class CapturaError {
public:
    CapturaError() : previa_(actual()) { actual() = this; }
    ~CapturaError() { actual() = previa_; }
    CapturaError(const CapturaError&) = delete;
    CapturaError& operator=(const CapturaError&) = delete;

    const QString& error() const { return error_; }

    /// Lo llama el aviso de error de cada modelo: true si había una captura y se lo quedó
    /// (entonces no se avisa a la vista).
    static bool tomar(const QString& e) {
        CapturaError* c = actual();
        if (c == nullptr) return false;
        if (c->error_.isEmpty()) c->error_ = e;
        return true;
    }

private:
    static CapturaError*& actual() {
        thread_local CapturaError* c = nullptr;
        return c;
    }
    CapturaError* previa_;
    QString error_;
};

/// Corre la acción y devuelve su error ("" si anduvo).
template <class F>
QString errorDe(F&& accion) {
    static_assert(std::is_void_v<decltype(accion())> || std::is_same_v<decltype(accion()), bool>, "PRUEBA");
    CapturaError c;
    std::forward<F>(accion)();
    return c.error();
}

} // namespace ba::app
