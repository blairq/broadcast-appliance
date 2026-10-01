// ============================================================================
// src/app/ui/Reconocedor.hpp — el reconocimiento de caras, FUERA de la interfaz
//
// POR QUE EXISTE ESTE ARCHIVO. El reconocimiento corría en el hilo de la
// interfaz: lo llamaba el reloj de la página de Personas y lo llamaba cada
// herramienta MCP `rostro_*` —el servidor MCP es un QTcpServer sobre el mismo
// bucle de eventos, así que una llamada suya ES el hilo de la interfaz—.
// Mientras detectaba y describía, la interfaz no dibujaba, el reloj de control
// no corría y el latido que A le manda al launcher se estancaba. Pasados 5 s
// el supervisor da a A por colgado y lo mata: probar el reconocimiento dejaba
// al aparato reiniciándose solo.
//
// COMO SE REPARTE EL TRABAJO, y por qué así:
//   - el cuadro se toma EN LA INTERFAZ (FrameService es suyo y no se comparte),
//   - detectar y describir van al hilo de acá (es lo que tarda),
//   - la libreta se toca solo EN LA INTERFAZ, al volver.
// De ese reparto sale que no haga falta un solo mutex: cada cosa la toca un
// solo hilo. El `Motor` de acá no lo toca nadie más.
//
// SE PUEDEN DESCARTAR PEDIDOS, a propósito. El reloj de la página pide una
// mirada por segundo; si una tarda más que eso, encolarlas haría una fila que
// crece para siempre y muestra caras viejas. Un pedido `descartable` que llega
// con otro en vuelo simplemente no se hace: el próximo segundo vuelve a pedir.
// Los de MCP nunca se descartan —alguien está esperando esa respuesta—.
// ============================================================================
#pragma once

// Sin los modelos vendoreados (`make deps-rostros`) este archivo no existe:
// no hay a quién sacarle el trabajo del hilo de la interfaz.
#if BA_HAVE_ROSTROS

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "rostros/Rostros.hpp"

namespace ba::app {

class Reconocedor : public QObject {
    Q_OBJECT
   public:
    /// Una cara vista: dónde está y su vector de 128 números.
    struct Visto {
        ba::rostros::Cara cara;
        std::vector<float> vector;
    };
    /// Se llama SIEMPRE en el hilo de la interfaz. `error` vacío = salió bien.
    using Respuesta = std::function<void(std::vector<Visto>, const QString& error)>;

    explicit Reconocedor(QObject* parent = nullptr);
    ~Reconocedor() override;

    /// Carga los modelos (en el hilo propio; espera a que terminen, ~100 ms una
    /// sola vez al arrancar). false deja el motivo en `error()`.
    bool abrir(const std::string& carpetaModelos);
    bool listo() const;
    QString error() const;

    /// Encola un análisis del cuadro `bgra` (ancho x alto, 4 bytes por píxel).
    ///
    /// :param descartable: true si es una mirada periódica que se puede saltear
    ///        cuando ya hay otra en vuelo. false para lo que alguien espera.
    void pedir(std::vector<std::uint8_t> bgra, int ancho, int alto, bool descartable, Respuesta luego);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
