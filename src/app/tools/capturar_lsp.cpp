// src/app/tools/capturar_lsp.cpp — la captura de la interfaz de un plugin CLAP
//
// Para las capturas de third_party/lsp-plugins-matrix-mixer (las de su
// documentación y sus íconos, como las de los módulos oficiales de LSP): abre
// el plugin con el host CLAP de la app, le pone parámetros de ejemplo, le pasa
// audio de prueba (tonos que suben y bajan, para que los medidores se muevan)
// y guarda en PNG el último cuadro que dibujó su interfaz (sin leer la
// pantalla: ver capturar_vista_mac.mm). Solo plugins de LSP.
//
//   capturar_lsp --clap lsp-plugins.clap --id in.lsp-plug.matrix_mixer_x8_stereo \
//                --salida captura.png [--esperar 2500] [--param "Matrix gain 1 to 2=-6"]...
//
// No es un test: es una herramienta (tools/lsp-matrix-mixer-doc.py la usa).
// (`--id` y no `--plugin`: Qt se queda con `-plugin` como opción propia.)

#include "audio/ClapHost.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QWindow>

#include <numbers>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>

using namespace ba::app::audio;

bool engancharCuadrosLsp(); // capturar_vista_mac.mm
QImage ultimoCuadroLsp();
const char* quienTieneElTeclado(void* nsview);
void dobleClick(void* nsview, double x, double y);

namespace {

/// Los servicios del hilo principal que pide el editor (timers, fds), con Qt.
class Servicios : public HostServices {
   public:
    void runOnMain(std::function<void()> fn) override {
        QMetaObject::invokeMethod(qApp, [fn]() { fn(); }, Qt::QueuedConnection);
    }
    int addTimer(int periodMs, std::function<void()> fn) override {
        auto* t = new QTimer();
        t->setInterval(periodMs);
        QObject::connect(t, &QTimer::timeout, qApp, [fn]() { fn(); });
        t->start();
        timers_[++next_] = t;
        return next_;
    }
    void removeTimer(int id) override {
        if (auto it = timers_.find(id); it != timers_.end()) {
            it->second->stop();
            it->second->deleteLater();
            timers_.erase(it);
        }
    }
    bool addFd(int fd, uint32_t flags, std::function<void(uint32_t)> fn) override {
        removeFd(fd);
        if (flags & 1u) {
            auto* n = new QSocketNotifier(fd, QSocketNotifier::Read);
            QObject::connect(n, &QSocketNotifier::activated, qApp, [fn]() { fn(1u); });
            fds_[fd].push_back(n);
        }
        if (flags & 2u) {
            auto* n = new QSocketNotifier(fd, QSocketNotifier::Write);
            QObject::connect(n, &QSocketNotifier::activated, qApp, [fn]() { fn(2u); });
            fds_[fd].push_back(n);
        }
        return true;
    }
    void removeFd(int fd) override {
        if (auto it = fds_.find(fd); it != fds_.end()) {
            for (auto* n : it->second)
                n->deleteLater();
            fds_.erase(it);
        }
    }

