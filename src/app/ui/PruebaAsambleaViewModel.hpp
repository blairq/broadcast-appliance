// ============================================================================
// src/app/ui/PruebaAsambleaViewModel.hpp — la prueba de asamblea (integrantes simulados)
//
// Como la Prueba de sala, pero para la SESIÓN REMOTA de la asamblea
// (docs/funciones/PRUEBA-ASAMBLEA.md): N integrantes de prueba entran por el camino real
// del integrante (link + PIN o usuario + contraseña, /ws?sesion), esperan que los
// habiliten en el Recinto, piden la palabra, hablan con cámara y micrófono, votan, se les
// corta la red y vuelven, alguno erra el PIN hasta quedar bloqueado. Los genera hinchada
// (el probador que viaja en el programa, --servidor integrantes) o un generador externo
// con el archivo de credenciales que se muestra.
//
// Es el MODELO/CONTROLADOR de la tarjeta «Prueba de asamblea» (Asamblea → Conexiones
// remotas) y del MCP (prueba_asamblea_*), con las mismas reglas para los dos
// (asamblea/PruebaAsambleaReglas):
//
//   - Preparar: SOLO en un evento sin integrantes reales (si no, no arranca y pide un
//     evento de prueba: la tarjeta ofrece crear uno vacío y abrirlo). Crea los integrantes
//     marcados `prueba`, los ubica en el recinto (el de antes queda guardado) y les da
//     acceso (link + PIN y usuario + contraseña), todo de una vez.
//   - Arrancar: escribe las credenciales para el probador (Pruebas/prueba-asamblea-<fecha>/
//     integrantes.json, solo legible por el dueño) y lanza hinchada contra el gateway de
//     esta máquina. Durante la prueba, una vez por segundo (un temporizador de acá, nunca
//     la vista), cuenta cómo van según la app: conectados, esperando, habilitados, en la
//     fila, con la palabra, los votos; con «habilitar solos» los habilita a los N s.
//   - Terminar (a mano, por tiempo o porque el probador terminó): corta el probador, saca
//     a los de prueba de la sesión, revoca su acceso, los borra del Cuerpo, deja el recinto
//     como estaba, borra el archivo de credenciales y guarda el informe (lo de la app y lo
//     de hinchada: qué le pasó a cada uno, latencias) en Pruebas/ del evento y en su lista
//     de chequeo (ítem «prueba_asamblea»).
//   - Si el programa se cerró en el medio, al volver a arrancar se limpia lo que quedó
//     (Runtime/prueba-asamblea-en-curso.json y los integrantes marcados).
//
// Vive en el hilo de la interfaz.
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "asamblea/PruebaAsambleaReglas.hpp"

