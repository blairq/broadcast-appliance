// ============================================================================
// src/core/rostros/Rostros.cpp — ver Rostros.hpp
// ============================================================================
#include "diag/Utf8.hpp"
#include "rostros/Rostros.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>

// La API de ONNX Runtime se pide A MANO: si el proceso cargó otro ONNX Runtime
// (en Windows, System32 trae uno viejo que gana a PATH), la que pide este código
// no está, y sin esto el puntero queda nulo y la primera llamada tira abajo todo
// el proceso. Así el motor de rostros queda cerrado con un error y nada más.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

namespace ba::rostros {
namespace {

/// La entrada de SFace: 112x112, que es como se entrenó. No es negociable.
constexpr int kLadoCara = 112;

/// Donde van los cinco puntos en esa imagen de 112x112.
///
/// SON LAS DE ARCFACE, las mismas con las que se entrenó el modelo. Enderezar
/// hacia otras posiciones da vectores que no se parecen a los del modelo y el
/// reconocimiento deja de funcionar sin que nada falle.
constexpr float kDestino[5][2] = {
    {38.2946f, 51.6963f}, // ojo derecho
    {73.5318f, 51.5014f}, // ojo izquierdo
    {56.0252f, 71.7366f}, // nariz
    {41.5493f, 92.3655f}, // comisura derecha
    {70.7299f, 92.2041f}, // comisura izquierda
};

/// Enderezar la cara: rotarla, escalarla y centrarla como espera el modelo.
///
/// ES EL PASO QUE DECIDE SI ESTO SIRVE. Sin enderezar, la misma persona de
/// perfil y de frente da vectores distintos y no se reconoce a sí misma. Se
/// resuelve con una semejanza (rotación + escala + traslación, sin deformar)
/// calculada por mínimos cuadrados entre los cinco puntos detectados y los
/// cinco de arriba. Una transformación que deformara la cara para encajar los
/// puntos exactos haría lo contrario de lo que se busca: dos caras distintas
/// se parecerían más.
void enderezar(const std::uint8_t* bgra, int ancho, int alto, const float* puntos,
               std::vector<std::uint8_t>& salida) {
    // Centroides.
    double sx = 0, sy = 0, dx = 0, dy = 0;
    for (int i = 0; i < 5; ++i) {
        sx += puntos[i * 2];
        sy += puntos[i * 2 + 1];
        dx += kDestino[i][0];
        dy += kDestino[i][1];
    }
    sx /= 5;
    sy /= 5;
    dx /= 5;
    dy /= 5;

    // Escala y rotación (Umeyama simplificado: sin reflexión, sin deformar).
    double varOrigen = 0, a = 0, b = 0;
    for (int i = 0; i < 5; ++i) {
        const double ox = puntos[i * 2] - sx, oy = puntos[i * 2 + 1] - sy;
        const double tx = kDestino[i][0] - dx, ty = kDestino[i][1] - dy;
        varOrigen += ox * ox + oy * oy;
        a += ox * tx + oy * ty;
        b += ox * ty - oy * tx;
    }
    if (varOrigen < 1e-6) varOrigen = 1e-6;
    const double cosS = a / varOrigen; // escala * cos(angulo)
    const double sinS = b / varOrigen; // escala * sen(angulo)

    // La inversa, para recorrer el destino y muestrear el origen (así no
    // quedan agujeros, que es lo que pasa recorriendo el origen).
    const double det = cosS * cosS + sinS * sinS;
    const double iC = cosS / det, iS = -sinS / det;

    salida.assign(static_cast<std::size_t>(kLadoCara) * kLadoCara * 3, 0);
    for (int y = 0; y < kLadoCara; ++y) {
        for (int x = 0; x < kLadoCara; ++x) {
            const double px = x - dx, py = y - dy;
            const int ox = static_cast<int>(std::lround(iC * px - iS * py + sx));
            const int oy = static_cast<int>(std::lround(iS * px + iC * py + sy));
            if (ox < 0 || oy < 0 || ox >= ancho || oy >= alto) continue;
            const std::uint8_t* p = bgra + (static_cast<std::size_t>(oy) * ancho + ox) * 4;
            std::uint8_t* q = salida.data() + (static_cast<std::size_t>(y) * kLadoCara + x) * 3;
            q[0] = p[0]; // B
            q[1] = p[1]; // G
            q[2] = p[2]; // R
        }
    }
}

/// YuNet tiene la entrada FIJA en 640x640: no acepta el cuadro como venga.
constexpr int kLadoDet = 640;

/// Mete el cuadro en 640x640 sin deformarlo, con barras, y dice como volver.
///
/// SIN DEFORMAR, a proposito: estirar una cara para que entre cambia las
/// proporciones que el modelo aprendio y empeora la deteccion justo en los
/// casos dificiles (alguien de costado, lejos). Se escala por el lado que
/// sobra y el resto se rellena con negro.
///
/// :param escala,offX,offY: reciben como volver de la imagen chica a la real.
void encajar(const std::uint8_t* bgra, int ancho, int alto, std::vector<float>& destino, double& escala,
             int& offX, int& offY) {
    escala = std::min(static_cast<double>(kLadoDet) / ancho, static_cast<double>(kLadoDet) / alto);
    const int w = std::max(1, static_cast<int>(ancho * escala));
    const int h = std::max(1, static_cast<int>(alto * escala));
    offX = (kLadoDet - w) / 2;
    offY = (kLadoDet - h) / 2;

    const std::size_t plano = static_cast<std::size_t>(kLadoDet) * kLadoDet;
    destino.assign(plano * 3, 0.0f);
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(alto - 1, static_cast<int>(y / escala));
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(ancho - 1, static_cast<int>(x / escala));
            const std::uint8_t* p = bgra + (static_cast<std::size_t>(sy) * ancho + sx) * 4;
            const std::size_t i = static_cast<std::size_t>(y + offY) * kLadoDet + (x + offX);
            destino[i] = p[0];
            destino[plano + i] = p[1];
            destino[2 * plano + i] = p[2];
        }
    }
}

