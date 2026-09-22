import QtQuick
Item {
    id: r
    width: 400; height: 100
    Rectangle {
        id: caja
        width: 80; height: 80; y: 10
        color: "#ff6a2b"
        x: 0
        NumberAnimation on x { from: 0; to: 300; duration: 1000; running: true }
    }
}
