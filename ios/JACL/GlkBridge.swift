//  GlkBridge.swift
//  Runs the embedded RemGlk interpreter and bridges its JSON to SwiftUI.
//
//  Architecture (see ios/README.md, "iOS embedding architecture"):
//    • a socketpair() connects app <-> terp
//    • a background Thread runs jacl_bridge_run(path, terpFD), which dup2's
//      the terp end onto stdin/stdout and runs remglk_main()
//    • a reader queue pulls JSON "update" objects off the app end and applies
//      them to the published display model
//    • send(_:) writes JSON events back
//
//  Requires the bridging header to import "jacl_bridge.h" and "jacl_ios.h".
//
//  STATUS: v0 scaffold. Assemble in Xcode; not yet compiled or run on device.
//  The socket/thread/JSON-splitter flow is the part to validate first.

import Foundation
import Darwin

// MARK: - Rendered display model (what ContentView draws)

struct RenderedSpan: Identifiable {
    let id = UUID()
    let text: String
    let style: String
    let hyperlink: Int?
    let image: Int?          // blorb image resource number, if this span is an image
}

struct RenderedParagraph: Identifiable {
    let id = UUID()
    var spans: [RenderedSpan]
}

final class GlkBridge: ObservableObject {

    /// Window geometry/kind, in send-order (drives the SwiftUI layout).
    @Published var windows: [GlkWindow] = []
    /// Buffer windows: accumulated transcript, id -> paragraphs.
    @Published var buffers: [Int: [RenderedParagraph]] = [:]
    /// Grid windows: replaced each update, id -> rows of spans.
    @Published var grids: [Int: [[RenderedSpan]]] = [:]
    /// The pending input request, if the terp is waiting for one.
    @Published var pendingInput: GlkInput?
    /// Set when the game has quit (terp thread ended / socket closed).
    @Published var finished = false

    private var appFD: Int32 = -1
    private var generation = 0
    private var splitter = JSONObjectStream()
    private let readerQueue = DispatchQueue(label: "jacl.remglk.reader")
    // Writes MUST be on their own queue: the reader queue is occupied forever
    // by a blocking read() loop, so a write dispatched there would never run
    // (the init event would never reach the terp -> blank screen / deadlock).
    private let writerQueue = DispatchQueue(label: "jacl.remglk.writer")

    // MARK: Lifecycle

    /// Launch `gamePath` (an absolute .j2 path in the sandbox) and send the
    /// initial metrics for a display of `size` points.
    func start(gamePath: String, size: CGSize) {
        var fds: [Int32] = [0, 0]
        guard socketpair(AF_UNIX, SOCK_STREAM, 0, &fds) == 0 else {
            NSLog("GlkBridge: socketpair failed")
            return
        }
        appFD = fds[0]
        let terpFD = fds[1]

        let terp = Thread { [gamePath] in
            // Blocks until the game quits (glk_exit -> pthread_exit).
            gamePath.withCString { _ = jacl_bridge_run($0, terpFD) }
        }
        terp.stackSize = 1 << 20
        terp.name = "jacl.remglk.terp"
        terp.start()

        startReader()
        send(.initialize(metrics: metrics(for: size),
                         support: ["timer", "hyperlinks", "graphics"]))
    }

    /// Submit a line of input for the window currently awaiting it.
    func submitLine(_ value: String) {
        guard let req = pendingInput, req.type == "line" else { return }
        send(.line(gen: generation, window: req.id, value: value))
        pendingInput = nil
    }

    /// Submit a single character (for char input).
    func submitChar(_ value: String) {
        guard let req = pendingInput, req.type == "char" else { return }
        send(.char(gen: generation, window: req.id, value: value))
        pendingInput = nil
    }

    /// Tell the terp the display resized (e.g. rotation, split view).
    func resize(to size: CGSize) {
        send(.arrange(gen: generation, metrics: metrics(for: size)))
    }

    // MARK: Reader

