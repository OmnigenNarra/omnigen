import QtQuick 2.3
import QtQuick.Window 2.14
import QtLocation 5.6
import QtPositioning 5.6

Item {
    id: mainWindow
    visible: true

    signal sendCoords(variant firstCoords, variant secondCoords)
    signal sendMsg(string msg)

    Map {
        id: map
        anchors.fill: parent
        plugin: Plugin { 
        name: "esri" 
        }
        //center: QtPositioning.coordinate(52.23, 21.01) // Warsaw
        center: QtPositioning.coordinate(42.4681, -107.134) // Granite McIntosh Peak
        //center: QtPositioning.coordinate(36.990835, -110.096237) // West and East Mitten Buttes
        
        //center: QtPositioning.coordinate(40.849, 86.853) // Sinciang - Hills, Rug Plains, Desert
        //center: QtPositioning.coordinate(53.23, 6.39) // Niekerk - Plains

        //center: QtPositioning.coordinate(19.65, 53.69) // Arabian Sand Desert - Scattered Moderate Hills
        //center: QtPositioning.coordinate(22.05, 54.43) // Arabian Sand Desert - Scattered High Hills
        //center: QtPositioning.coordinate(49.34, -85.011) // Algoma - Moderate Hills (Irregular Plains)
        //center: QtPositioning.coordinate(51.8213, -115,35) // Alberta-British Columbia Foothills (Open High Hills)
        
        //center: QtPositioning.coordinate(35.38, -120.72) // Santa Margarita Low Mountains
        //center: QtPositioning.coordinate(41.23, -123.36) // Horn Crk, Forks of Salmon - High Mountains
        //center: QtPositioning.coordinate(-17.06, -40.39) // Itanhem - Scattered Low Mountains
        //center: QtPositioning.coordinate(40.1, 20.27) // Qender - Scattered High Mountains

        zoomLevel: 10

        MapRectangle {
            id: marker

            color: "transparent"
            border.color: "black"
            border.width: 2
            opacity: 0.8

            topLeft {
                latitude: 0
                longitude: 0
            }
            bottomRight {
                latitude: 0
                longitude: 0
            }
        }
    }

    MouseArea {
        id: mouseArea
        property bool __isPanning: false
        property bool __isDrawing: false
        property bool __isSwitched: false
        property int __lastX: -1
        property int __lastY: -1
        property variant rectStart: QtPositioning.coordinate(1.0, 2.0)
        property double __rectIncrement: 0.055
        property double __latiDiff: 0.0
        property double __longiDiff: 0.0

        anchors.fill : parent

        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: {
            if(mouseArea.pressedButtons & Qt.LeftButton) {
                __isPanning = true
                __lastX = mouse.x
                __lastY = mouse.y
            }
            else if(mouseArea.pressedButtons & Qt.RightButton) {
                __isDrawing = true
                var coords = map.toCoordinate(Qt.point(mouse.x, mouse.y))
                marker.topLeft = coords
                rectStart = coords
                marker.bottomRight = coords
            }
        }

        onReleased: {
            __isPanning = false
            if(__isDrawing && (__latiDiff != 0.0) && (__longiDiff != 0.0)){
                mainWindow.sendCoords(marker.topLeft, marker.bottomRight)
            }
            else if (__isDrawing){
                mainWindow.sendMsg("No area selected.")
            }

            __isDrawing = false
            __isSwitched = false
        }

        onPositionChanged: {
            if (__isPanning) {
                var dx = mouse.x - __lastX
                var dy = mouse.y - __lastY
                map.pan(-dx, -dy)
                __lastX = mouse.x
                __lastY = mouse.y
            }
            else if(__isDrawing) {
                var coords = map.toCoordinate(Qt.point(mouse.x, mouse.y))
                __latiDiff = ~~((coords.latitude - rectStart.latitude) / __rectIncrement) * __rectIncrement
                __longiDiff = ~~((coords.longitude - rectStart.longitude) / __rectIncrement) * __rectIncrement

                if(__isSwitched && (marker.bottomRight.longitude < coords.longitude)){
                    marker.topLeft = marker.bottomRight

                    __isSwitched = false
                    marker.bottomRight = QtPositioning.coordinate((rectStart.latitude + __latiDiff), (rectStart.longitude + __longiDiff))
                }
                else if(__isSwitched){
                    marker.topLeft = QtPositioning.coordinate((rectStart.latitude + __latiDiff), (rectStart.longitude + __longiDiff))
                }
                else if(rectStart.longitude < coords.longitude){
                    marker.bottomRight = QtPositioning.coordinate((rectStart.latitude + __latiDiff), (rectStart.longitude + __longiDiff))
                }
                else {
                    marker.bottomRight = marker.topLeft
 
                    marker.topLeft = QtPositioning.coordinate((rectStart.latitude + __latiDiff), (rectStart.longitude + __longiDiff))
                    __isSwitched = true
                }
            }
        }

        onCanceled: {
            __isPanning = false
            __isSwitched = false
            __isDrawing = false
        }
    }
}
