// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import "theme.js" as Theme
import QtQuick.Controls

Item {
    id: root
    required property var profileData
    property int hovered: -1
    property int paintedBins: canvas.paintedBins
    implicitHeight: 390
    onProfileDataChanged: canvas.requestPaint()
    Canvas {
        id: canvas
        objectName: "agpCanvas"
        anchors.fill: parent
        property int paintedBins: 0
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible)
            requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const bins = root.profileData.bins || [], l = 48, r = 16, t = 18, b = 42, w = width - l - r, h = height - t - b;
            if (w <= 0 || h <= 0)
                return;
            const maxY = 20;
            const x = m => l + m / 1440 * w, y = v => t + h - Math.min(v, maxY) / maxY * h;
            ctx.fillStyle = Theme.bg;
            ctx.fillRect(0, 0, width, height);
            const lo = root.profileData.low, hi = root.profileData.high;
            if (lo > 0 && hi > lo) {
                ctx.fillStyle = Theme.chartTargetFill;
                ctx.fillRect(l, y(hi), w, y(lo) - y(hi));
                ctx.strokeStyle = Theme.chartTargetEdge;
                ctx.lineWidth = 1;
                ctx.strokeRect(l, y(hi), w, y(lo) - y(hi));
            }
            ctx.strokeStyle = Theme.chartGrid;
            ctx.lineWidth = .5;
            ctx.fillStyle = Theme.chartAxisText;
            ctx.font = "9px sans-serif";
            for (let v = 0; v <= maxY; v += 1) {
                ctx.beginPath();
                ctx.moveTo(l, y(v));
                ctx.lineTo(l + w, y(v));
                ctx.stroke();
                ctx.fillText(String(v), 12, y(v) + 3);
            }
            ctx.font = "12px sans-serif";
            for (let hour = 0; hour <= 24; hour += 3)
                ctx.fillText(String(hour).padStart(2, "0"), x(hour * 60) - 7, height - 19);
            ctx.fillText("mmol/l", 4, 12);
            let runs = [], run = [];
            paintedBins = 0;
            for (let p of bins) {
                if (p.days > 0) {
                    run.push(p);
                    paintedBins++;
                } else if (run.length) {
                    runs.push(run);
                    run = [];
                }
            }
            if (run.length)
                runs.push(run);
            function band(points, low, high, fill, edge) {
                if (points.length < 2)
                    return;
                ctx.beginPath();
                ctx.moveTo(x(points[0].minute), y(points[0][low]));
                for (let p of points)
                    ctx.lineTo(x(p.minute), y(p[low]));
                for (let i = points.length - 1; i >= 0; i--)
                    ctx.lineTo(x(points[i].minute), y(points[i][high]));
                ctx.closePath();
                ctx.fillStyle = fill;
                ctx.fill();
                ctx.strokeStyle = edge;
                ctx.lineWidth = 1;
                ctx.stroke();
            }
            for (let points of runs) {
                band(points, "p05", "p95", Theme.chart595Fill, Theme.chart595Edge);
                band(points, "p25", "p75", Theme.chart2575Fill, Theme.chart2575Edge);
                for (let key of ["mean", "median"]) {
                    ctx.beginPath();
                    ctx.lineWidth = key === "mean" ? 2.5 : 1.5;
                    ctx.strokeStyle = key === "mean" ? Theme.chartMean : Theme.chartMedian;
                    for (let i = 0; i < points.length; i++) {
                        let p = points[i];
                        if (i === 0)
                            ctx.moveTo(x(p.minute), y(p[key]));
                        else
                            ctx.lineTo(x(p.minute), y(p[key]));
                    }
                    ctx.stroke();
                }
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: mouse => {
            root.hovered = Math.max(0, Math.min(95, Math.floor((mouse.x - 48) / (width - 64) * 96)));
        }
        onExited: root.hovered = -1
    }
    ToolTip {
        visible: root.hovered >= 0
        text: {
            let p = (root.profileData.bins || [])[root.hovered];
            if (!p)
                return "";
            let hour = Math.floor(root.hovered / 4), minute = (root.hovered % 4) * 15;
            return String(hour).padStart(2, "0") + ":" + String(minute).padStart(2, "0") + " · " + p.days + qsTranslate("Studio", " päivää · kattavuus ") + (100 * p.coverage).toFixed(1) + " %" + (p.days ? "\nKeskiarvo " + p.mean.toFixed(2) + " · mediaani " + p.median.toFixed(2) + " mmol/l" : "\nEi mittauksia");
        }
    }
    Label {
        anchors.centerIn: parent
        visible: !(root.profileData.coveredSeconds > 0)
        text: qsTranslate("Studio", "Valitulta jaksolta ei ole mittauksia")
    }
}
