import QtQuick

Canvas {
    id: root

    required property string shape
    property int squareRadius: 20
    property int starPoints: 5
    property int starInnerRadius: 50
    property int ngonSides: 3
    property color fillColor: "white"

    antialiasing: true
    onShapeChanged: requestPaint()
    onSquareRadiusChanged: requestPaint()
    onStarPointsChanged: requestPaint()
    onStarInnerRadiusChanged: requestPaint()
    onNgonSidesChanged: requestPaint()
    onFillColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function polygonPath(context, points, innerRatio) {
        const cx = width / 2
        const cy = height / 2
        const radius = Math.min(width, height) / 2
        const count = innerRatio > 0 ? points * 2 : points
        for (let index = 0; index < count; ++index) {
            const angle = -Math.PI / 2 + index * 2 * Math.PI / count
            const r = innerRatio > 0 && index % 2 === 1 ? radius * innerRatio : radius
            const x = cx + Math.cos(angle) * r
            const y = cy + Math.sin(angle) * r
            if (index === 0) context.moveTo(x, y)
            else context.lineTo(x, y)
        }
        context.closePath()
    }

    onPaint: {
        const context = getContext("2d")
        context.reset()
        context.fillStyle = fillColor
        context.beginPath()
        if (shape.indexOf("Square") >= 0) {
            const radius = Math.min(width, height) * 0.5 * squareRadius / 100
            context.roundedRect(0, 0, width, height, radius, radius)
        } else if (shape.indexOf("Star") >= 0) {
            polygonPath(context, Math.max(3, starPoints), starInnerRadius / 100)
        } else if (shape.indexOf("Ngon") >= 0) {
            polygonPath(context, Math.max(3, ngonSides), 0)
        } else {
            context.ellipse(0, 0, width, height)
        }
        context.fill()
    }
}
