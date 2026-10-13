// src/app/arranque/ModoVivoConfig.cpp — ver el .hpp

#include "arranque/ModoVivoConfig.hpp"

#include "arranque/ArchivoConfig.hpp"

namespace ba::app::config {

ModoVivoConfig::ModoVivoConfig(std::filesystem::path dataRoot)
    : archivo_(std::move(dataRoot) / "Config" / "modo-vivo.json") {
    aplicar(leerObjeto(archivo_));
}

void ModoVivoConfig::aplicar(const nlohmann::json& a) {
    if (a.contains("activo")) activo_ = a.value("activo", true);
    if (a.contains("incluir_grabando")) incluirGrabando_ = a.value("incluir_grabando", false);
    if (a.contains("paginas") && a["paginas"].is_array()) {
        paginas_.clear();
        for (const auto& p : a["paginas"])
            if (p.is_string()) paginas_ << QString::fromStdString(p.get<std::string>());
    }
}

std::string ModoVivoConfig::cambiar(const nlohmann::json& a) {
    aplicar(a);
    nlohmann::json p = nlohmann::json::array();
    for (const QString& x : paginas_) p.push_back(x.toStdString());
    if (!guardar(archivo_, {{"activo", activo_}, {"incluir_grabando", incluirGrabando_}, {"paginas", p}}))
        return "rige, pero no se pudo guardar modo-vivo.json";
    return {};
}

} // namespace ba::app::config
