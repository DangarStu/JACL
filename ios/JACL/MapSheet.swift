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
            let fit = min(geo.size.width / CGFloat(max(map.w, 1)),
                          geo.size.height / CGFloat(max(map.h, 1))) * 0.95
            ZStack(alignment: .topLeading) {
                Canvas { ctx, _ in
                    for e in map.edges {
                        var path = Path()
                        path.move(to: CGPoint(x: e.x1, y: e.y1))
                        path.addLine(to: CGPoint(x: e.x2, y: e.y2))
                        ctx.stroke(path, with: .color(.secondary), lineWidth: 1.5)
                        if e.updown {
                            drawArrow(ctx, CGPoint(x: e.x1, y: e.y1), CGPoint(x: e.x2, y: e.y2))
                        }
                    }
                    for n in map.nodes {
                        let rect = CGRect(x: n.x, y: n.y, width: 80, height: 80)
                        let rr = Path(roundedRect: rect, cornerRadius: 12)
                        // Opaque base hides the exit lines that run to the box
                        // centre; the current room gets an accent tint on top.
                        ctx.fill(rr, with: .color(Color(.secondarySystemBackground)))
                        if n.here { ctx.fill(rr, with: .color(Color.accentColor.opacity(0.35))) }
                        ctx.stroke(rr, with: .color(.secondary), lineWidth: 1)
                    }
                }
                .frame(width: CGFloat(map.w), height: CGFloat(map.h))

                // Room names: centred, wrapped, shrunk to fit the 80x80 box.
                ForEach(Array(map.nodes.enumerated()), id: \.offset) { _, n in
                    Text(n.label)
                        .font(.system(size: 12))
                        .multilineTextAlignment(.center)
                        .minimumScaleFactor(0.4)
                        .lineLimit(4)
                        .frame(width: 74, height: 74)
                        .position(x: CGFloat(n.x) + 40, y: CGFloat(n.y) + 40)
                }
            }
            .frame(width: CGFloat(map.w), height: CGFloat(map.h))
            .scaleEffect(fit * zoom)
            .offset(pan)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
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