/// Traduce la salida cruda de YuNet a rectangulos con sus cinco puntos.
///
/// POR NOMBRE Y NO POR POSICION. YuNet saca doce tensores (`cls_8`, `obj_8`,
/// `bbox_8`, `kps_8`, y lo mismo para 16 y 32). Leerlos por el orden en que
/// vienen funciona hasta que una version del modelo los reordena, y ahi no
/// falla nada: simplemente deja de encontrar caras. Preguntando el nombre,
/// un modelo que no encaje se nota al abrirlo.
///
/// Cada escala mira la imagen en una grilla de `alto/s` x `ancho/s`. Cada
/// celda propone una cara; la confianza es la raiz de (clase x objeto), que
/// es como la calcula el propio modelo.
std::vector<Cara> decodificar(const std::vector<std::string>& nombres, const std::vector<Ort::Value>& salidas,
                              float minConfianza) {
    const auto buscar = [&](const std::string& n) -> const float* {
        for (std::size_t i = 0; i < nombres.size(); ++i)
            if (nombres[i] == n) return salidas[i].GetTensorData<float>();
        return nullptr;
    };
    std::vector<Cara> caras;
    for (const int s : {8, 16, 32}) {
        const std::string suf = "_" + std::to_string(s);
        const float* cls = buscar("cls" + suf);
        const float* obj = buscar("obj" + suf);
        const float* box = buscar("bbox" + suf);
        const float* kps = buscar("kps" + suf);
        if (cls == nullptr || obj == nullptr || box == nullptr || kps == nullptr) continue;
        const int cols = kLadoDet / s;
        const int filas = kLadoDet / s;
        for (int r = 0; r < filas; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int i = r * cols + c;
                const float conf = std::sqrt(std::max(0.0f, cls[i]) * std::max(0.0f, obj[i]));
                if (conf < minConfianza) continue;
                Cara f;
                const float cx = (c + box[i * 4 + 0]) * s;
                const float cy = (r + box[i * 4 + 1]) * s;
                const float w = std::exp(box[i * 4 + 2]) * s;
                const float h = std::exp(box[i * 4 + 3]) * s;
                f.x = static_cast<int>(cx - w / 2);
                f.y = static_cast<int>(cy - h / 2);
                f.ancho = static_cast<int>(w);
                f.alto = static_cast<int>(h);
                f.confianza = conf;
                for (int k = 0; k < 5; ++k) {
                    f.puntos[k * 2] = (c + kps[i * 10 + k * 2]) * s;
                    f.puntos[k * 2 + 1] = (r + kps[i * 10 + k * 2 + 1]) * s;
                }
                caras.push_back(f);
            }
        }
    }
    // LA MISMA CARA APARECE VARIAS VECES, en celdas vecinas y en las tres
    // escalas. Se queda la de mas confianza y se descartan las que se le
    // superponen: sin esto, una persona sola entra cinco veces a la lista.
    std::sort(caras.begin(), caras.end(),
              [](const Cara& a, const Cara& b) { return a.confianza > b.confianza; });
    std::vector<Cara> quedan;
    for (const Cara& f : caras) {
        bool repetida = false;
        for (const Cara& q : quedan) {
            const int x1 = std::max(f.x, q.x), y1 = std::max(f.y, q.y);
            const int x2 = std::min(f.x + f.ancho, q.x + q.ancho);
            const int y2 = std::min(f.y + f.alto, q.y + q.alto);
            const int inter = std::max(0, x2 - x1) * std::max(0, y2 - y1);
            const int uni = f.ancho * f.alto + q.ancho * q.alto - inter;
            if (uni > 0 && static_cast<float>(inter) / uni > 0.3f) {
                repetida = true;
                break;
            }
        }
        if (!repetida) quedan.push_back(f);
    }
    return quedan;
}

} // namespace

