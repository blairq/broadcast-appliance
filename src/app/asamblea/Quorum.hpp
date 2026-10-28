// SPDX-License-Identifier: GPL-2.0-or-later
//
// Quorum: las reglas del cuerpo para sesionar y para aprobar, y el resultado
// OFICIAL de cada votación (el del tablero del recinto o el que lee
// Secretaría), que es el que va al acta. La urna del público es otra cosa
// (consulta, no vinculante).
//
//   quórum: {miembros, minimo: "mitad_mas_uno" | n}
//   mayoría de fábrica: simple | absoluta | dos_tercios_presentes | dos_tercios_miembros
//   resultado oficial: {punto?, asunto, afirmativos, negativos, abstenciones,
//                       presentes, mayoria, aprobado, nominal?: [{nombre, voto}]}
//
// Config/quorum.json (reglas) y Config/votaciones-oficiales.json (la sesión).
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::asamblea {

class Quorum : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estadoUi NOTIFY cambio)
    Q_PROPERTY(QVariantList oficiales READ oficialesUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    /// `presentes`: cuántos miembros con voto hay ahora (del padrón o del cuerpo).
    /// `miembrosDelPadron`: cuántos tienen voto en el padrón; son los miembros mientras
    /// no se fije la cantidad a mano (miembros 0).
    Quorum(const std::filesystem::path& datos, std::function<int()> presentes,
           std::function<int()> miembrosDelPadron = {}, QObject* parent = nullptr);

    json regla() const;
    /// {miembros?, minimo?, mayoria?}. "" o el error.
    std::string setRegla(const json& r);
    /// {hay_quorum, presentes, necesarios, miembros, miembros_fijos (0 = los del padrón),
    /// minimo, mayoria}.
    json estado() const;
    /// Cuántos votos afirmativos hacen falta con esta mayoría.
    static int necesarios(const std::string& mayoria, int votantes, int presentes, int miembros);
    /// Carga un resultado oficial: {asunto, afirmativos, negativos, abstenciones, presentes?, mayoria?, punto?, nominal?}
    /// o {csv}: «nombre;voto» por línea (afirmativo|negativo|abstención|ausente). Devuelve el resultado o {error}.
    json cargarOficial(const json& r);
    json oficiales() const { return oficiales_; }
    void limpiarOficiales();
    /// El anexo del acta: quórum y votaciones oficiales (markdown).
    std::string anexoActa() const;

    QVariantMap estadoUi() const;
    QVariantList oficialesUi() const;
    Q_INVOKABLE QString setReglaUi(const QVariantMap& r);
    /// Carga rápida desde la interfaz. "" o el error.
    Q_INVOKABLE QString cargarOficialUi(const QString& asunto, int afirmativos, int negativos, int abstenciones,
                                        const QString& mayoria);
    Q_INVOKABLE QString cargarCsvUi(const QString& asunto, const QString& csv, const QString& mayoria);

   signals:
    void cambio();

   private:
    void guardar() const;
    /// Los fijados a mano o, si no, los del padrón con voto.
    int miembros() const;
    std::filesystem::path archivoRegla_, archivoOficiales_;
    std::function<int()> presentes_, miembrosDelPadron_;
    int miembros_ = 0;
    json minimo_ = "mitad_mas_uno";
    std::string mayoria_ = "simple";
    json oficiales_ = json::array();
};

} // namespace ba::app::asamblea
