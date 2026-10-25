// ============================================================================
// src/app/dispositivos/Conocidos.hpp — los dispositivos de audio y video que la
// máquina ya vio, con el NOMBRE que les puso el operador
//
// «La cámara del fondo», «Mic del atril», «Placa de la consola»: el operador
// no piensa en «FaceTime HD Camera (Built-in)» ni en un UID de Core Audio. El
// nombre es de la MÁQUINA, no del evento (la cámara sigue enchufada al mismo
// lugar cuando se abre otro evento), así que vive en
// <datos de la máquina>/Config/dispositivos.json, al lado de control-sistema.json,
// y NO en la carpeta de un evento.
//
// La clave es el id estable del catálogo (Dispositivos: «local:video:<uniqueID>»,
// «local:audio:<UID>», «local:salida:<id>», «ndi:<nombre>», «red:<ip>»), nunca el
// índice: el índice cambia al reenchufar y el nombre quedaría pegado a otro equipo.
//
// Recordarlos también sirve para lo contrario: un equipo que no está al
// arrancar (se desenchufó, se cortó la luz y la capturadora tarda) se sigue
// mostrando como AUSENTE con su nombre, en vez de desaparecer de la lista.
//
// Sin Qt a propósito (solo std y nlohmann): se prueba solo (test_dispositivos).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ba::app::dispositivos {

/// Los tipos que se recuerdan: los de audio y video que usan las entradas, el
/// mezclador y las salidas. Lo USB/HID/serie y los equipos sueltos de la red no:
/// aparecen cuando se busca y nada más.
bool seRecuerda(const std::string& tipo);

struct Conocido {
    std::string id;      ///< el del catálogo (estable)
    std::string tipo;    ///< camara_local | microfono | salida_audio | ndi | camara_red
    std::string tecnico; ///< el nombre que da el sistema (el último que se vio)
    std::string valor;   ///< lo que usa una entrada: id de la cámara, del micrófono, nombre NDI, IP
    std::string nombre;  ///< el del operador ("" = todavía no le puso)
    nlohmann::json config = nlohmann::json::object(); ///< {nota, modo…}
    std::string desde;   ///< cuándo se lo vio por primera vez (ISO)
    const std::string& mostrar() const { return nombre.empty() ? tecnico : nombre; }
    nlohmann::json comoJson() const;
};

class Conocidos {
   public:
    /// `archivo` vacío: en memoria (los tests, o una app sin carpeta de datos).
    explicit Conocidos(std::filesystem::path archivo = {});

    void leer();
    /// Anota que se lo vio. Guarda solo si es nuevo o cambió su nombre técnico o
    /// su valor (no se reescribe el archivo en cada búsqueda). true si guardó.
    bool visto(const std::string& id, const std::string& tipo, const std::string& tecnico,
               const std::string& valor, const std::string& cuando);
    /// "" o el error. Nombre vacío: vuelve al del sistema. Dos equipos no pueden
    /// llamarse igual (las entradas los eligen por nombre).
    std::string renombrar(const std::string& id, const std::string& nombre);
    /// Mezcla `config` en la suya; una clave con null se borra. "" o el error.
    std::string configurar(const std::string& id, const nlohmann::json& config);
    /// Se olvida de un equipo (su nombre y su configuración). "" o el error.
    std::string olvidar(const std::string& id);

    bool hay(const std::string& id) const;
    Conocido uno(const std::string& id) const;
    /// El conocido de ese tipo con ese valor (p. ej. el micrófono cuyo UID es X); id vacío si no hay.
    Conocido porValor(const std::string& tipo, const std::string& valor) const;
    std::vector<Conocido> todos() const;
    /// El nombre para mostrar: el del operador, o `tecnico` si no le puso.
    std::string nombreDe(const std::string& id, const std::string& tecnico) const;

    /// Los de la máquina, para quien muestra un dispositivo sin pasar por el
    /// catálogo (el mezclador, al listar las salidas). Lo fija Dispositivos.
    static void setDeLaMaquina(Conocidos* c);
    static std::string nombreParaMostrar(const std::string& id, const std::string& tecnico);

   private:
    bool guardar() const; // con mu_ tomado
    std::filesystem::path archivo_;
    mutable std::mutex mu_;
    std::map<std::string, Conocido> todos_;
};

} // namespace ba::app::dispositivos
