// ============================================================================
// src/app/arranque/TraductorQt.cpp — ver TraductorQt.hpp
// ============================================================================
#include "arranque/TraductorQt.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

namespace ba::app {

QString TraductorQt::translate(const char* context, const char* sourceText, const char* disambiguation,
                               int n) const {
    if (sourceText == nullptr) return {};
    return QString::fromStdString(ba::i18n::traducir(context, sourceText, disambiguation, n));
}

void instalarIdioma() {
    ba::i18n::usar(ba::i18n::idiomaDeLaMaquina(ba::platform::dataRoot()));
    const ba::i18n::Perfil& p = ba::i18n::actual();
    static TraductorQt traductor;
    QCoreApplication::installTranslator(&traductor);
    // Los formatos: los del sistema si habla la misma lengua (un operador en inglés
    // de Australia quiere sus fechas), si no los del idioma.
    const QLocale perfil(QString::fromStdString(p.locale));
    const QLocale sistema = QLocale::system();
    QLocale::setDefault(sistema.language() == perfil.language() ? sistema : perfil);
    QGuiApplication::setLayoutDirection(p.derechaAIzquierda ? Qt::RightToLeft : Qt::LeftToRight);
    ba::diag::registro().info("idioma: " + p.codigo + " (" + p.nombre + "), formatos " +
                              QLocale().name().toStdString());
}

} // namespace ba::app
