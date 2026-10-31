// src/app/video/SuperRes.hpp — superresolución de las entradas de red (Studio.superres)
//
// Cuatro motores, todos como elementos de GStreamer en la entrada (ba_gst_fuente):
//   - apple: la de VideoToolbox (macOS 26), ~3 ms. (obs-module/ba_gst_superres_mac.mm)
//   - fsr:   FSR 1 de AMD en OpenGL, cualquier GPU, sin red. (ba_gst_fsr.cpp)
//   - onnx:  una red en ONNX (ba_gst_onnxsr.cpp): la «red chica» que se entrena
//            acá mirando la cámara, SPAN o Real-ESRGAN (convertidos acá).
//
// El LABORATORIO (assets/superres/laboratorio.py) corre con el Python del
// programa; sus bibliotecas (PyTorch, spandrel, onnx: ~800 MB) se bajan UNA vez
// a Datos/Herramientas/superres-libs. Los modelos van a Datos/Modelos/superres
// (cada .onnx con su .json: qué es, de dónde, cuánto mejora). Un proceso a la
// vez; su avance va a `trabajo`. Lo que BAJA algo (el laboratorio, los pesos, los
// plugins, el runtime de GPU) va por la cola única de descargas (net/Descargas).
//
// Es de la interfaz: el grafo de video (VideoGrafos) es del dueño del motor, así que lo
// lee de la foto y le pide por ui/VideoViewModel (F3-03). Poner un motor o una tarjeta en
// una entrada es una orden: lo que se valida acá vuelve en el acto, y lo que dice el
// grafo, en `listo` (o en el aviso al operador).
#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "net/Descargas.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace ba::app::plugins {
class Hijo;
}
namespace ba::app {
class VideoViewModel;
}

namespace ba::app::video {

class SuperRes : public QObject {
    Q_OBJECT
    /// [{archivo, nombre, arquitectura, escala, canales, psnr_red, psnr_bicubico, licencia, fecha,
    /// tamano_mb}]
    Q_PROPERTY(QVariantList modelos READ modelosQml NOTIFY cambio)
    /// Qué motores hay en este sistema (apple, fsr, onnx).
    Q_PROPERTY(QStringList motores READ motores NOTIFY cambio)
    /// Si el laboratorio ya tiene sus bibliotecas.
    Q_PROPERTY(bool laboratorioListo READ laboratorioListo NOTIFY cambio)
    /// El trabajo en curso o el último: {que, entrada, corriendo, pct, texto, error, psnr_red, psnr_bicubico,
    /// modelo}
    Q_PROPERTY(QVariantMap trabajo READ trabajo NOTIFY trabajoCambio)

   public:
    using json = nlohmann::json;

    SuperRes(std::filesystem::path dataRoot, VideoViewModel* video, QObject* parent = nullptr);
    ~SuperRes() override;

    json modelos() const;
    QStringList motores() const;
    bool laboratorioListo() const;
    QVariantMap trabajo() const { return trabajo_; }
    /// Datos/Modelos/superres (no cambia: desde cualquier hilo).
    static std::filesystem::path carpetaModelos();

    /// Pone en la cola de descargas las bibliotecas del laboratorio. "" o el error.
    std::string prepararLaboratorio();
    /// El laboratorio (las bibliotecas de Python: PyTorch, spandrel, onnx):
    /// {listo, directml, carpeta, mb, bibliotecas}.
    json laboratorio() const;
    /// Borra sus bibliotecas (~800 MB; se vuelven a bajar cuando hagan falta). "" o el error.
    std::string borrarLaboratorio();
    /// Convierte un modelo de terceros: "span" o "esrgan". Prepara el laboratorio si hace falta.
    std::string convertir(const std::string& cual);
    /// Entrena la red chica mirando la entrada `entrada` (graba `segundos` y entrena
    /// `minutos`); si `aplicar`, al terminar la pone en esa entrada. Sigue desde la
    /// red anterior de esa entrada si la hay.
    std::string entrenar(const std::string& entrada, double minutos, int segundos, bool aplicar);
    /// DESTILACIÓN: entrena una red propia chica (luma x2, corre en vivo hasta en la
    /// CPU) que imita al plugin `maestro` (del catálogo: Real-ESRGAN, LSDIR…) en lo
    /// que ve la entrada. `cpu`: sin la GPU (más lento, no calienta). "" o el error.
    std::string destilar(const std::string& entrada, const std::string& maestro, double minutos, bool aplicar,
                         bool cpu);
    void cancelar();
    /// Pone un motor (y modelo, por nombre de archivo o ruta) en una entrada. "" o el error
    /// de lo que se valida acá; lo que diga el grafo llega a `listo` (sin `listo`, al aviso).
    std::string aplicar(const std::string& entrada, const std::string& motor, const std::string& modelo,
                        std::function<void(const std::string&)> listo = {});

