// ============================================================================
// src/app/ui/LectorDocumentos.hpp — leer un documento de identidad de una imagen
//
// docs/funciones/DOCUMENTOS.md. Junta las dos mitades, sin duplicar ninguna:
//   - LEER EL CÓDIGO de la imagen (documentos/lector/LectorCodigos, zxing-cpp),
//     que es lo que tarda: en un hilo propio, nunca en el de la interfaz (el MCP
//     también corre en ese hilo; ver ui/Reconocedor.hpp, mismo problema y mismo
//     reparto);
//   - TRADUCIRLO con los tipos de documento: primero los de fábrica
//     (documentos::Catalogo, el DNI argentino), en el acto; después los de los
//     plugins que corren (@plugin.documento), por su canal y con tope de espera
//     (plugins::Plugins::pedir). Un plugin colgado no traba nada: no contesta y
//     se sigue con el próximo.
//
// Lo usan el tótem (ui/TotemsViewModel, el paso «documento») y el MCP
// (`documento_leer`, `documento_tipos`). Uno solo, del tótem.
//
// SE PUEDEN DESCARTAR PEDIDOS, como en el reconocimiento: el tótem pide una
// mirada cada 300 ms; si una tarda más, encolarlas mostraría cuadros viejos.
// Los del MCP no se descartan: alguien espera esa respuesta.
//
// Los datos de un documento no van a los registros (a lo sumo
// documentos::paraLog) ni salen de la máquina.
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "documentos/Documento.hpp"

namespace ba::app {

namespace plugins {
class Plugins;
}

class LectorDocumentos : public QObject {
    Q_OBJECT
   public:
    struct Resultado {
        /// Lo que se vio en la imagen (o lo que se pasó para traducir).
        std::vector<documentos::Codigo> codigos;
        /// Si alguno era un documento de los aceptados: sus datos (documentos::Datos::aJson).
        bool leido = false;
        nlohmann::json datos;
        /// "" = salió bien (aunque no haya leído nada).
        QString error;
    };
    /// Se llama SIEMPRE en el hilo de la interfaz.
    using Respuesta = std::function<void(Resultado)>;

    explicit LectorDocumentos(plugins::Plugins* plugins, QObject* parent = nullptr);
    ~LectorDocumentos() override;

    /// Si esta compilación trae el lector de códigos (zxing-cpp).
    static bool disponible();

    /// Busca los códigos de `img` (en el hilo propio) y los traduce con los tipos
    /// `aceptados` (vacío = todos los que haya, de fábrica primero).
    void leer(const QImage& img, std::vector<std::string> aceptados, bool descartable, Respuesta luego);
    /// Traduce códigos ya leídos (o un texto que se pegó) con los tipos `aceptados`.
    void traducir(std::vector<documentos::Codigo> codigos, std::vector<std::string> aceptados, Respuesta luego);

    /// Todos los tipos que hay ahora: [{id, titulo, comoSeDice, formatos, origen:
    /// "fabrica" | "plugin", plugin?, pais, descripcion}].
    nlohmann::json tipos() const;
    const documentos::Catalogo& catalogo() const { return catalogo_; }

   signals:
    /// Cambiaron los tipos de los plugins (uno arrancó o se apagó).
    void tiposCambio();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    documentos::Catalogo catalogo_;
    QPointer<plugins::Plugins> plugins_;
};

} // namespace ba::app
