// ============================================================================
// src/app/arranque/Configuraciones.hpp — configuraciones con nombre, cada una con lo suyo
//
// Cada configuración es una carpeta `<datos>/Configuraciones/<nombre>/` con TODO lo
// suyo (Config/, Media/, Graficos/, Plugins/, Fotos/, Miniaturas/, Eventos/,
// Recordings/…; ver platform/Paths.hpp): nada se comparte entre configuraciones, así
// un cambio en una (un plugin, una escena) no rompe otra. De la máquina quedan
// Modelos/, Herramientas/, Agentes/, Credenciales/, Logs/ y Runtime/.
//
// CAMBIAR de configuración no copia nada: se anota la activa en
// `<datos>/configuraciones.json` y se reinicia (el launcher le pasa a la app y al
// gateway la carpeta nueva). CREAR una sí escribe sus archivos una vez:
//   - "vacia": la configuración de fábrica;
//   - "actual" u otra por su nombre: una copia (sin grabaciones, salvo que se pida);
//   - "demo:<nombre>": una de las demos que trae el programa (Demos/<nombre>);
//   - "evento:<carpeta>/<inicio|cierre|ultima>": la configuración con la que se hizo
//     un evento (su foto de Config/, ver evento/FotosConfig) y los medios de la actual.
// Cada una puede tener una descripción (configuracion.json en su carpeta).
// Las rutas que guardan son relativas a su carpeta (arranque/RutasPortables): se
// pueden copiar a otra máquina tal cual.
// ============================================================================
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app {

class Configuraciones : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lista READ listaUi NOTIFY cambio)
    Q_PROPERTY(QString activa READ activaUi NOTIFY cambio)
    Q_PROPERTY(QVariantList demos READ demosUi CONSTANT)

   public:
    using json = nlohmann::json;
    /// `reiniciar`: cierra la app y la vuelve a abrir (con la configuración que quedó anotada).
    explicit Configuraciones(std::function<QString()> reiniciar, QObject* parent = nullptr);

    /// [{nombre, activa, descripcion, creada_ms, desde, bytes}].
    json listar() const;
    std::string activa() const;
    /// Las demos que trae el programa: [{nombre, descripcion}].
    json demos() const;

    /// Crea `nombre` desde `desde` (ver arriba). "" o el error.
    std::string crear(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                      bool conGrabaciones = false);
    /// Deja anotada `nombre` como activa (sin reiniciar). "" o el error.
    std::string activar(const std::string& nombre);
    std::string renombrar(const std::string& de, const std::string& a);
    std::string describir(const std::string& nombre, const std::string& descripcion);
    /// A la papelera (Configuraciones/.papelera/): nunca la activa.
    std::string borrar(const std::string& nombre);

    QVariantList listaUi() const;
    QString activaUi() const { return QString::fromStdString(activa()); }
    QVariantList demosUi() const;
    Q_INVOKABLE QString crearUi(const QString& nombre, const QString& desde, const QString& descripcion);
    /// Activa y reinicia.
    Q_INVOKABLE QString cambiarUi(const QString& nombre);
    Q_INVOKABLE QString renombrarUi(const QString& de, const QString& a);
    Q_INVOKABLE QString describirUi(const QString& nombre, const QString& descripcion);
    Q_INVOKABLE QString borrarUi(const QString& nombre);
    /// Reinicia con la que esté anotada (después de activar por MCP).
    QString reiniciar() { return reiniciar_ ? reiniciar_() : QStringLiteral("no se puede reiniciar desde acá"); }

   signals:
    void cambio();

   private:
    std::filesystem::path datos() const;
    std::filesystem::path carpeta(const std::string& nombre) const;
    static std::string nombreValido(const std::string& nombre);
    std::function<QString()> reiniciar_;
};

} // namespace ba::app
