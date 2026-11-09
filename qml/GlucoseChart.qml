// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "theme.js" as Theme
import "chartsegments.js" as ChartSegments
import QtQuick.Controls
import QtGraphs

Item {
    id: root
    // QQuickItem.data is the default visual-child list. Never shadow it.
    required property var chartData
    property string sourceName: "tietolähde"
    property bool highlightOutsideTarget: false
    readonly property var renderedSegments: {
        const data = chartData || {}
        return highlightOutsideTarget
            ? ChartSegments.split(data.segments, data.low, data.high)
            : (data.segments || []).map(points => ({points: points, outside: false}))
    }
    readonly property int expectedPointCount: {
        let count = 0
        for (const segment of renderedSegments) count += segment.points.length
        const data = chartData || {}
        for (const limit of [data.low, data.high]) if (limit > 0) count += 2
        return count
    }
    property var created: []
    readonly property var graph: graphLoader.item
    implicitHeight: 380
    Component { id: lineFactory; LineSeries { color: Theme.accentTeal; width: 2 } }
    function seriesAt(index) {
        if (index === created.length) {
            let s = lineFactory.createObject(graph)
            graph.addSeries(s)
            created.push(s)
        }
        return created[index]
    }
    function updateSeries(index, points, color, width) {
        let s = seriesAt(index)
        s.color = color
        s.width = width
        s.replace(points.map(p => Qt.point(p.x, p.y)))
        s.visible = points.length > 0
    }
    function redraw() {
        if (!visible) return
        if (!graphLoader.active) { graphLoader.active=true;return }
        if (!graph) return
        // Keep live QObject identities until GraphsView itself is destroyed.
        // Removing/deleting rendered series reproduced a SIGSEGV in
        // QMetaObject::cast during Qt Graphs' pending polish work.
        let used = 0
        for (const segment of renderedSegments) {
            updateSeries(used++, segment.points, segment.outside ? Theme.alertRed : Theme.accentTeal, 2)
        }
        const limits = [(chartData || {}).low, (chartData || {}).high]
        for (let i = 0; i < limits.length; ++i) {
            if (limits[i] > 0) {
                updateSeries(used++, [{x:chartData.from,y:limits[i]},{x:chartData.to,y:limits[i]}], i === 0 ? Theme.chartLow : Theme.chartHigh, 1)
            }
        }
        for (let i = used; i < created.length; ++i) {
            if (created[i].count) created[i].clear()
            created[i].visible = false
        }
    }
    onRenderedSegmentsChanged: redrawTimer.restart()
    onVisibleChanged: if (visible) redrawTimer.restart()
    Component.onCompleted: redrawTimer.start()
    Timer { id:redrawTimer;interval:100;onTriggered:root.redraw() }
    Canvas {
        id: gridCanvas
        anchors.fill: parent
        property var plot: root.graph ? root.graph.plotArea : null
        onPlotChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx=getContext("2d");ctx.reset()
            const p=plot
            if(!p||p.width<=0||p.height<=0)return
            ctx.strokeStyle=Theme.chartGrid;ctx.lineWidth=0.5
            for(let v=0;v<=20;v+=1){
                const y=p.y+p.height-v/20*p.height
                ctx.beginPath();ctx.moveTo(p.x,y);ctx.lineTo(p.x+p.width,y);ctx.stroke()
            }
        }
    }
    Loader {
        id: graphLoader
        anchors.fill: parent
        active: false
        sourceComponent: graphComponent
        onLoaded: redrawTimer.restart()
    }
    Component {
      id: graphComponent
      GraphsView {
        marginLeft: 10; marginRight: 15; marginTop: 10; marginBottom: 25
        axisX: DateTimeAxis {
            min: new Date(root.chartData.from || Date.now()-86400000)
            max: new Date(root.chartData.to || Date.now())
            labelFormat: "dd.MM HH:mm"
        }
        axisY: ValueAxis { min: 0; max: 20; tickInterval: 1; labelDecimals: 0; titleText: "mmol/l" }
        theme: GraphsTheme {
            colorScheme: GraphsTheme.ColorScheme.Dark
            backgroundVisible: false
            plotAreaBackgroundVisible: false
            gridVisible: false
            axisYLabelFont: Qt.font({pixelSize: 9})
            axisXLabelFont: Qt.font({pixelSize: 9})
        }
      }
    }
    Repeater {
        model: root.chartData.markers || []
        delegate: Rectangle {
            required property var modelData
            property real ratio: (modelData.time-root.chartData.from)/(root.chartData.to-root.chartData.from)
            width: 10; height: 10; radius: modelData.kind === "carbs" ? 0 : 5
            x: root.graph ? root.graph.plotArea.x + ratio*root.graph.plotArea.width-width/2 : 0
            y: root.graph ? root.graph.plotArea.y+root.graph.plotArea.height-12-(modelData.kind === "rapid" ? 18 : modelData.kind === "basal" ? 36 : 0) : 0
            color: modelData.kind === "carbs" ? Theme.accentAmber : modelData.kind === "basal" ? Theme.accentPurple : Theme.accentBlue
            visible: !!root.graph && ratio>=0 && ratio<=1
            HoverHandler { id: hover }
            ToolTip.visible: hover.hovered
            ToolTip.text: (modelData.timeText || new Date(modelData.time).toLocaleString(Qt.locale("fi_FI"),"dd.MM HH:mm"))+" · "+modelData.label+" "+modelData.value+" · "+(modelData.local ? "paikallinen" : root.sourceName)
        }
    }
    Label { anchors.centerIn: parent; visible: !(root.chartData.segments || []).length; text: "Tällä aikavälillä ei ole mittauksia" }
}
