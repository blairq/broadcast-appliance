// ============================================================================
// src/app/arranque/Registrar.cpp — ver Registrar.hpp
// ============================================================================
#include "arranque/Registrar.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "diag/Caidas.hpp"
#include "diag/Perfil.hpp"

#include <cstdlib>
#include "diag/Registro.hpp"

#if BA_APP_HAVE_GUI
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>
#define BA_REG_QT 1
#endif

namespace ba::app {
namespace {

using ba::diag::Nivel;

#if BA_REG_QT
/// El mensaje de Qt trae el archivo y la línea cuando es de QML, y ahí es
/// justamente donde hace falta: un aviso de QML sin el archivo no se puede
/// arreglar. Se pega al final para que la descripción quede primero.
std::string conOrigen(const QMessageLogContext& c, const QString& msg) {
    std::string s = msg.toStdString();
    if (c.file != nullptr && c.line > 0) {
        // Solo el nombre del archivo: la ruta entera del bundle no aporta y
        // hace la línea tres veces más larga.
        std::string archivo(c.file);
        const auto barra = archivo.find_last_of("/\\");
        if (barra != std::string::npos) archivo = archivo.substr(barra + 1);
        s += "  (" + archivo + ":" + std::to_string(c.line) + ")";
    }
    return s;
}

void handlerDeQt(QtMsgType tipo, const QMessageLogContext& c, const QString& msg) {
    Nivel n = Nivel::Info;
    switch (tipo) {
        case QtDebugMsg: n = Nivel::Depuracion; break;
        case QtInfoMsg: n = Nivel::Info; break;
        case QtWarningMsg: n = Nivel::Aviso; break;
        case QtCriticalMsg: n = Nivel::Error; break;
        case QtFatalMsg: n = Nivel::Fatal; break;
    }
    // Lo de QML se separa de lo del resto de Qt, que es la division que de
    // verdad se usa al buscar un problema. No sirve mirar la categoria: los
    // avisos del motor de QML vienen en la categoria "default". Lo que sí los
    // distingue es que el archivo es un .qml.
    const std::string cat = c.category != nullptr ? c.category : "";
    const std::string archivo = c.file != nullptr ? c.file : "";
    const bool esQml = archivo.size() > 4 && archivo.compare(archivo.size() - 4, 4, ".qml") == 0;
    ba::diag::registro().linea(n, esQml ? "qml" : "qt", 0, conOrigen(c, msg));
}
#endif


} // namespace

void registrarTodo(const std::filesystem::path& dataRoot, const char* componente) {
    // Antes que nada: si esto se cae, que deje la pila escrita. Ver
    // core/diag/Caidas.hpp — paso una vez que A murio con SIGSEGV y en el
    // registro no quedo ni una linea.
    ba::diag::instalarManejadorDeCaidas(componente);

    auto& reg = ba::diag::registro();
    reg.setComponentePorDefecto(componente);
    // Con el launcher, el archivo lo escribe la captura de stderr; suelto, se
    // abre uno propio. Ver Registro::abrirSegunEntorno.
    reg.abrirSegunEntorno(dataRoot / "Logs", std::string(componente) + ".log");
    ba::diag::Perfil::arrancar(componente, dataRoot / "Logs"); // en un build normal no hace nada

#if BA_REG_QT
    qInstallMessageHandler(handlerDeQt);
    // Los avisos de QML vienen apagados por defecto en un build de release de
    // Qt. Se prenden a propósito: son los que dicen que la interfaz se rompió.
    QLoggingCategory::setFilterRules("qt.qml.binding.removal.info=false\n"
                                     "qml.debug=false\n"
                                     "*.warning=true\n");
#endif
    reg.info("registro abierto (" + std::string(componente) + ")");
}

} // namespace ba::app
