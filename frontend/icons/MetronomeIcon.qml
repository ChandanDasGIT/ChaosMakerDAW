import QtQuick

// A small line drawing of a metronome: a tapered body, a base and a tilted pendulum with its weight.
Canvas {
    id: root
    property color color: Theme.text
    implicitWidth: 18
    implicitHeight: 18

    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        ctx.strokeStyle = root.color
        ctx.fillStyle = root.color
        ctx.lineWidth = Math.max(1.2, w / 12)
        ctx.lineJoin = "round"
        ctx.lineCap = "round"

        // Body: narrow at the top, wide at the bottom.
        ctx.beginPath()
        ctx.moveTo(w * 0.38, h * 0.10)
        ctx.lineTo(w * 0.62, h * 0.10)
        ctx.lineTo(w * 0.82, h * 0.80)
        ctx.lineTo(w * 0.18, h * 0.80)
        ctx.closePath()
        ctx.stroke()

        // Solid base.
        ctx.fillRect(w * 0.10, h * 0.80, w * 0.80, h * 0.12)

        // Pendulum from the pivot near the base, leaning right past the top.
        ctx.beginPath()
        ctx.moveTo(w * 0.50, h * 0.70)
        ctx.lineTo(w * 0.80, h * 0.04)
        ctx.stroke()

        // Sliding weight on the pendulum.
        ctx.fillRect(w * 0.58, h * 0.36, w * 0.16, h * 0.10)
    }
}
