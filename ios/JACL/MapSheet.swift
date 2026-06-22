//  MapSheet.swift
//  A swipe-to-dismiss sheet showing the explored map, drawn on a Canvas.

import SwiftUI
import UIKit

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
struct MapCanvas: View {
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
            // One input surface for every device: mouse wheel / trackpad two-finger
            // scroll and pinch both zoom; finger or pointer drag pans; double-tap or
            // double-click resets. SwiftUI gestures can't see Catalyst's scroll-wheel
            // events, so this drops to UIKit recognizers (see MapInput).
            .overlay(
                MapInput(
                    onZoom: { factor in zoom = min(6, max(0.3, zoom * factor)) },
                    onPan: { t in pan = CGSize(width: lastPan.width + t.width,
                                               height: lastPan.height + t.height) },
                    onPanEnded: { lastPan = pan },
                    onReset: { withAnimation { zoom = 1; pan = .zero; lastPan = .zero } }
                )
            )
            // Always-available zoom controls -- the reliable path on a Mac with a
            // mouse (where scroll-wheel routing through the sheet is unreliable) and
            // a clear affordance on touch too. Drawn on top of the input surface.
            .overlay(alignment: .bottomTrailing) {
                VStack(spacing: 0) {
                    zoomButton("plus")  { zoom = min(6, zoom * 1.3) }
                    Divider().frame(width: 26)
                    zoomButton("minus") { zoom = max(0.3, zoom / 1.3) }
                }
                .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 10))
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(.quaternary, lineWidth: 1))
                .padding(14)
            }
        }
    }

    private func zoomButton(_ icon: String, action: @escaping () -> Void) -> some View {
        Button { withAnimation(.easeOut(duration: 0.15)) { action() } } label: {
            Image(systemName: icon)
                .font(.system(size: 17, weight: .semibold))
                .frame(width: 40, height: 40)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
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

/// A transparent input surface over the map covering every input device:
///   - mouse wheel / trackpad two-finger scroll  -> zoom  (the Mac gap we're filling)
///   - pinch                                      -> zoom
///   - finger or pointer drag                     -> pan
///   - double tap / double click                  -> reset
/// SwiftUI gestures can't observe Catalyst's scroll-wheel events, so this uses
/// UIKit recognizers. A scroll-only pan (maximumNumberOfTouches = 0) is kept
/// separate from the drag pan (minimumNumberOfTouches = 1) so the two never fight.
private struct MapInput: UIViewRepresentable {
    var onZoom: (CGFloat) -> Void     // multiplicative factor (1 == no change)
    var onPan: (CGSize) -> Void       // translation within the current drag
    var onPanEnded: () -> Void
    var onReset: () -> Void

    func makeUIView(context: Context) -> UIView {
        let v = UIView()
        v.backgroundColor = .clear
        let c = context.coordinator

        let scroll = UIPanGestureRecognizer(target: c, action: #selector(Coordinator.onScroll(_:)))
        scroll.allowedScrollTypesMask = .all      // mouse wheel + trackpad scroll
        scroll.maximumNumberOfTouches = 0         // indirect scroll only, never a finger
        scroll.delegate = c
        v.addGestureRecognizer(scroll)

        let drag = UIPanGestureRecognizer(target: c, action: #selector(Coordinator.onDrag(_:)))
        drag.minimumNumberOfTouches = 1
        drag.delegate = c
        v.addGestureRecognizer(drag)

        let pinch = UIPinchGestureRecognizer(target: c, action: #selector(Coordinator.onPinch(_:)))
        pinch.delegate = c
        v.addGestureRecognizer(pinch)

        let reset = UITapGestureRecognizer(target: c, action: #selector(Coordinator.onReset))
        reset.numberOfTapsRequired = 2
        v.addGestureRecognizer(reset)
        return v
    }

    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.parent = self }
    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var parent: MapInput
        private var lastScrollY: CGFloat = 0
        private var lastPinch: CGFloat = 1
        init(_ p: MapInput) { parent = p }

        @objc func onScroll(_ g: UIPanGestureRecognizer) {
            let y = g.translation(in: g.view).y
            switch g.state {
            case .began:   lastScrollY = y
            case .changed:
                let dy = y - lastScrollY
                lastScrollY = y
                parent.onZoom(1 - dy / 250)        // scroll up -> zoom in
            default:       lastScrollY = 0
            }
        }

        @objc func onDrag(_ g: UIPanGestureRecognizer) {
            let t = g.translation(in: g.view)
            parent.onPan(CGSize(width: t.x, height: t.y))
            if g.state == .ended || g.state == .cancelled {
                parent.onPanEnded()
                g.setTranslation(.zero, in: g.view)
            }
        }

        @objc func onPinch(_ g: UIPinchGestureRecognizer) {
            switch g.state {
            case .began:   lastPinch = 1
            case .changed: parent.onZoom(g.scale / lastPinch); lastPinch = g.scale
            default:       lastPinch = 1
            }
        }

        @objc func onReset() { parent.onReset() }

        // Let pinch + drag (and the scroll pan) coexist without one cancelling another.
        func gestureRecognizer(_ g: UIGestureRecognizer,
                               shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }
    }
}
