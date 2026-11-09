// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "theme.js" as Theme
import QtQuick.Controls

// Single-day glucose curve in the same visual language as ProfileChart:
// fixed 0–20 mmol/l axis, 1 mmol/l grid, target band, hour labels and hover.
Item {
    id: root
    required property var chartData
    readonly property int leftMargin: 48
    readonly property int rightMargin: 16
    readonly property int topMargin: 18
    readonly property int bottomMargin: 42
    property var hoveredPoint: null
    property int paintedPoints: canvas.paintedPoints
    implicitHeight: 260
    onChartDataChanged: canvas.requestPaint()
    function plotWidth() { return width - leftMargin - rightMargin }
    function plotHeight() { return height - topMargin - bottomMargin }
    function xFor(t) {
        const w = plotWidth(), from = root.chartData.from || 0, to = root.chartData.to || 0
        return leftMargin + (to > from ? Math.max(0, Math.min(1, (t - from) / (to - from))) : 0) * w
    }
    function yFor(v) {
        const h = plotHeight()
        return h <= 0 ? topMargin : topMargin + h - Math.min(v, 20) / 20 * h
    }
    function nearest(mouseX) {
        const from = root.chartData.from || 0, to = root.chartData.to || 0, w = plotWidth()
        if (w <= 0 || !(to > from)) return null
        const t = from + Math.max(0, Math.min(1, (mouseX - leftMargin) / w)) * (to - from)
        let best = null, bestDistance = Infinity
        for (const segment of (root.chartData.segments || []))
            for (const point of segment) {
                const distance = Math.abs(point.x - t)
                if (distance < bestDistance) { bestDistance = distance; best = point }
            }
        return best
    }
    function targetsSet() {
        const lo = Number(root.chartData.low), hi = Number(root.chartData.high)
        return lo > 0 && hi > lo
    }
    function pointTimeText(point) { return point ? (point.timeText || "") : "" }
    function colorForValue(value) {
        if (!targetsSet()) return Theme.accentTeal
        const lo = Number(root.chartData.low), hi = Number(root.chartData.high)
        return value < lo || value > hi ? Theme.alertRed : Theme.accentTeal
    }
    Canvas {
        id: canvas
        objectName: "currentDayCanvas"
        anchors.fill: parent
        property int paintedPoints: 0
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            const l = root.leftMargin, r = root.rightMargin, t = root.topMargin, b = root.bottomMargin
            const w = width - l - r, h = height - t - b
            if (w <= 0 || h <= 0) return
            const maxY = 20
            const x = root.xFor, y = root.yFor
            ctx.fillStyle = Theme.bg; ctx.fillRect(0, 0, width, height)
            const lo = root.chartData.low, hi = root.chartData.high
            if (lo > 0 && hi > lo) {
                ctx.fillStyle = Theme.chartTargetFill; ctx.fillRect(l, y(hi), w, y(lo) - y(hi))
                ctx.strokeStyle = Theme.chartTargetEdge; ctx.lineWidth = 1; ctx.strokeRect(l, y(hi), w, y(lo) - y(hi))
            }
            ctx.strokeStyle = Theme.chartGrid; ctx.lineWidth = .5; ctx.fillStyle = Theme.chartAxisText; ctx.font = "9px sans-serif"
            for (let v = 0; v <= maxY; v += 1) {
                ctx.beginPath(); ctx.moveTo(l, y(v)); ctx.lineTo(l + w, y(v)); ctx.stroke()
                ctx.fillText(String(v), 12, y(v) + 3)
            }
            ctx.font = "12px sans-serif"
            const from = root.chartData.from || 0, to = root.chartData.to || 0
            if (to > from) {
                for (const tick of (root.chartData.ticks || [])) {
                    const ratio = (tick.time - from) / (to - from)
                    if (ratio >= 0 && ratio <= 1) ctx.fillText(tick.label, l + ratio * w - 7, height - 19)
                }
            }
            ctx.fillText("mmol/l", 4, 12)
            paintedPoints = 0
            const low = Number(root.chartData.low), high = Number(root.chartData.high), targets = root.targetsSet()
            ctx.lineWidth = 2; ctx.lineJoin = "round"; ctx.lineCap = "round"
            let runColor = null
            function endRun() {
                if (runColor !== null) { ctx.strokeStyle = runColor; ctx.stroke(); runColor = null }
            }
            for (const segment of (root.chartData.segments || [])) {
                if (!segment.length) continue
                paintedPoints += segment.length
                if (segment.length === 1) {
                    ctx.fillStyle = root.colorForValue(segment[0].y)
                    ctx.beginPath(); ctx.arc(x(segment[0].x), y(segment[0].y), 2.5, 0, Math.PI * 2); ctx.fill()
                    continue
                }
                for (let i = 1; i < segment.length; ++i) {
                    const a = segment[i - 1], b = segment[i], dy = b.y - a.y
                    const cuts = [0, 1]
                    if (targets && dy !== 0) {
                        for (const edge of [low, high]) {
                            const cut = (edge - a.y) / dy
                            if (cut > 0 && cut < 1) cuts.push(cut)
                        }
                        cuts.sort((p, q) => p - q)
                    }
                    for (let k = 0; k < cuts.length - 1; ++k) {
                        const t0 = cuts[k], t1 = cuts[k + 1]
                        if (t1 - t0 < 1e-9) continue
                        const x0 = a.x + (b.x - a.x) * t0, y0 = a.y + dy * t0
                        const x1 = a.x + (b.x - a.x) * t1, y1 = a.y + dy * t1
                        const color = root.colorForValue((y0 + y1) / 2)
                        if (runColor !== color) {
                            endRun()
                            ctx.beginPath(); ctx.moveTo(x(x0), y(y0)); runColor = color
                        }
                        ctx.lineTo(x(x1), y(y1))
                    }
                }
                endRun()
            }
            if (root.hoveredPoint) {
                const px = x(root.hoveredPoint.x), py = y(root.hoveredPoint.y)
                ctx.strokeStyle = Theme.chartAxisText; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(px, t); ctx.lineTo(px, t + h); ctx.stroke()
                ctx.fillStyle = root.colorForValue(root.hoveredPoint.y)
                ctx.beginPath(); ctx.arc(px, py, 4, 0, Math.PI * 2); ctx.fill()
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: mouse => { root.hoveredPoint = root.nearest(mouse.x) }
        onExited: root.hoveredPoint = null
    }
    onHoveredPointChanged: canvas.requestPaint()
    ToolTip {
        visible: root.hoveredPoint !== null
        text: root.hoveredPoint
            ? root.pointTimeText(root.hoveredPoint) + " · "
              + Number(root.hoveredPoint.y).toLocaleString(Qt.locale("fi_FI"), 'f', 1) + " mmol/l"
            : ""
    }
    Label {
        anchors.centerIn: parent
        visible: canvas.paintedPoints === 0
        text: "Kuluvan päivän käyrältä ei ole vielä mittauksia"
    }
}
