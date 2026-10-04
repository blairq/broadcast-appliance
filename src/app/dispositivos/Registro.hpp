// src/app/dispositivos/Registro.hpp — enchufa los detectores y las acciones (ver Registro.cpp)
#pragma once

namespace ba::app {
class OperatorMessages;
namespace red {
class Red;
}
namespace plugins {
class Plugins;
}
namespace video {
class VideoGrafos;
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
    video::VideoGrafos* video = nullptr;
    ptz::ControlCamaras* camaras = nullptr;
    OperatorMessages* mensajes = nullptr;
};

void registrarTodo(Dispositivos& d, const Dependencias& dep);

} // namespace ba::app::dispositivos
