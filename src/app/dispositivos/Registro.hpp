// src/app/dispositivos/Registro.hpp — enchufa los detectores y las acciones (ver Registro.cpp)
#pragma once

namespace ba::app {
class AppRuntime;
class InputsViewModel;
class OperatorMessages;
class VideoViewModel;
namespace red {
class Red;
}
namespace plugins {
class Plugins;
}
namespace ptz {
class ControlCamaras;
}
} // namespace ba::app

namespace ba::app::dispositivos {

class Dispositivos;

struct Dependencias {
    red::Red* red = nullptr;
    plugins::Plugins* plugins = nullptr;
    VideoViewModel* video = nullptr; ///< el grafo de video, visto desde la interfaz
    ptz::ControlCamaras* camaras = nullptr;
    OperatorMessages* mensajes = nullptr;
    /// Para listar las cámaras, micrófonos y placas de la máquina (con el motor).
    AppRuntime* rt = nullptr;
    /// «Agregar como entrada» de un dispositivo de la máquina: lo hace la misma
    /// página de entradas (una sola manera de armar una entrada).
    InputsViewModel* entradas = nullptr;
};

void registrarTodo(Dispositivos& d, const Dependencias& dep);

} // namespace ba::app::dispositivos