    // --- Plugins (datos/superres-catalogo.json + Datos/Modelos/superres/<id>/) -----------
    /// Todos: los del catálogo (instalados o no) y los instalados que no están en
    /// él (la red propia entrenada acá). Cada uno: {id, nombre, tipo, descripcion,
    /// escala, plataformas, instalado, aca (sirve en esta máquina), ms_coreml?, licencia}.
    /// Lee el catálogo y la carpeta de modelos: desde cualquier hilo (superres_plugins
    /// contesta en el pool, con la interfaz trabada).
    static json plugins();
    /// Las recetas del catálogo (datos/superres-catalogo.json, recurso de Qt): se lee una vez.
    static const json& catalogo();
    /// Instala un plugin del catálogo (laboratorio `instalar`). "" o el error.
    std::string instalarPlugin(const std::string& id);
    /// Lo de esta máquina para ese plugin: {motor, modelo, escala_red, proveedor?}
    /// o vacío con `error`. Lo usa el grafo al mandar la tarjeta.
    json resolverPlugin(const std::string& id, std::string* error) const;
    /// Pone una tarjeta «Superresolución» con ese plugin en la entrada (o se lo cambia
    /// a la que tiene). "" o el error del plugin; lo que diga el grafo, en `listo` (como aplicar).
    std::string tarjetaConPlugin(const std::string& entrada, const std::string& id, const json& extra = json::object(),
                                 std::function<void(const std::string&)> listo = {});

    // --- ONNX Runtime de GPU (NVIDIA / AMD Linux / Intel), bajado a pedido ----------
    /// Qué placa hay (nvidia, amd, intel o ninguna), qué runtime está instalado
    /// y qué se recomienda bajar. {placa, instalado: {tipo}?, sugerido, nota}.
    json runtimeGpu() const;
    /// Baja el ONNX Runtime de GPU para `tipo` (nvidia, amd, intel; vacío = el sugerido)
    /// a Datos/Herramientas/onnxruntime-gpu. Corre como trabajo. "" o el error.
    std::string instalarRuntimeGpu(const std::string& tipo, bool conTensorRt = true);

    // --- Plugins propios de tipo «elemento» (contrato abierto) ------------------------
    /// Crea (o reemplaza) Datos/Modelos/superres/<id>/plugin.json para un elemento de
    /// GStreamer: {id, nombre, elemento, memoria, memoria_salida?, propiedades, escala, descripcion}.
    std::string guardarPluginElemento(const json& p);
    /// Borra un plugin instalado (su carpeta). "" o el error.
    std::string borrarPlugin(const std::string& id);
    /// Las variantes de una red compiladas para un tamaño fijo (sus
    /// variante.json); `plugin` vacío: las de todos.
    json variantes(const std::string& plugin) const;
    /// Compila la red de `plugin` para cada tamaño ("1024x576"): CoreML en la
    /// Mac, ONNX fijo en todos (variantes.py, sin PyTorch). Un trabajo.
    std::string crearVariantes(const std::string& plugin, const std::vector<std::string>& tamanos,
                               const std::string& id, const std::string& motores, const std::string& para,
                               const std::string& descripcion, bool reemplazar);
    /// Los tamaños de las cámaras del estudio que todavía no tienen variante de `plugin`.
    std::vector<std::string> tamanosSinVariante(const std::string& plugin) const;
    std::string borrarVariante(const std::string& plugin, const std::string& id);
    /// Cambia para qué es o la descripción (lo demás lo pone la herramienta).
    std::string editarVariante(const std::string& plugin, const std::string& id, const json& cambios);

