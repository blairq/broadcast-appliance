// Tarjeta "Sistema": medidor de CPU + fps, codificador, frames perdidos.
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import BA
import "../controls"

Item {
    RowLayout {
        anchors.fill: parent
        spacing: 12

        // Medidor circular de CPU
        Item {
            Layout.preferredWidth: Math.min(parent.height, 150)
            Layout.fillHeight: true
            readonly property real frac: Math.min(1, Studio.cpuPercent / 100)
            id: gauge
            Shape {
                anchors.fill: parent
                layer.enabled: true
                layer.samples: 4
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.1); strokeWidth: 10; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: gauge.width / 2; centerY: gauge.height / 2
                                   radiusX: Math.min(gauge.width, gauge.height) / 2 - 8; radiusY: radiusX
                                   startAngle: 135; sweepAngle: 270 }
                }
                ShapePath {
                    strokeColor: gauge.frac > 0.8 ? Theme.live : (gauge.frac > 0.6 ? Theme.amber : Theme.accent)
                    strokeWidth: 10; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: gauge.width / 2; centerY: gauge.height / 2
                                   radiusX: Math.min(gauge.width, gauge.height) / 2 - 8; radiusY: radiusX
                                   startAngle: 135; sweepAngle: 270 * gauge.frac
                                   Behavior on sweepAngle { NumberAnimation { duration: 300 } } }
                }
            }
            Column {
                anchors.centerIn: parent
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Math.round(Studio.cpuPercent) + "%"
                       color: Theme.text; font { family: Theme.font; pixelSize: 26; weight: Font.Light } }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "CPU"; color: Theme.textFaint; font.pixelSize: 10 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10; rowSpacing: 6
            Text { text: "FPS"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: Studio.fps.toFixed(1); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text { text: "Codificador"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: Studio.encoder || "se elige al grabar o transmitir"; color: Studio.encoder ? Theme.text : Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
            Text { text: "Perdidos"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: Studio.droppedFrames + " / " + Studio.totalFrames; color: Studio.droppedFrames > 0 ? Theme.warn : Theme.text; font.pixelSize: 12 }
            Text { text: "Lienzo"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: Studio.canvasLabel; color: Theme.text; font.pixelSize: 12 }
            Text { text: "Gateway"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: Studio.gatewayAlive ? "en linea" : "sin respuesta"; color: Studio.gatewayAlive ? Theme.ok : Theme.live; font.pixelSize: 12 }
        }
    }
}
