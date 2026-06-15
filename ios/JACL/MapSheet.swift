//  MapSheet.swift
//  A swipe-to-dismiss sheet showing the explored map, drawn on a Canvas.

import SwiftUI

struct MapSheet: View {
    // Observe the bridge so the sheet updates when the `map` command's data
    // arrives (it's published a moment after the Map button sends "map").
    @ObservedObject var bridge: GlkBridge
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if let map = bridge.gameMap {
                    MapCanvas(map: map)
                } else {
                    ContentUnavailableView("No Map Yet", systemImage: "map",
                        description: Text("Move around, then tap Map to chart where you've been."))
                }
            }
            .navigationTitle("Map")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .presentationDetents([.large])
    }
}

/// Draws the rooms/exits on a Canvas (in the map's own pixel space) and lays
/// the room names over them as real Text views, so they centre, wrap and
/// shrink to fit the box. The whole thing is scaled fit-to-view, with
/// pinch-zoom and drag-pan on top.
private struct MapCanvas: View {
    let map: GameMap
    @State private var zoom: CGFloat = 1
    @State private var pan: CGSize = .zero
    @State private var lastPan: CGSize = .zero

    var body: some View {
        GeometryReader { geo in
            // Everything is drawn in SCREEN coordinates (map coord * scale +
            // offset), so there's no giant scaled frame -- a large `map all`
            // map (which SwiftUI's scaleEffect-on-a-huge-frame failed to render)
            // now lays out fine. `scale` fits the whole map to the viewport,
            // times the pinch zoom; `ox/oy` centre it plus the drag pan.
            let baseFit = min(geo.size.width / CGFloat(max(map.w, 1)),
                              geo.size.height / CGFloat(max(map.h, 1))) * 0.95
            let scale = baseFit * zoom
            let ox = (geo.size.width - CGFloat(map.w) * scale) / 2 + pan.width
            let oy = (geo.size.height - CGFloat(map.h) * scale) / 2 + pan.height
            let sp = { (x: Int, y: Int) -> CGPoint in
                CGPoint(x: ox + CGFloat(x) * scale, y: oy + CGFloat(y) * scale)
            }
            ZStack(alignment: .topLeading) {
                Canvas { ctx, _ in
                    for e in map.edges {
                        var path = Path()
                        path.move(to: sp(e.x1, e.y1))
                        path.addLine(to: sp(e.x2, e.y2))
                        ctx.stroke(path, with: .color(.secondary), lineWidth: 1.5)
                        if e.updown { drawArrow(ctx, sp(e.x1, e.y1), sp(e.x2, e.y2)) }
                    }
                    for n in map.nodes {
                        let tl = sp(n.x, n.y)
                        let rect = CGRect(x: tl.x, y: tl.y, width: 80 * scale, height: 80 * scale)
                        let rr = Path(roundedRect: rect, cornerRadius: 12 * scale)
                        // Opaque base hides the exit lines that run to the box
                        // centre; the current room gets an accent tint on top.
                        ctx.fill(rr, with: .color(Color(.secondarySystemBackground)))
                        if n.here { ctx.fill(rr, with: .color(Color.accentColor.opacity(0.35))) }
                        ctx.stroke(rr, with: .color(.secondary), lineWidth: 1)
                    }
                }

                // Room names: centred (H+V), wrapped, shrunk to fit the box.
                ForEach(Array(map.nodes.enumerated()), id: \.offset) { _, n in
                    Text(n.label)
                        .font(.system(size: max(6, 11 * scale)))
                        .multilineTextAlignment(.center)
                        .minimumScaleFactor(0.4)
                        .lineLimit(4)
                        .frame(width: 74 * scale, height: 74 * scale)
                        .position(x: ox + (CGFloat(n.x) + 40) * scale,
                                  y: oy + (CGFloat(n.y) + 40) * scale)
                }
            }
            .frame(width: geo.size.width, height: geo.size.height)
            .contentShape(Rectangle())
            .gesture(
                SimultaneousGesture(
                    MagnificationGesture()
                        .onChanged { zoom = $0 }
                        .onEnded { _ in if zoom < 0.25 { withAnimation { zoom = 1; pan = .zero; lastPan = .zero } } },
                    DragGesture()
                        .onChanged { pan = CGSize(width: lastPan.width + $0.translation.width,
                                                  height: lastPan.height + $0.translation.height) }
                        .onEnded { _ in lastPan = pan }
                )
            )
        }
    }

    private func drawArrow(_ ctx: GraphicsContext, _ a: CGPoint, _ b: CGPoint) {
        let mid = CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2)
        let dx = b.x - a.x, dy = b.y - a.y
        let len = max(1, hypot(dx, dy))
        let ux = dx / len, uy = dy / len, back: CGFloat = 9, spread: CGFloat = 6
        let base = CGPoint(x: mid.x - ux * back, y: mid.y - uy * back)
        let px = -uy * spread, py = ux * spread
        var path = Path()
        path.move(to: mid); path.addLine(to: CGPoint(x: base.x + px, y: base.y + py))
        path.move(to: mid); path.addLine(to: CGPoint(x: base.x - px, y: base.y - py))
        ctx.stroke(path, with: .color(.secondary), lineWidth: 1.5)
    }
}
