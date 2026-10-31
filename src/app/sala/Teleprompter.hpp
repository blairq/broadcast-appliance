// ============================================================================
// src/app/sala/Teleprompter.hpp — el teleprompter de la sala (modelo)
//
// El dueño del estado es el gateway (src/gateway/Prompter.hpp): acá se le
// pregunta y se le mandan las órdenes. Lo usan la página Teleprompter
// (PrompterViewModel, y por ella el MCP) y el cue «teleprompter» del guion
// (EventoViewModel), con las mismas reglas:
//
//   - un discurso se carga por su ruta en Discursos/ del evento y con esa ruta
//     de título: así la lista sabe cuál está cargado, y el operador ve
//     "Apertura/intendente.txt" y no solo "intendente.txt";
//   - cargado queda LISTO pero parado, salvo que se pida arrancar (el orador
//     todavía está caminando hacia el atril);
//   - velocidad de 40 a 400 palabras por minuto, tamaño de 1 a 10.
//
// HAY VARIOS (gateway/Prompter.hpp: Prompters), cada uno con su texto y su
// página. Un Teleprompter habla con UNO: el principal, o el que da `para(id)`.
// Los nombres que les pone el operador están en ListaPrompters.
//
// Nada se guarda acá de antemano: se manda y el estado vuelve con el cambio.
// ============================================================================
#pragma once

#include <string>

#include "sala/Pedido.hpp"

namespace ba::app {
class Discursos;
}

namespace ba::app::sala {

class Teleprompter {
   public:
    /// `id`: cuál ("" o "principal" = el de siempre, /prompter/).
    explicit Teleprompter(ConferenceClient& sala, std::string id = {});
    /// El mismo modelo, hablándole a otro teleprompter.
    Teleprompter para(const std::string& id) const { return Teleprompter(sala_, id); }
    const std::string& id() const { return id_; }

    /// Los que tiene el gateway: {prompters: [{id, titulo, palabras, corriendo, oradores, controles}]}.
    void lista(Listo listo);
    /// Lo saca del gateway (el principal no).
    void quitar(Listo listo = {});

    /// El estado (titulo, texto, palabras, posicion, ppm, corriendo, clientes, clave…).
    void ver(Listo listo);
    /// Carga el discurso `rel` de `docs`. Devuelve el error inmediato (no se
    /// pudo leer) o "".
    std::string cargarDiscurso(const Discursos& docs, const std::string& rel, bool arrancar = false, Listo listo = {});
    /// Carga un texto que no está en disco (el editor, para probar), con su título.
    void cargarTexto(const std::string& titulo, const std::string& texto, Listo listo = {});
    void arrancar(bool si, Listo listo = {});
    void setPpm(int ppm, Listo listo = {});
    void setTamano(int tamano, Listo listo = {});
    void setEspejo(bool si, Listo listo = {});
    /// Salta a la palabra `posicion`.
    void irA(double posicion, Listo listo = {});
    /// Al principio del párrafo `n` (0 = el primero).
    void irAParrafo(int n, Listo listo = {});
    /// Un aviso al orador ("Redondeá"); vacío lo saca.
    void avisar(const std::string& texto, Listo listo = {});
    /// Para en seco donde esté, sin mover la posición.
    void panico(Listo listo = {});
    /// Lo del presentador de la línea de tiempo ({alAire: {nombre, desdeMs, hastaMs?},
    /// proximo?, hold}; null = no corre): la vista del orador lo muestra arriba. Con
    /// `para("*")` va a todos los atriles.
    void presentador(const nlohmann::json& p, Listo listo = {});

   private:
    void mandar(const nlohmann::json& orden, Listo listo);

    ConferenceClient& sala_;
    std::string id_;
};

} // namespace ba::app::sala
