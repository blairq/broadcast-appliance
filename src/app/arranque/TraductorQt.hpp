// ============================================================================
// src/app/arranque/TraductorQt.hpp — qsTr y QObject::tr, con los catálogos de ba::i18n
//
// Qt traduce con un QTranslator; este le pregunta a ba::i18n (el mismo catálogo que
// usan el núcleo, el gateway y la web), así no hay un .qm aparte ni lrelease en la
// compilación. Ver docs/planes/PLAN-I18N.md.
// ============================================================================
#pragma once

#include <QTranslator>

namespace ba::app {

class TraductorQt : public QTranslator {
public:
    using QTranslator::QTranslator;
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return false; }
};

/// Elige el idioma de la máquina y lo pone en la app: el catálogo (ba::i18n::usar), el
/// traductor de Qt, los formatos (QLocale: los del sistema si es la misma lengua, si no
/// los del perfil) y la dirección (derecha a izquierda). Después de crear la
/// QGuiApplication y antes de mostrar nada.
void instalarIdioma();

} // namespace ba::app
