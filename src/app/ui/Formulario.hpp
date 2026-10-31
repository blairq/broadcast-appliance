#pragma once
// Los formularios armados por esquema: el formato de un campo (el que dibuja
// qml/controls/CampoFormulario) y la conversión de lo que escribió el operador al tipo
// del campo, con su error. UNA regla para todos los formularios (la configuración de un
// plugin, los parámetros de la acción de un equipo): antes cada página clasificaba los
// campos por el nombre de la clave, parseaba JSON y se tragaba los errores en QML.
//
// Un campo: {name, label, help, kind, value, options: [{value, label}], obligatorio}.
// kind: bool | int | float | number | list | text | path | color | clave | json | header | info.

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app::formulario {

QVariantMap campo(const QString& name, const QString& label, const QString& kind, const QVariant& value,
                  const QString& help = {}, const QVariantList& options = {});

/// El kind de un tipo declarado por un plugin o un driver (texto, clave, numero, si_no,
/// opcion, json); el que no se conoce es texto.
QString kindDeTipo(const QString& tipo);

/// Opciones [{value, label}] de una lista de textos (el valor es el texto).
QVariantList opcionesDe(const QStringList& valores);

/// Lo que dijo la vista, al tipo del campo, en `out`. "" o el error (traducido, con la
/// etiqueta del campo). Un vacío en un campo no obligatorio da un QVariant inválido
/// («no se manda»).
QString convertir(const QVariantMap& campo, const QVariant& crudo, QVariant* out);

/// Todos los `crudos` (nombre -> lo que dijo la vista) contra los `campos`: en `out` los
/// convertidos, más los obligatorios que faltan como error. "" o el primer error.
QString convertirTodos(const QVariantList& campos, const QVariantMap& crudos, QVariantMap* out);

} // namespace ba::app::formulario