    // --- Interfaz ---------------------------------------------------------------------
    QVariantList modelosQml() const;
    Q_INVOKABLE QString prepararUi() { return q(prepararLaboratorio()); }
    Q_INVOKABLE QString convertirUi(const QString& cual) { return q(convertir(cual.toStdString())); }
    Q_INVOKABLE QString entrenarUi(const QString& entrada, double minutos, bool aplicar) {
        return q(entrenar(entrada.toStdString(), minutos, 60, aplicar));
    }
    Q_INVOKABLE void cancelarUi() { cancelar(); }
    Q_INVOKABLE QString aplicarUi(const QString& entrada, const QString& motor, const QString& modelo) {
        return q(aplicar(entrada.toStdString(), motor.toStdString(), modelo.toStdString()));
    }
    Q_INVOKABLE QString borrarModelo(const QString& archivo);
    Q_INVOKABLE QVariantList pluginsUi() const;
    Q_INVOKABLE QString instalarPluginUi(const QString& id) { return q(instalarPlugin(id.toStdString())); }
    Q_INVOKABLE QVariantMap runtimeGpuUi() const;
    Q_INVOKABLE QString instalarRuntimeGpuUi(const QString& tipo, bool tensorrt) {
        return q(instalarRuntimeGpu(tipo.toStdString(), tensorrt));
    }
    Q_INVOKABLE QString guardarPluginElementoUi(const QVariantMap& p);
    Q_INVOKABLE QString borrarPluginUi(const QString& id) { return q(borrarPlugin(id.toStdString())); }
    /// Los plugins instalados de tipo «elemento», con su plugin.json completo.
    Q_INVOKABLE QVariantList pluginsElementoUi() const;
    Q_INVOKABLE QVariantList variantesUi(const QString& plugin) const;
    Q_INVOKABLE QString crearVarianteUi(const QString& plugin, int ancho, int alto, const QString& para,
                                        const QString& motores);
    Q_INVOKABLE QString crearFaltantesUi(const QString& plugin);
    Q_INVOKABLE QStringList tamanosSinVarianteUi(const QString& plugin) const;
    Q_INVOKABLE QString borrarVarianteUi(const QString& plugin, const QString& id) {
        return q(borrarVariante(plugin.toStdString(), id.toStdString()));
    }
    Q_INVOKABLE QString editarVarianteUi(const QString& plugin, const QString& id, const QString& para,
                                         const QString& descripcion);
    Q_INVOKABLE QString destilarUi(const QString& entrada, const QString& maestro, double minutos, bool aplicar,
                                   bool cpu) {
        return q(destilar(entrada.toStdString(), maestro.toStdString(), minutos, aplicar, cpu));
    }

   signals:
    void cambio();
    void trabajoCambio();

   private:
    static QString q(const std::string& s) { return QString::fromStdString(s); }
    std::filesystem::path python() const;
    std::filesystem::path libs() const;
    /// Corre `args` con el Python del programa; cada línea JSON va a `trabajo`; `fin` al terminar.
    /// `tipo` es la clave estable del trabajo (`trabajo.tipo`: laboratorio, convertir,
    /// entrenar, destilar, instalar, onnx, variantes); `que`, lo que se le muestra al operador.
    std::string correr(const char* tipo, const QString& que, const QString& entrada, const QStringList& args,
                       std::function<void(bool ok)> fin);
    void linea(const QByteArray& l);
    /// De dónde graba el laboratorio: la dirección de red de la entrada (--uri) o, si
    /// es una cámara local, una carpeta con cuadros capturados ahora (--entrada). Llama
    /// a `listo` con los argumentos (enseguida, o al terminar de capturar). "" o el error.
    std::string origen(const std::string& entrada, int segundos, std::function<void(QStringList)> listo);
    QTimer captura_;
    int capturados_ = 0, aCapturar_ = 0;
    bool pidiendoCuadro_ = false; ///< hay un cuadro pedido (capturarCuadroAparte) que no llegó

    std::filesystem::path root_;
    VideoViewModel* video_;
    QVariantMap trabajo_;
    std::unique_ptr<plugins::Hijo> hijo_;
    QByteArray pendiente_;
    std::function<void(bool)> fin_;
    std::vector<std::function<void()>> trasLaboratorio_; ///< lo que sigue cuando el laboratorio quedó listo
    std::string prepararLaboratorio(std::function<void()> despues);

    // Lo que BAJA algo (laboratorio, pesos, plugins, runtime de GPU) va por la cola
    // de descargas: `bajar` en el hilo de la cola (archivos verificados) y después
    // `lanzar` (el proceso de Python) en la interfaz, cuando el laboratorio no
    // tiene otro proceso andando. "" o el error del pedido.
    std::string encolar(net::Descargas::Pedido p, std::function<std::string(net::Descargas::Contexto&)> bajar,
                        std::function<std::string(std::function<void(bool)> alFin)> lanzar);
    void esperarYLanzar(std::function<std::string(std::function<void(bool)>)> lanzar,
                        std::function<void(const std::string&)> listo, net::Descargas::Contexto* c,
                        const std::string& id);
    void cortarHijo();
    net::Descargas::Contexto* ctxCola_ = nullptr; ///< el de la descarga que corre su proceso ahora
    std::string idCola_;
    std::function<void(const std::string&)> listoCola_;
    std::vector<std::function<void(const std::string&)>> listosVivos_;
};

} // namespace ba::app::video
