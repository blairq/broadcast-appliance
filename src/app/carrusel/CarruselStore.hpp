// ============================================================================
// src/app/carrusel/CarruselStore.hpp — la biblioteca de carruseles
//
// Un carrusel se arma UNA vez y se aplica donde haga falta, igual que un QR
// guardado o un grafico: por eso es una biblioteca y no algo que vive adentro
// de una escena. El mismo juego de laminas puede ir en la escena de apertura y
// en la del cierre.
//
// DONDE SE GUARDAN. En `Config/carruseles.json`, al lado del resto de lo que el
// operador arma. NO en la carpeta del evento: un carrusel de placas
// institucionales sirve para todos los eventos, y meterlo adentro de uno
// obligaria a copiarlo a mano para el siguiente.
//
// SE ESCRIBE ENTERO Y SE RENOMBRA. Igual que el padron: si se corta la luz
// mientras se guarda, el archivo que estaba sigue completo. Un carrusel a medio
// escribir es una tira de laminas que ya no apunta a ningun archivo.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "carrusel/Carrusel.hpp"

namespace ba::carrusel {

class Store {
   public:
    /// :param archivo: Config/carruseles.json (se crea la carpeta si falta).
    explicit Store(std::filesystem::path archivo);

    const std::vector<Carrusel>& todos() const { return todos_; }
    /// Por nombre, sin distinguir mayusculas: el operador lo escribe como sale.
    Carrusel* porNombre(const std::string& nombre);
    const Carrusel* porNombre(const std::string& nombre) const;

    /// Guarda uno nuevo o reemplaza el que tenga ese nombre. Devuelve false si
    /// no tiene nombre: un carrusel sin nombre no se puede volver a encontrar.
    bool guardar(const Carrusel& c);
    bool borrar(const std::string& nombre);

    /// Escribe la biblioteca al disco. Lo llaman `guardar` y `borrar`; esta
    /// aparte para poder tocar un carrusel y persistirlo.
    bool persistir();
    /// Vuelve a leer del disco, descartando lo que haya en memoria.
    void releer();

    const std::string& error() const { return error_; }

   private:
    std::filesystem::path archivo_;
    std::vector<Carrusel> todos_;
    std::string error_;
};

} // namespace ba::carrusel
