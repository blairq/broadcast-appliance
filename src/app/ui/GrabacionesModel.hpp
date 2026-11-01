// ============================================================================
// src/app/ui/GrabacionesModel.hpp — las grabaciones que se pueden subir (YouTube, Facebook)
//
// Una sola lista para las páginas de las plataformas y el MCP: las del evento en curso y
// las de Recordings/, lo más nuevo primero. Leer el disco puede tardar (un disco USB, una
// carpeta con cientos de archivos): se lee aparte, nunca en el hilo de la interfaz, al
// crearse, al pedirlo (refrescar) y solo cuando una grabación empieza o termina (el flanco
// de `graba` en la foto del motor). Filas: {uid, name, size, modified, path}.
// ============================================================================
#pragma once

#include <QString>

#include "ui/ListModels.hpp" // JsonListModel

namespace ba::app {

class AppRuntime;
class VentanillaMotor;

class GrabacionesModel : public JsonListModel {
    Q_OBJECT
   public:
    explicit GrabacionesModel(VentanillaMotor& ventanilla, QObject* parent = nullptr);

    /// Vuelve a leer las carpetas (aparte): la lista cambia cuando termina.
    void refrescar();
    /// La carpeta donde se graba ahora (la de la foto: FotoMotor::sala.carpetaGrabacion).
    QString carpeta() const { return QString::fromStdString(carpeta_); }

   private:
    VentanillaMotor& ventanilla_;
    bool grababa_ = false;  ///< para ver el flanco de `graba`
    std::string carpeta_;   ///< donde se graba ahora (FotoMotor::sala.carpetaGrabacion)
    unsigned lecturas_ = 0; ///< para quedarse solo con la última lectura pedida
};

} // namespace ba::app
