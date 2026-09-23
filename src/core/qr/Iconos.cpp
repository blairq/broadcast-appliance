// ============================================================================
// src/core/qr/Iconos.cpp — ver Iconos.hpp
// ============================================================================
#include "qr/Iconos.hpp"

namespace ba::qr {

const std::vector<Icono>& iconos() {
    // Lienzo de 24x24, trazos sin relleno (grosor 2, puntas redondeadas), en
    // el formato de Lucide. Para agregar uno nuevo: pegar su `d` acá.
    static const std::vector<Icono> v = {
        {"wifi", "WiFi",
         "M5 13a10 10 0 0 1 14 0M8.5 16.5a5 5 0 0 1 7 0M2 8.82a15 15 0 0 1 20 0M12 20h.01"},
        {"enlace", "Enlace",
         "M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"
         "M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"},
        {"persona", "Persona", "M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2M12 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8z"},
        {"telefono", "Teléfono",
         "M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6"
         "A19.79 19.79 0 0 1 2.12 4.18 2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72c.13.96.36 1.9.7 2.81"
         "a2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45"
         "c.91.34 1.85.57 2.81.7A2 2 0 0 1 22 16.92z"},
        {"correo", "Correo", "M4 4h16a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM22 7l-10 6L2 7"},
        {"calendario", "Calendario",
         "M8 2v4M16 2v4M3 6h18a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1zM2 11h20"},
        {"camara", "Cámara",
         "M23 7l-7 5 7 5V7zM1 7a2 2 0 0 1 2-2h11a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V7z"},
        {"microfono", "Micrófono",
         "M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3zM19 10v2a7 7 0 0 1-14 0v-2M12 19v4M8 23h8"},
        {"reproducir", "Reproducir", "M6 3l14 9-14 9V3z"},
        {"pantalla", "Pantalla",
         "M3 4h18a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM8 21h8M12 17v4"},
        {"lugar", "Lugar", "M20 10c0 6-8 12-8 12s-8-6-8-12a8 8 0 0 1 16 0zM12 7a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"},
        {"entrada", "Entrada",
         "M2 9a3 3 0 0 1 0 6v3a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-3a3 3 0 0 1 0-6V6a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v3z"
         "M13 5v2M13 11v2M13 17v2"},
        {"informacion", "Información", "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM12 16v-4M12 8h.01"},
        {"estrella", "Estrella", "M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14l-5-4.87 6.91-1.01L12 2z"},
        {"corazon", "Corazón",
         "M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23"
         "l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"},
        {"carrito", "Carrito",
         "M9 22a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM20 22a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM1 1h4l2.68 13.39"
         "a2 2 0 0 0 2 1.61h9.72a2 2 0 0 0 2-1.61L23 6H6"},
        {"documento", "Documento", "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zM14 2v6h6M8 13h8M8 17h8"},
        {"casa", "Casa", "M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V9zM9 22V12h6v10"},
        {"globo", "Mundo", "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM2 12h20M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"},
        {"candado", "Candado",
         "M5 11h14a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1zM8 11V7a4 4 0 0 1 8 0v4"},
    };
    return v;
}

const Icono* icono(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const Icono& i : iconos())
        if (i.id == id) return &i;
    return nullptr;
}

} // namespace ba::qr