float parecido(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0;
    double punto = 0, na = 0, nb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        punto += static_cast<double>(a[i]) * b[i];
        na += static_cast<double>(a[i]) * a[i];
        nb += static_cast<double>(b[i]) * b[i];
    }
    if (na <= 0 || nb <= 0) return 0;
    return static_cast<float>(punto / (std::sqrt(na) * std::sqrt(nb)));
}

namespace {
/// La API de ONNX Runtime de este proceso, o null si la que hay es de otra versión.
const OrtApi* apiOrt() {
    static const OrtApi* api = [] {
        const OrtApi* a = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (a != nullptr) Ort::InitApi(a);
        return a;
    }();
    return api;
}
} // namespace

struct Motor::Impl {
    // Se crean solo si la API está (ver apiOrt).
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::SessionOptions> opciones;
    std::unique_ptr<Ort::Session> detector;
    std::unique_ptr<Ort::Session> descriptor;
    std::unique_ptr<Ort::MemoryInfo> memoriaPtr;
    Ort::MemoryInfo& memoria() const { return *memoriaPtr; }
    mutable std::string error;
    // Los nombres de las entradas/salidas los dice el modelo: preguntarlos
    // evita que un modelo nuevo con otros nombres rompa en silencio.
    std::string entradaDet, entradaDes;
    std::vector<std::string> salidasDet;
    std::string salidaDes;
};

Motor::Motor() : d_(std::make_unique<Impl>()) {
    // Un hilo: esto corre al lado del motor de video, que es quien tiene que
    // llegar a tiempo. Una cara tarda milisegundos igual.
    if (apiOrt() == nullptr) {
        d_->error = "el ONNX Runtime cargado en el programa no es el suyo (otra versión: en Windows, el de System32): "
                    "sin reconocimiento de caras";
        return;
    }
    try {
        d_->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "ba-rostros");
        d_->opciones = std::make_unique<Ort::SessionOptions>();
        d_->opciones->SetIntraOpNumThreads(1);
        d_->opciones->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        d_->memoriaPtr = std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    } catch (const Ort::Exception& e) {
        d_->error = std::string("ONNX Runtime: ") + ba::diag::utf8Valido(e.what()); // ONNX Runtime da el mensaje en la página de códigos del sistema
        d_->env.reset();
    }
}
Motor::~Motor() = default;

bool Motor::abierto() const { return d_->detector && d_->descriptor; }
const std::string& Motor::error() const { return d_->error; }

bool Motor::abrir(const std::string& carpetaModelos) {
    namespace fs = std::filesystem;
    if (!d_->env) return false; // sin ONNX Runtime usable: el error ya lo dice
    const fs::path yunet = fs::path(carpetaModelos) / "yunet.onnx";
    const fs::path sface = fs::path(carpetaModelos) / "sface.onnx";
    for (const fs::path& p : {yunet, sface}) {
        if (!fs::exists(p)) {
            d_->error = "falta el modelo " + p.string() + " (correr `make deps-rostros`)";
            return false;
        }
    }
    try {
        d_->detector = std::make_unique<Ort::Session>(*d_->env, yunet.c_str(), *d_->opciones);
        d_->descriptor = std::make_unique<Ort::Session>(*d_->env, sface.c_str(), *d_->opciones);
        Ort::AllocatorWithDefaultOptions al;
        d_->entradaDet = d_->detector->GetInputNameAllocated(0, al).get();
        d_->entradaDes = d_->descriptor->GetInputNameAllocated(0, al).get();
        d_->salidaDes = d_->descriptor->GetOutputNameAllocated(0, al).get();
        for (std::size_t i = 0; i < d_->detector->GetOutputCount(); ++i)
            d_->salidasDet.emplace_back(d_->detector->GetOutputNameAllocated(i, al).get());
    } catch (const Ort::Exception& e) {
        d_->error = std::string("no se pudieron cargar los modelos: ") + ba::diag::utf8Valido(e.what()); // ONNX Runtime da el mensaje en la página de códigos del sistema
        d_->detector.reset();
        d_->descriptor.reset();
        return false;
    }
    d_->error.clear();
    return true;
}

