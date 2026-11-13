import QtQuick

// A small line drawing of a horseshoe magnet (snap to grid), in one colour like the other tool icons.
Canvas {
    id: root
    property color color: Theme.text
    implicitWidth: 16
    implicitHeight: 16

    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        const line = Math.max(2, w / 6)
        ctx.strokeStyle = root.color
        ctx.fillStyle = root.color
        ctx.lineWidth = line
        ctx.lineCap = "butt"

        // The U: two legs joined by a half circle at the bottom.
        const left = w * 0.24, right = w * 0.76, top = h * 0.12, bend = h * 0.58
        const radius = (right - left) / 2
        ctx.beginPath()
        ctx.moveTo(left, top)
        ctx.lineTo(left, bend)
        ctx.arc(w / 2, bend, radius, Math.PI, 0, true)
        ctx.lineTo(right, top)
        ctx.stroke()

        // The pole tips, solid.
        const tip = h * 0.16
        ctx.fillRect(left - line / 2, top, line, tip)
        ctx.fillRect(right - line / 2, top, line, tip)
    }
}
