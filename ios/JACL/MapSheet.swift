//  MapSheet.swift
//  A swipe-to-dismiss sheet showing the explored map, drawn on a Canvas.

import SwiftUI

struct MapSheet: View {
    let map: GameMap?
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if let map {
                    MapCanvas(map: map)
                } else {
                    ContentUnavailableView("No Map Yet", systemImage: "map",
                        description: Text("Type “map” during play to chart where you've been."))
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

/// Draws the map fit-to-view, with pinch-zoom and drag-pan on top.
private struct MapCanvas: View {
    let map: GameMap
    @State private var zoom: CGFloat = 1
    @State private var pan: CGSize = .zero
    @State private var lastPan: CGSize = .zero

    var body: some View {
        GeometryReader { geo in
            let fit = min(geo.size.width / CGFloat(max(map.w, 1)),
                          geo.size.height / CGFloat(max(map.h, 1))) * 0.95
            Canvas { ctx, size in
                let ox = (size.width - CGFloat(map.w) * fit) / 2
                let oy = (size.height - CGFloat(map.h) * fit) / 2
                func p(_ x: Int, _ y: Int) -> CGPoint {
                    CGPoint(x: ox + CGFloat(x) * fit, y: oy + CGFloat(y) * fit)
                }

                for e in map.edges {
                    var path = Path()
                    path.move(to: p(e.x1, e.y1))
                    path.addLine(to: p(e.x2, e.y2))
                    ctx.stroke(path, with: .color(.secondary), lineWidth: 1.5)
                    if e.updown { drawArrow(ctx, p(e.x1, e.y1), p(e.x2, e.y2)) }
                }

                for n in map.nodes {
                    let rect = CGRect(x: ox + CGFloat(n.x) * fit, y: oy + CGFloat(n.y) * fit,
                                      width: 80 * fit, height: 80 * fit)
                    let rr = Path(roundedRect: rect, cornerRadius: 12 * fit)
                    ctx.fill(rr, with: .color(n.here
                        ? Color.accentColor.opacity(0.3)
                        : Color(.secondarySystemBackground)))
                    ctx.stroke(rr, with: .color(.secondary), lineWidth: 1)
                    let text = Text(n.label).font(.system(size: max(7, 11 * fit)))
                    ctx.draw(ctx.resolve(text), in: rect.insetBy(dx: 2, dy: 2))
                }
            }
            .scaleEffect(zoom)
            .offset(pan)
            .gesture(
                SimultaneousGesture(
                    MagnificationGesture()
                        .onChanged { zoom = $0 }
                        .onEnded { _ in if zoom < 0.3 { withAnimation { zoom = 1 } } },
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
