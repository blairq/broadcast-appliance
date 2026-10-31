// ============================================================================
// src/app/ui/TemaEventoViewModel.hpp — el tema del evento abierto (la cara en la web)
//
// El acento, el fondo, la letra, el logo y el nombre que llevan TODAS las páginas web
// del evento abierto (docs/planes/PLAN-TEMA-EVENTO.md). El modelo es evento::TemaEvento
// (Eventos/<x>/tema.json y Tema/); acá se lo edita (Servicios → Aspecto de las páginas
// web, PanelTemaEvento.qml; y el MCP: evento_tema) y se lo publica al gateway, que arma
// /comun/tema.css para todas.
//
// SE PUBLICA SOLO, sin depender del sitio del evento (la sesión remota tiene cara aunque
// el evento no tenga página): al abrir o cambiar de evento, al cambiar el tema o los
// colores del sitio, y CADA VEZ QUE LA SALA VUELVE (el gateway lo tiene en memoria: si
// se reinicia, lo olvida y hay que volver a dárselo). Lo que se mandó por última vez se
// recuerda para no repetirlo.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/TemaEvento.hpp"

namespace ba::app {

class ConferenceViewModel;
class EventoViewModel;

class TemaEventoViewModel : public QObject {
    Q_OBJECT
    /// Hay un evento abierto (sin evento no hay tema que editar).
    Q_PROPERTY(bool hayEvento READ hayEvento NOTIFY cambio)
    /// El evento tiene tema propio (tema.json) o el que sale de su sitio; sin ninguno, las
    /// páginas quedan como son.
    Q_PROPERTY(bool existe READ existe NOTIFY cambio)
    /// Todavía no hay tema.json: lo que se ve sale de los colores del sitio.
    Q_PROPERTY(bool delSitio READ delSitio NOTIFY cambio)
    /// Lo editable, como en tema.json: {modo, colores{…}, tipografia{texto, titulos, archivos},
    /// forma{radio}, marca{nombre, logo, logoClaro, favicon}, alcance{publico, control}}.
    Q_PROPERTY(QVariantMap tema READ tema NOTIFY cambio)
    /// Lo que va a salir (ba::tema::resolver): los colores finales y el contraste.
    Q_PROPERTY(QVariantMap resuelto READ resuelto NOTIFY cambio)
    /// Los avisos de contraste, ya en palabras.
    Q_PROPERTY(QStringList avisos READ avisos NOTIFY cambio)
    /// El logo (y el claro) para la vista previa: file:// o "".
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY cambio)
    Q_PROPERTY(QString logoClaroUrl READ logoClaroUrl NOTIFY cambio)
    /// La carpeta Tema/ del evento abierto ("" sin evento).
    Q_PROPERTY(QString carpeta READ carpeta NOTIFY cambio)

   public:
    TemaEventoViewModel(EventoViewModel& evento, ConferenceViewModel& sala, QObject* parent = nullptr);

    bool hayEvento() const { return !carpetaEvento_.empty(); }
    bool existe() const { return tema_.existe; }
    bool delSitio() const { return tema_.delSitio; }
    QVariantMap tema() const;
    QVariantMap resuelto() const;
    QStringList avisos() const;
    QString logoUrl() const { return urlDe(tema_.marca.logo); }
    QString logoClaroUrl() const { return urlDe(tema_.marca.logoClaro); }
    QString carpeta() const;

    /// Cambia lo que venga (las mismas claves que `tema`; lo que no viene queda): colores
    /// "#rrggbb" o "" (se calcula), modo "", "claro" u "oscuro", radio -1…32, alcance
    /// nada|acento|todo, tipografia{texto, titulos}, marca{nombre}. Devuelve el error o "".
    Q_INVOKABLE QString cambiar(const QVariantMap& campos);
    /// Copia a Tema/ un logo (svg, png o webp) y lo pone de `cual`: logo | logoClaro | favicon.
    /// `archivo`: ruta o file://. Devuelve el error o "".
    Q_INVOKABLE QString subirLogo(const QString& archivo, const QString& cual);
    /// Copia una fuente (woff2, woff, ttf, otf) y la suma a la tipografía con esa familia
    /// (y, si no había, la pone como letra del texto). Devuelve el error o "".
    Q_INVOKABLE QString subirFuente(const QString& archivo, const QString& familia, const QString& peso = "400",
                                    const QString& estilo = "normal");
    /// Saca algo: logo | logoClaro | favicon | fuentes. Borra de Tema/ lo que ya no se usa.
    Q_INVOKABLE QString quitar(const QString& que);
    /// Borra tema.json: vuelve al de los colores del sitio (o a ninguno).
    Q_INVOKABLE QString restablecer();
    /// Pasa el sitio ya generado (Web/) al tema común (ver evento::migrarSitio). Devuelve
    /// {cambiados: [...]} o {error}.
    Q_INVOKABLE QVariantMap migrarSitio();

    /// Para el MCP: lo de tema.json, lo resuelto, los avisos y dónde están los archivos.
    nlohmann::json estado() const;
    /// Lo mismo que `cambiar`, con JSON (el MCP).
    QString cambiarJson(const nlohmann::json& campos);

   signals:
    void cambio();

   private:
    void seguirEvento();
    void recargar();
    void publicar();
    QString guardar(evento::TemaEvento t);
    QString urlDe(const std::string& archivo) const;

    EventoViewModel& evento_;
    ConferenceViewModel& sala_;
    std::filesystem::path carpetaEvento_;
    evento::TemaEvento tema_;
    nlohmann::json resuelto_ = nlohmann::json::object();
    std::string mandado_; ///< el último cuerpo que aceptó la sala ("" = hay que mandarlo)
};

} // namespace ba::app