namespace ba::app {
class ConferenceClient;
class EventoViewModel;
namespace asamblea {
class CuerpoViewModel;
class Cuerpo;
class SesionRemota;
} // namespace asamblea

class PruebaAsambleaViewModel : public QObject {
    Q_OBJECT
    /// "apagada" | "preparada" (los integrantes y sus accesos, sin probador) | "corriendo" | "terminando"
    Q_PROPERTY(QString estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(bool corriendo READ corriendo NOTIFY estadoChanged)
    /// Preparada, corriendo o terminando: hay integrantes de prueba en el Cuerpo.
    Q_PROPERTY(bool activa READ activa NOTIFY estadoChanged)
    Q_PROPERTY(int maxIntegrantes READ maxIntegrantes CONSTANT)
    /// Los del padrón del evento que NO son de prueba (con alguno, la prueba no corre acá).
    Q_PROPERTY(int integrantesReales READ integrantesReales NOTIFY padronChanged)
    /// Los de prueba que hay en el Cuerpo ahora.
    Q_PROPERTY(int integrantesPrueba READ integrantesPrueba NOTIFY padronChanged)
    /// Por qué no se puede preparar en este evento ("" = se puede).
    Q_PROPERTY(QString impedimento READ impedimento NOTIFY padronChanged)
    Q_PROPERTY(bool hinchadaDisponible READ hinchadaDisponible CONSTANT)
    /// "hinchada" | "externo" (el de la prueba en curso o la última)
    Q_PROPERTY(QString generador READ generador NOTIFY estadoChanged)
    /// Para un generador externo: el archivo de credenciales (copiarlo a la otra máquina),
    /// la URL y la línea de hinchada. Vacíos sin prueba corriendo.
    Q_PROPERTY(QString archivoCredenciales READ archivoCredenciales NOTIFY accesoChanged)
    Q_PROPERTY(QString urlIngreso READ urlIngreso NOTIFY accesoChanged)
    Q_PROPERTY(QString comandoExterno READ comandoExterno NOTIFY accesoChanged)
    /// Cómo va (PruebaAsambleaReglas::vivo + segundos): pedidos, conectados, esperando,
    /// habilitados, afuera, bloqueados, enLaFila, conPalabra, votaron, porOpcion, votacion.
    Q_PROPERTY(QVariantMap vivo READ vivo NOTIFY vivoChanged)
    /// El informe de la última terminada (con `archivo` y `nota`); vacío si no hubo.
    Q_PROPERTY(QVariantMap ultimoInforme READ ultimoInforme NOTIFY informeChanged)
    /// El comportamiento de fábrica (PruebaAsambleaReglas::Comportamiento, en snake_case).
    Q_PROPERTY(QVariantMap comportamientoPorOmision READ comportamientoPorOmision CONSTANT)

   public:
    struct Partes {
        ConferenceClient& sala;
        asamblea::SesionRemota* sesion; ///< puede faltar (sin sesión remota no hay prueba)
        asamblea::CuerpoViewModel& cuerpo;
        EventoViewModel& evento;
        std::filesystem::path dataRoot;
    };
    explicit PruebaAsambleaViewModel(Partes p, QObject* parent = nullptr);
    ~PruebaAsambleaViewModel() override;

    QString estado() const { return estado_; }
    bool corriendo() const { return estado_ == QLatin1String("corriendo"); }
    bool activa() const { return estado_ != QLatin1String("apagada"); }
    int maxIntegrantes() const { return asamblea::prueba::kMaxIntegrantes; }
    int integrantesReales() const;
    int integrantesPrueba() const;
    QString impedimento() const;
    bool hinchadaDisponible() const;
    QString generador() const { return generador_; }
    QString archivoCredenciales() const { return archivo_; }
    QString urlIngreso() const { return url_; }
    QString comandoExterno() const;
    QVariantMap vivo() const { return vivo_; }
    QVariantMap ultimoInforme() const { return ultimo_; }
    QVariantMap comportamientoPorOmision() const;

    /// Crea `n` integrantes de prueba y les da acceso (sin lanzar el probador). "" o por qué no.
    Q_INVOKABLE QString preparar(int n);
    /// Arranca: prepara si hace falta, escribe las credenciales y (con `conHinchada`) lanza el
    /// probador por `minutos`. `comportamiento`: las claves de PruebaAsambleaReglas::Comportamiento
    /// (lo que falta, de fábrica). "" o por qué no.
    Q_INVOKABLE QString arrancar(int n, int minutos, const QVariantMap& comportamiento, bool conHinchada);
    /// Termina (o descarta una preparada): limpieza completa e informe.
    Q_INVOKABLE void parar();
    /// Sin prueba: borra los integrantes de prueba que hayan quedado (y su acceso). "" o el error.
    Q_INVOKABLE QString limpiar();

    /// Para el MCP: lo mismo con JSON.
    std::string arrancarJson(int n, int minutos, const nlohmann::json& comportamiento, bool conHinchada);
    /// Todo junto: estado, generador, padrón, acceso (sin el archivo si `conAcceso` es false),
    /// vivo y el resumen del último informe.
    nlohmann::json estadoJson(bool conAcceso) const;
    nlohmann::json ultimoInformeJson() const { return ultimoJson_; }

   signals:
    void estadoChanged();
    void padronChanged();
    void accesoChanged();
    void vivoChanged();
    void informeChanged();
    void notify(const QString& texto, const QString& nivel);

   private:
    asamblea::Cuerpo& cuerpo() const;
    void setEstado(const QString& e);
    void tic();
    void lanzarHinchada();
    void terminar(const std::string& motivo);
    void cerrar(const std::string& motivo);
    /// Saca a los de prueba de la sesión, revoca su acceso, los borra y deja el recinto. Cuántos.
    int limpiarIntegrantes(const nlohmann::json& recinto, bool restaurarRecinto);
    void limpiarColgada();
    nlohmann::json calcularVivo() const;
    std::filesystem::path carpetaPruebas() const;
    std::filesystem::path archivoEnCurso() const;
    QString rutaHinchada() const;
    static std::int64_t ahoraMs();

    Partes p_;
    QString estado_ = QStringLiteral("apagada");
    QString generador_;
    QString archivo_, url_, salida_;
    asamblea::prueba::Comportamiento comp_;
    int minutos_ = 5, pedidos_ = 0, maxConectados_ = 0;
    std::vector<std::string> ids_;
    nlohmann::json accesosJson_;          ///< lo que lee hinchada (PruebaAsambleaReglas::archivoHinchada)
    bool arranco_ = false;                ///< se lanzó (o se abrió para un generador externo)
    nlohmann::json recintoAntes_;          ///< el recinto de antes de la prueba (null: no había)
    bool hayRecintoAntes_ = false;
    std::map<std::string, std::int64_t> esperaDesde_; ///< «habilitar solos»: desde cuándo espera cada uno
    QVariantMap vivo_, ultimo_;
    nlohmann::json vivoJson_ = nlohmann::json::object();
    nlohmann::json ultimoJson_ = nlohmann::json::object();
    QTimer tic_;
    QPointer<QProcess> hinchada_;
    std::int64_t inicioMs_ = 0, topeMs_ = 0;
};

} // namespace ba::app
