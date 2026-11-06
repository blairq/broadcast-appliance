// ============================================================================
// src/app/sala/GeneradorCarga.hpp — quien genera los invitados de prueba de la capacidad
//
// La prueba de capacidad (docs/funciones/CAPACIDAD.md) sube por escalones: en cada uno
// se suman los invitados NUEVOS (los de antes siguen adentro). `GeneradorCarga` es lo que
// el modelo (ui/CapacidadViewModel) necesita de quien los genera; el de verdad es
// `GeneradorHinchada` (hinchada, el probador que viaja en el programa: un proceso por
// escalón), y el test del modelo usa uno de mentira.
//
// La credencial de prueba va en el ENTORNO del proceso (HINCHADA_PIN), nunca en la línea
// de comandos (se ve en la lista de procesos).
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace ba::app::sala {

class GeneradorCarga : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;
    ~GeneradorCarga() override = default;

    struct Lote {
        QString url;        ///< el gateway (https://…)
        QString credencial; ///< la credencial de prueba (va al entorno)
        int cantidad = 0;   ///< los nuevos de este escalón
        int entradaCadaMs = 300;
        QString prefijo;    ///< para los nombres («c40-»): no se repiten entre escalones
        QString salida;     ///< carpeta para su consola e informe
        bool inseguro = true; ///< certificado propio de la caja (no se verifica)
        /// `--decodificar` de hinchada: "nada", "todo" o el % (sala::prueba::argDecodificar);
        /// vacío, el de hinchada.
        QString decodificar;
    };
    /// ¿Se puede? (hinchada viene en el programa)
    virtual bool disponible() const = 0;
    /// Suma un lote. "" o por qué no arrancó.
    virtual QString lanzar(const Lote& l) = 0;
    /// Corta todos los lotes (terminate y, si no se van, kill).
    virtual void parar() = 0;
    /// Los procesos andando (para medir su CPU).
    virtual std::vector<std::uint32_t> pids() const = 0;
    virtual bool andando() const = 0;
    /// ¿Listo para generar? (hinchada: su video grabado ya está; la primera vez, no).
    virtual bool preparado() const { return true; }
    /// Lo prepara (en el fondo; avisa con preparacionTerminada). "" o por qué no arrancó.
    virtual QString preparar() { return {}; }

   signals:
    void preparacionTerminada(bool ok, const QString& error);
    /// Un lote se fue solo (no por parar()): `codigo` de salida.
    void seCayo(int codigo);
};

/// hinchada: un proceso por lote, todos hijos de la app (se cortan al cerrarla).
class GeneradorHinchada final : public GeneradorCarga {
    Q_OBJECT
   public:
    /// `ejecutable`: bin/64bit/hinchada/hinchada; `grabado`: el video grabado común.
    GeneradorHinchada(QString ejecutable, QString grabado, QObject* parent = nullptr);
    ~GeneradorHinchada() override;

    bool disponible() const override;
    QString lanzar(const Lote& l) override;
    void parar() override;
    std::vector<std::uint32_t> pids() const override;
    bool andando() const override;
    bool preparado() const override;
    QString preparar() override;

   private:
    QString exe_, grabado_;
    std::vector<QPointer<QProcess>> procs_;
    bool parando_ = false;
};

} // namespace ba::app::sala
