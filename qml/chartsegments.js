// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Split each original continuous segment at its exact target crossings.
// Never join separate segments or turn a missing target into a low threshold.
function split(segments, low, high) {
    low = Number(low); high = Number(high)
    const targets = Number.isFinite(low) && Number.isFinite(high) && low > 0 && high > low
    function outside(value) { return targets && (value < low || value > high) }
    const result = []
    for (const points of (segments || [])) {
        if (!points.length) continue
        if (!targets || points.length === 1) {
            result.push({points: points, outside: outside(points[0].y)})
            continue
        }
        let run = null
        for (let i = 1; i < points.length; ++i) {
            const a = points[i - 1], b = points[i], dy = b.y - a.y
            const cuts = [0, 1]
            if (dy !== 0) {
                for (const edge of [low, high]) {
                    const cut = (edge - a.y) / dy
                    if (cut > 0 && cut < 1) cuts.push(cut)
                }
                cuts.sort((p, q) => p - q)
            }
            for (let k = 1; k < cuts.length; ++k) {
                const t0 = cuts[k - 1], t1 = cuts[k]
                const start = {x: a.x + (b.x - a.x) * t0, y: a.y + dy * t0}
                const end = {x: a.x + (b.x - a.x) * t1, y: a.y + dy * t1}
                const isOutside = outside((start.y + end.y) / 2)
                if (!run || run.outside !== isOutside) {
                    run = {points: [start], outside: isOutside}
                    result.push(run)
                }
                run.points.push(end)
            }
        }
    }
    return result
}
