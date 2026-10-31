// ============================================================================
// src/app/sala/ConfigSala.hpp — la configuración de la sala en caliente (modelo)
//
// Cambiar un ajuste de la conferencia ya no la reinicia (F3-06): lo que el
// gateway sabe aplicar andando se le manda por su API (PUT
// /api/v1/config/<sección>) y rige enseguida, sin cortar a nadie. Solo lo
// estructural (puertos, TLS, túneles, tamaños de video, lugares reservados)
// pide reiniciarla, y entonces se avisa a cuántos se corta (`conectados`).
// gateway.json lo escribe la app igual (lo lee el gateway al volver a arrancar).
//
// Las secciones (claves, sin traducir):
//   ajustes  los avanzados de la sala de alcance «live» (ajustes/Registro.hpp):
//            {valores: {clave: valor}} con TODOS los que no están en su valor de
//            siempre (los «live» que no vienen vuelven al de siempre; los que
//            reinician la sala no se tocan);
//   sala     {lobby, pinIntentos, pinBloqueoS, maxUploadMB}.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace ba::app {
class ConferenceClient;
} // namespace ba::app

namespace ba::app::sala {

class ConfigSala {
   public:
    /// "" = rige; si no, por qué no (la sala no contesta, un valor fuera de rango…).
    using Listo = std::function<void(const std::string& error)>;

    explicit ConfigSala(ConferenceClient& sala);

    /// Manda `valores` a la sección. El resultado llega a `listo` (en el hilo de la interfaz).
    void aplicar(const std::string& seccion, const nlohmann::json& valores, Listo listo);
    /// Los que están en la sala ahora (invitados, en el lobby o al aire); -1 si no contesta.
    int conectados() const;

   private:
    ConferenceClient& sala_;
};

} // namespace ba::app::sala
