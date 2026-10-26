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
// EL DORSO (la zona de lectura mecánica, documentos/Mrz): cuando el código del
// frente no sale, la franja de abajo de la foto de la parte de atrás va a un
// MODELO DE VISIÓN (agent/Mirada), que transcribe los tres renglones:
//   1. el MODELO LOCAL con su proyector de visión (el llama-server del programa en
//      127.0.0.1, con --mmproj): la foto NO SALE DE LA MÁQUINA. Es el primero;
//   2. si no hay visión local (o falla), el MODELO PROPIO del operador, si está
//      configurado (su servidor; ver DOCUMENTOS.md, «Privacidad»).
// Lo que contesta pasa por el parser TD1 con sus dígitos verificadores (un modelo
// puede inventar) y el criterio de varias fotos (documentos::ConfirmadorMrz), y
// se traduce como un código más (formato "MRZ") con los mismos tipos. Sin
// ninguno de los dos, no hay dorso (dorsoDisponible()).
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

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"

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
        /// Lo leído salió del dorso (la MRZ), no de un código de barras.
        bool dorso = false;
        /// Del dorso, si no se leyó: por qué, sin datos («no se ven los renglones»,
        /// «no cierra el verificador del número»). Para decir qué corregir y para el log.
        std::string detalle;
        /// Del dorso: el modelo dijo que no se ven los renglones.
        bool noSeVe = false;
        /// Del dorso: cerraron todos los verificadores menos el compuesto y todavía no
        /// hay otra foto que coincida (se sigue mirando).
        bool dudosa = false;
        /// Del dorso: quién miró ("modelo local", "modelo propio").
        std::string con;
    };
    /// Cómo pedir el llama-server local con visión (lo engancha TotemsViewModel con
    /// ModelosLocales y ServidorModeloLocal).
    struct VisionLocal {
        /// Si se puede (hay modelo, su proyector de visión y llama-server); si no, `falta`
        /// dice qué falta (p. ej. qué complemento bajar).
        std::function<bool(QString* falta)> disponible;
        /// Pedir el servidor para `quien`: `listo(puerto, "")` o `listo(0, error)`.
        std::function<void(QObject* quien, std::function<void(quint16, QString)> listo)> pedir;
        std::function<void(QObject* quien)> soltar;
    };
    /// Se llama SIEMPRE en el hilo de la interfaz.
    using Respuesta = std::function<void(Resultado)>;

    /// `dataRoot`: de donde sale la configuración del modelo propio (el dorso).
    LectorDocumentos(plugins::Plugins* plugins, std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~LectorDocumentos() override;

    /// Si esta compilación trae el lector de códigos (zxing-cpp).
    static bool disponible();

    /// Busca los códigos de `img` (en el hilo propio) y los traduce con los tipos
    /// `aceptados` (vacío = todos los que haya, de fábrica primero).
    void leer(const QImage& img, std::vector<std::string> aceptados, bool descartable, Respuesta luego);
    void setVisionLocal(VisionLocal v) { vision_ = std::move(v); }
    /// Si se puede leer el dorso: visión local o modelo propio.
    bool dorsoDisponible() const;
    /// Con qué se leería ("modelo local", "modelo propio", "") y, si no se puede o no
    /// hay local, qué falta para tenerlo.
    QString dorsoCon(QString* falta = nullptr) const;
    /// Que el modelo local con visión vaya cargando (el tótem, al empezar a pedir el
    /// documento: cargar el modelo tarda), y soltarlo cuando ya no se busca ningún dorso.
    void prepararDorso();
    void soltarDorso();
    /// Le pide al modelo de visión los renglones del dorso de `img` (en un hilo aparte,
    /// con tope de espera), los valida (documentos::interpretarRespuestaMrz y, con
    /// `confirmador`, el criterio de dos fotos; sin él, solo se acepta una lectura con
    /// todos los verificadores) y los traduce con los tipos `aceptados`. Sin cola:
    /// quien llama manda una foto por vez (el tótem, una por conexión).
    void leerDorso(const QImage& img, std::vector<std::string> aceptados,
                   std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego);
    /// Traduce códigos ya leídos (o un texto que se pegó) con los tipos `aceptados`.
    void traducir(std::vector<documentos::Codigo> codigos, std::vector<std::string> aceptados, Respuesta luego);

    /// Todos los tipos que hay ahora: [{id, titulo, comoSeDice, formatos, origen:
    /// "fabrica" | "plugin", plugin?, pais, descripcion, dorso}] (`dorso`: se lee también
    /// por la parte de atrás, la MRZ, si dorsoDisponible()).
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
    std::filesystem::path dataRoot_;
    VisionLocal vision_;
    QObject* retenido_ = nullptr; ///< quien tiene el servidor local mientras el tótem busca dorsos

    /// Lo que contestó un modelo (o su error), al resultado.
    void alTranscribir(std::string texto, std::string error, std::string con, std::vector<std::string> aceptados,
                       std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego);
    void dorsoConPropio(const QImage& img, std::vector<std::string> aceptados,
                        std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego, std::string antes);
};

} // namespace ba::app
