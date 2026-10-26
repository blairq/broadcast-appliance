// ============================================================================
// src/core/documentos/DniArgentino.hpp — el DNI argentino por su PDF417
//
// El código de barras 2D del frente del DNI trae los datos separados por «@».
// Hay DOS formatos en la calle, y los dos se aceptan:
//
//   NUEVO (DNI tarjeta desde ~2012 y el actual): 8 o 9 campos
//     trámite@apellido@nombres@sexo@DNI@ejemplar@nacimiento@emisión[@cuil]
//     00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016@274
//   El noveno, cuando está, son las dos primeras cifras del CUIL y la última
//   («274» -> 27-11222333-4): se usa solo si el dígito verificador cierra.
//
//   VIEJO (los primeros DNI tarjeta, ~2009-2012): empieza con «@», 16 o 17 campos
//     @DNI@ejemplar@?@apellido@nombres@nacionalidad@nacimiento@sexo@emisión@
//     trámite@?@vencimiento@…
//   (el número viene con espacios atrás: «@11222333    @A@1@…»).
//
// EL DORSO (formato "MRZ", documentos/Mrz): los tres renglones de la zona de
// lectura mecánica, TD1 de la OACI con emisor ARG. El número del DNI va en el
// campo del número de documento (6-14, con su verificador); si ahí no hay un
// número de DNI (algunas series traen otra cosa), se lo busca en los datos
// opcionales del renglón 1. Lo que sale del dorso no trae trámite, ejemplar ni
// CUIL, y trae el vencimiento.
//
// Las fechas vienen dd/mm/aaaa y quedan en ISO. Los nombres vienen en
// mayúsculas y sin tildes: quedan con mayúscula inicial («Juana Andrea»).
// Los ejemplos de los tests son SINTÉTICOS (test_documentos).
// ============================================================================
#pragma once

#include "documentos/Documento.hpp"

namespace ba::documentos {

class DniArgentino : public TipoDocumento {
   public:
    std::string id() const override { return "dni-ar"; }
    std::string titulo() const override { return "DNI argentino"; }
    std::string comoSeDice() const override { return "tu DNI"; }
    std::vector<std::string> formatos() const override { return {"PDF417", "MRZ"}; }
    bool leer(const Codigo& c, Datos& out) const override;
};

} // namespace ba::documentos