    private func startReader() {
        let fd = appFD
        readerQueue.async { [weak self] in
            var buf = [UInt8](repeating: 0, count: 16 * 1024)
            while true {
                let n = read(fd, &buf, buf.count)
                if n <= 0 { break }                       // EOF / terp gone
                let objs = self?.splitter.append(Data(buf[0..<n])) ?? []
                for obj in objs {
                    guard let update = try? JSONDecoder().decode(GlkUpdate.self, from: obj)
                    else { continue }
                    DispatchQueue.main.async { self?.apply(update) }
                }
            }
            DispatchQueue.main.async { self?.finished = true }
        }
    }

    private func send(_ event: GlkEvent) {
        let data = event.jsonData()
        let fd = appFD
        writerQueue.async {                                // own queue, NOT the blocked reader queue
            data.withUnsafeBytes { raw in
                _ = write(fd, raw.baseAddress, raw.count)
            }
        }
    }

    // MARK: Apply an update to the model

    private func apply(_ update: GlkUpdate) {
        if update.type == "error" {
            NSLog("RemGlk error: %@", update.message ?? "?")
            return
        }
        if let gen = update.gen { generation = gen }

        if let ws = update.windows {
            windows = ws
            let live = Set(ws.map(\.id))
            buffers = buffers.filter { live.contains($0.key) }
            grids = grids.filter { live.contains($0.key) }
        }

        for c in update.content ?? [] {
            if let lines = c.lines {                       // grid window: absolute rows
                var rows = grids[c.id] ?? []
                for gl in lines {
                    while rows.count <= gl.line { rows.append([]) }
                    rows[gl.line] = (gl.content ?? []).map(render)
                }
                grids[c.id] = rows
            }
            if let text = c.text {                          // buffer window: append
                var paras = (c.clear == true) ? [] : (buffers[c.id] ?? [])
                for p in text {
                    let spans = (p.content ?? []).map(render)
                    if p.append == true, var last = paras.last {
                        last.spans.append(contentsOf: spans)
                        paras[paras.count - 1] = last
                    } else {
                        paras.append(RenderedParagraph(spans: spans))
                    }
                }
                buffers[c.id] = paras
            }
        }

        pendingInput = update.input?.first
    }

    private func render(_ span: GlkSpan) -> RenderedSpan {
        RenderedSpan(text: span.text ?? "",
                     style: span.style ?? "normal",
                     hyperlink: span.hyperlink,
                     image: span.special == "image" ? span.image : nil)
    }

    // MARK: Metrics

    /// Estimate metrics from a point size. charwidth/height are a monospace
    /// cell guess; TODO: measure the actual font the UI uses so the grid
    /// (status line) columns line up exactly.
    private func metrics(for size: CGSize) -> GlkMetrics {
        GlkMetrics(width: Double(size.width),
                   height: Double(size.height),
                   charwidth: 9.0,
                   charheight: 18.0)
    }
}

// MARK: - Incremental JSON object splitter

/// Accumulates bytes and yields each complete top-level JSON object as RemGlk
/// emits them (one "update" per turn, possibly arriving across reads).
struct JSONObjectStream {
    private var buffer = Data()

    mutating func append(_ data: Data) -> [Data] {
        buffer.append(data)
        var objects: [Data] = []
        var depth = 0, inString = false, escape = false
        var start = -1, consumed = 0
        let bytes = [UInt8](buffer)
        var i = 0
        while i < bytes.count {
            let b = bytes[i]
            if inString {
                if escape { escape = false }
                else if b == 0x5C { escape = true }        // backslash
                else if b == 0x22 { inString = false }     // quote
            } else if b == 0x22 {
                inString = true
            } else if b == 0x7B {                          // {
                if depth == 0 { start = i }
                depth += 1
            } else if b == 0x7D {                          // }
                depth -= 1
                if depth == 0 && start >= 0 {
                    objects.append(Data(bytes[start...i]))
                    consumed = i + 1
                    start = -1
                }
            }
            i += 1
        }
        if consumed > 0 {
            buffer.removeSubrange(buffer.startIndex..<(buffer.index(buffer.startIndex, offsetBy: consumed)))
        }
        return objects
    }
}
