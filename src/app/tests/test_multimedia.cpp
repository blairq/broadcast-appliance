// ============================================================================
// src/app/tests/test_multimedia.cpp — ¿el Qt vendoreado decodifica?
//
// El visor de Medios (doble clic en un archivo) reproduce con Qt Multimedia, y
// el motor de Qt Multimedia es ffmpeg ENLAZADO ADENTRO de su plugin. Eso lo
// hace portable —no pide ffmpeg del sistema ni VLC— pero lo vuelve invisible:
// si el plugin no viaja en el bundle, o si un dia se lo cambia de lugar, no
// falla al compilar ni al arrancar. Falla cuando un operador hace doble clic
// en un video, en vivo, y no pasa nada.
//
// Esto abre un video de un segundo y comprueba que sepa su duracion y que
// tiene imagen y sonido. Es poco y es exactamente lo que hace falta: prueba
// que el plugin esta, que carga y que decodifica.
// ============================================================================
#include <cstdio>

#include <QAudioOutput>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTimer>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString archivo = QString(BA_TEST_DATOS) + "/un-segundo.mp4";
    if (!QFileInfo::exists(archivo)) {
        std::printf("FALLO: falta el archivo de prueba %s\n", qPrintable(archivo));
        return 1;
    }

    QMediaPlayer p;
    QAudioOutput salida; // sin esto no hay pista de audio que mirar
    p.setAudioOutput(&salida);

    int fallos = 1;
    QString motivo = "no contesto a tiempo";
    QObject::connect(&p, &QMediaPlayer::mediaStatusChanged, [&](QMediaPlayer::MediaStatus s) {
        if (s != QMediaPlayer::LoadedMedia && s != QMediaPlayer::BufferedMedia) return;
        fallos = 0;
        // Un segundo, con los dos canales. Los margenes son anchos a proposito:
        // lo que se prueba es que decodifica, no cuanto mide al milisegundo.
        if (p.duration() < 500 || p.duration() > 2000) {
            std::printf("FALLO: duracion rara: %lld ms\n", p.duration());
            ++fallos;
        }
        if (!p.hasVideo()) { std::printf("FALLO: no ve la imagen\n"); ++fallos; }
        if (!p.hasAudio()) { std::printf("FALLO: no oye el sonido\n"); ++fallos; }
        app.quit();
    });
    QObject::connect(&p, &QMediaPlayer::errorOccurred, [&](QMediaPlayer::Error, const QString& t) {
        motivo = t;
        app.quit();
    });

    p.setSource(QUrl::fromLocalFile(archivo));
    p.play();
    QTimer::singleShot(20000, &app, [&] { app.quit(); });
    app.exec();

    if (fallos == 1 && p.duration() == 0) {
        // Ni cargo ni fallo: es el caso de «no hay motor de reproduccion».
        std::printf("FALLO: %s (¿falta el plugin multimedia/ffmpeg?)\n", qPrintable(motivo));
        return 1;
    }
    if (fallos == 0) std::printf("test_multimedia: OK\n");
    return fallos == 0 ? 0 : 1;
}