std::vector<Cara> Motor::detectar(const std::uint8_t* bgra, int ancho, int alto, float minConfianza) const {
    std::vector<Cara> caras;
    if (!abierto() || bgra == nullptr || ancho <= 0 || alto <= 0) return caras;
    try {
        std::vector<float> entrada;
        double escala = 1;
        int offX = 0, offY = 0;
        encajar(bgra, ancho, alto, entrada, escala, offX, offY);
        const std::array<std::int64_t, 4> forma{1, 3, kLadoDet, kLadoDet};
        Ort::Value t = Ort::Value::CreateTensor<float>(d_->memoria(), entrada.data(), entrada.size(),
                                                       forma.data(), forma.size());
        std::vector<const char*> nSal;
        for (const std::string& s : d_->salidasDet)
            nSal.push_back(s.c_str());
        const char* nEnt[] = {d_->entradaDet.c_str()};
        auto salidas = d_->detector->Run(Ort::RunOptions{nullptr}, nEnt, &t, 1, nSal.data(), nSal.size());
        caras = decodificar(d_->salidasDet, salidas, minConfianza);
        // De la imagen encajada de vuelta a la real.
        for (Cara& f : caras) {
            f.x = static_cast<int>((f.x - offX) / escala);
            f.y = static_cast<int>((f.y - offY) / escala);
            f.ancho = static_cast<int>(f.ancho / escala);
            f.alto = static_cast<int>(f.alto / escala);
            for (int k = 0; k < 5; ++k) {
                f.puntos[k * 2] = static_cast<float>((f.puntos[k * 2] - offX) / escala);
                f.puntos[k * 2 + 1] = static_cast<float>((f.puntos[k * 2 + 1] - offY) / escala);
            }
        }
    } catch (const Ort::Exception& e) {
        // SE ANOTA EL MOTIVO. Devolver «no hay caras» cuando en realidad
        // fallo la llamada es la peor forma de fallar: la interfaz muestra
        // «no reconoci a nadie» y nadie se entera de que el modelo ni corrio.
        d_->error = std::string("al detectar: ") + ba::diag::utf8Valido(e.what()); // ONNX Runtime da el mensaje en la página de códigos del sistema
        return {};
    }
    return caras;
}

std::vector<float> Motor::describir(const std::uint8_t* bgra, int ancho, int alto, const Cara& cara) const {
    if (!abierto() || bgra == nullptr) return {};
    try {
        std::vector<std::uint8_t> recta;
        enderezar(bgra, ancho, alto, cara.puntos, recta);
        // SFace toma NCHW BGR sin normalizar (0..255), 112x112.
        std::vector<float> entrada(static_cast<std::size_t>(3) * kLadoCara * kLadoCara);
        const std::size_t plano = static_cast<std::size_t>(kLadoCara) * kLadoCara;
        for (std::size_t i = 0; i < plano; ++i) {
            entrada[i] = recta[i * 3];
            entrada[plano + i] = recta[i * 3 + 1];
            entrada[2 * plano + i] = recta[i * 3 + 2];
        }
        const std::array<std::int64_t, 4> forma{1, 3, kLadoCara, kLadoCara};
        Ort::Value t = Ort::Value::CreateTensor<float>(d_->memoria(), entrada.data(), entrada.size(),
                                                       forma.data(), forma.size());
        const char* nEnt[] = {d_->entradaDes.c_str()};
        const char* nSal[] = {d_->salidaDes.c_str()};
        auto salidas = d_->descriptor->Run(Ort::RunOptions{nullptr}, nEnt, &t, 1, nSal, 1);
        const float* v = salidas[0].GetTensorData<float>();
        const std::size_t n = salidas[0].GetTensorTypeAndShapeInfo().GetElementCount();
        return std::vector<float>(v, v + n);
    } catch (const Ort::Exception& e) {
        d_->error = std::string("al describir: ") + ba::diag::utf8Valido(e.what()); // ONNX Runtime da el mensaje en la página de códigos del sistema
        return {};
    }
}

} // namespace ba::rostros