   private:
    int next_ = 0;
    std::map<int, QTimer*> timers_;
    std::map<int, std::vector<QSocketNotifier*>> fds_;
};

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    std::string clap, plugin, salida;
    int esperar = 2500;
    std::vector<std::pair<std::string, std::string>> params;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--clap")
            clap = v;
        else if (k == "--id")
            plugin = v;
        else if (k == "--salida")
            salida = v;
        else if (k == "--esperar")
            esperar = std::atoi(v.c_str());
        else if (k == "--param" && v.find('=') != std::string::npos)
            params.emplace_back(v.substr(0, v.find('=')), v.substr(v.find('=') + 1));
    }
    if (clap.empty() || plugin.empty() || salida.empty()) {
        std::fprintf(
            stderr,
            "uso: capturar_lsp --clap <.clap> --id <id> --salida <png> [--esperar ms] [--param n=v]\n");
        return 2;
    }
    std::string err;
    auto lib = ClapLibrary::open(clap, &err);
    if (!lib) {
        std::fprintf(stderr, "no se pudo abrir %s: %s\n", clap.c_str(), err.c_str());
        return 1;
    }
    if (!engancharCuadrosLsp()) {
        std::fprintf(stderr, "%s no tiene la vista Cocoa de LSP (CocoaCairoView)\n", clap.c_str());
        return 1;
    }
    Servicios svc;
    auto inst = ClapInstance::create(lib, plugin, &svc, &err);
    if (!inst || !inst->activate(48000, 512)) {
        std::fprintf(stderr, "no se pudo crear %s: %s\n", plugin.c_str(), err.c_str());
        return 1;
    }
    // Parámetros de ejemplo, por nombre y en el texto del plugin («-6 dB», «on»).
    for (const auto& [nombre, texto] : params) {
        bool ok = false;
        for (const auto& p : inst->params()) {
            if (p.name != nombre) continue;
            double v = 0;
            ok = inst->textToValue(p.id, texto, v) ? inst->setValue(p.id, v)
                                                   : inst->setValue(p.id, std::atof(texto.c_str()));
        }
        if (!ok) std::fprintf(stderr, "aviso: no se pudo poner «%s» = %s\n", nombre.c_str(), texto.c_str());
    }

    // Audio de prueba en su propio hilo: un tono por entrada, cada uno con su
    // nivel y una envolvente lenta (los medidores se mueven como en vivo).
    std::atomic<bool> salir{false};
    std::thread audio([&]() {
        const uint32_t N = 512;
        const auto& ins = inst->inputs();
        const auto& outs = inst->outputs();
        std::vector<std::vector<std::vector<float>>> bi(ins.size()), bo(outs.size());
        std::vector<std::vector<float*>> pi(ins.size()), po(outs.size());
        std::vector<float* const*> vi, vo;
        for (size_t p = 0; p < ins.size(); ++p) {
            bi[p].assign(ins[p].channels, std::vector<float>(N));
            for (auto& c : bi[p])
                pi[p].push_back(c.data());
            vi.push_back(pi[p].data());
        }
        for (size_t p = 0; p < outs.size(); ++p) {
            bo[p].assign(outs[p].channels, std::vector<float>(N));
            for (auto& c : bo[p])
                po[p].push_back(c.data());
            vo.push_back(po[p].data());
        }
        double t = 0;
        while (!salir) {
            for (size_t p = 0; p < ins.size(); ++p) {
                const double nivel = std::pow(10.0, (-6.0 - 4.0 * static_cast<double>(p)) / 20.0);
                for (size_t c = 0; c < bi[p].size(); ++c)
                    for (uint32_t i = 0; i < N; ++i) {
                        const double s = t + i / 48000.0;
                        const double env = 0.6 + 0.4 * std::sin(2 * std::numbers::pi * (0.3 + 0.07 * p) * s + c);
                        bi[p][c][i] =
                            static_cast<float>(nivel * env * std::sin(2 * std::numbers::pi * (220.0 * (p + 1)) * s));
                    }
            }
            inst->process(vi.data(), vo.data(), N);
            t += N / 48000.0;
            std::this_thread::sleep_for(std::chrono::microseconds(10667));
        }
    });

    // BA_CAPTURA_PRINCIPAL=1: antes, una ventana principal grande (como la de la
    // app), para reproducir cómo abre el editor con otra ventana ya abierta.
    std::unique_ptr<QWindow> principal;
    if (qEnvironmentVariableIsSet("BA_CAPTURA_PRINCIPAL")) {
        principal = std::make_unique<QWindow>();
        principal->setGeometry(QGuiApplication::primaryScreen()->availableGeometry());
        principal->show();
        QCoreApplication::processEvents();
    }
    if (!inst->openEditor(inst->desc().name, &err)) {
        std::fprintf(stderr, "no se pudo abrir el editor: %s\n", err.c_str());
        salir = true;
        audio.join();
        return 1;
    }
    int rc = 1;
    // BA_CAPTURA_DOBLECLICK="x,y": un doble click ahí a la mitad de la espera,
    // y al final se listan los parámetros que cambiaron (prueba de interacción).
    std::map<std::string, double> antes;
    for (const auto& p : inst->params())
        inst->getValue(p.id, antes[p.name]);
    if (const QByteArray dc = qgetenv("BA_CAPTURA_DOBLECLICK"); !dc.isEmpty()) {
        const QList<QByteArray> xy = dc.split(',');
        // BA_CAPTURA_DOBLES=n: n dobles clicks repartidos en la espera.
        const int n = std::max(1, qEnvironmentVariableIntValue("BA_CAPTURA_DOBLES"));
        for (int k = 1; k <= n; ++k)
            QTimer::singleShot(esperar * k / (n + 1), [xy, &inst]() {
                for (const auto& p : inst->params())
                    if (p.name == "Matrix gain 1 to 2") {
                        double v = 0;
                        inst->getValue(p.id, v);
                        std::fprintf(stderr, "  antes del doble click: %g\n", v);
                    }
                for (QWindow* tw : QGuiApplication::topLevelWindows())
                    if (tw->isVisible())
                        dobleClick(reinterpret_cast<void*>(tw->winId()), xy.value(0).toDouble(),
                                   xy.value(1).toDouble());
            });
    }
    QTimer::singleShot(esperar, [&]() {
        for (const auto& p : inst->params()) {
            double v = 0;
            inst->getValue(p.id, v);
            if (std::fabs(v - antes[p.name]) > 1e-6)
                std::fprintf(stderr, "cambio: %s %g -> %g\n", p.name.c_str(), antes[p.name], v);
        }
        // El cuadro viene a la escala de la pantalla (Retina: al doble): se
        // deja al tamaño lógico de la ventana, como las capturas de LSP.
        QImage img = ultimoCuadroLsp();
        for (QWindow* tw : QGuiApplication::topLevelWindows())
            if (tw->isVisible())
                std::fprintf(stderr, "teclado: %s\n",
                             quienTieneElTeclado(reinterpret_cast<void*>(tw->winId())));
        QWindow* w = QGuiApplication::topLevelWindows().isEmpty()
                         ? nullptr
                         : QGuiApplication::topLevelWindows().first();
        if (img.isNull()) {
            std::fprintf(stderr, "la interfaz no dibujo ningun cuadro\n");
        } else {
            if (w != nullptr && img.width() > w->width())
                img = img.scaled(w->width(), w->height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (img.save(QString::fromStdString(salida))) {
                std::printf("%s (%dx%d)\n", salida.c_str(), img.width(), img.height());
                rc = 0;
            }
        }
        inst->closeEditor();
        salir = true;
        audio.join();
        app.quit();
    });
    app.exec();
    return rc;
}
