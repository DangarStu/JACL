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
import UIKit

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
    /// A pending save/restore file prompt, if the game called save/restore and
    /// is waiting for a filename. Drives the name dialog / restore picker.
    @Published var pendingFilePrompt: GlkSpecialInput?
    /// Set when the game has quit (terp thread ended / socket closed).
    @Published var finished = false

    private var appFD: Int32 = -1
    private var generation = 0
    /// Remembered launch args so the game can be restarted in place.
    private var gamePath = ""
    private var lastSize = CGSize.zero
    /// RemGlk requires exactly one event per update. `awaiting` is true between
    /// sending an event and receiving the update it triggers; further events
    /// queue (with consecutive arranges coalesced) until then.
    private var awaiting = false
    private var outQueue: [GlkEvent] = []
    private var splitter = JSONObjectStream()
    private let readerQueue = DispatchQueue(label: "jacl.remglk.reader")
    // Writes MUST be on their own queue: the reader queue is occupied forever
    // by a blocking read() loop, so a write dispatched there would never run
    // (the init event would never reach the terp -> blank screen / deadlock).
    private let writerQueue = DispatchQueue(label: "jacl.remglk.writer")

    /// The terp that currently owns the shared stdin/stdout. Only one may run
    /// at a time; starting a new game force-stops this one first.
    static weak var active: GlkBridge?

    /// Point size of the monospaced status-grid cell, kept in step with the
    /// transcript reading size (Settings). The status line is a fixed-width
    /// grid, so the cell width the interpreter lays out columns to must match
    /// the font we actually draw -- set this before `start`/`resize`.
    var statusFontSize: Double = ReadingDefaults.fontSize

    // MARK: Lifecycle

    /// Launch `gamePath` (an absolute .j2 path in the sandbox) and send the
    /// initial metrics for a display of `size` points.
    func start(gamePath: String, size: CGSize) {
        // Remember the launch args + reset all protocol/display state, so this
        // is safe to call again for a Restart (fresh terp, generation back to 0).
        self.gamePath = gamePath
        self.lastSize = size
        buffers = [:]; grids = [:]; windows = []
        pendingInput = nil; pendingFilePrompt = nil; finished = false
        generation = 0; awaiting = false
        outQueue.removeAll(); splitter = JSONObjectStream()
        // Drop the previous game's cached blorb images -- the cache is keyed by
        // resource number, so this game's image 1 would otherwise show the last
        // game's image 1 (grail rendering the Down Dragon banner).
        BlorbImageCache.shared.clear()
        // Force-stop whatever terp was running before this one. GameView's
        // onDisappear normally does it, but that's unreliable on a navigation
        // pop (the view lives inside a GeometryReader), which would leave the
        // previous game's terp alive to hijack this game's shared stdin/stdout
        // -- "running grail but showing dragon". The C-side gate then makes this
        // terp wait until that one has actually exited.
        if let prev = GlkBridge.active, prev !== self {
            prev.stop()
        }
        GlkBridge.active = self
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
        enqueue(.initialize(metrics: metrics(for: size),
                            support: ["timer", "hyperlinks", "graphics"]))
    }

    /// Stop the interpreter and let its thread exit. Closing the app end of the
    /// socketpair gives the terp EOF on stdin, so it winds down through
    /// glk_exit (which closes its Glk windows) and pthread_exit; the shutdown
    /// also wakes the reader's blocked read(). This MUST run when leaving a
    /// game: JACL and RemGlk keep process-global state, so only one terp may
    /// run at a time. The next game's read_gamefile() (clear_game_data) and
    /// RemGlk's gli_initialize_windows() reset that state for a clean start --
    /// which is only safe because the previous terp has been stopped first.
    func stop() {
        let fd = appFD
        guard fd >= 0 else { return }
        appFD = -1
        shutdown(fd, SHUT_RDWR)
        close(fd)
        finished = true
    }

    deinit {
        let fd = appFD
        if fd >= 0 {
            shutdown(fd, SHUT_RDWR)
            close(fd)
        }
    }

    /// Submit a line of input for the window currently awaiting it.
    func submitLine(_ value: String) {
        guard let req = pendingInput, req.type == "line" else { return }
        enqueue(.line(gen: generation, window: req.id, value: value))
        pendingInput = nil
    }

    /// Submit a single character (for char input).
    func submitChar(_ value: String) {
        guard let req = pendingInput, req.type == "char" else { return }
        enqueue(.char(gen: generation, window: req.id, value: value))
        pendingInput = nil
    }

    /// Answer a pending save/restore file prompt with `name` (a bare filename
    /// the player chose). An empty `name` cancels the save/restore.
    func submitFileref(_ name: String) {
        guard pendingFilePrompt != nil else { return }
        enqueue(.specialResponse(gen: generation, value: name))
        pendingFilePrompt = nil
    }

    /// Cancel a pending save/restore prompt (sends an empty filename).
    func cancelFileref() { submitFileref("") }

    /// Restart the current game from scratch (used by the Restart control).
    /// The caller first suppresses the autosave and deletes the slot, so the
    /// relaunched terp finds no autosave and runs the intro fresh.
    func restart() {
        guard !gamePath.isEmpty else { return }
        stop()
        start(gamePath: gamePath, size: lastSize)
    }

    /// Tell the terp the display resized (e.g. rotation, split view, keyboard).
    func resize(to size: CGSize) {
        enqueue(.arrange(gen: generation, metrics: metrics(for: size)))
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

    /// Queue an outgoing event. Consecutive arranges are coalesced (the
    /// keyboard animation fires a burst of resizes); the queue then drains one
    /// event per received update.
    private func enqueue(_ event: GlkEvent) {
        if event.isArrange, let last = outQueue.last, last.isArrange {
            outQueue[outQueue.count - 1] = event
        } else {
            outQueue.append(event)
        }
        pump()
    }

    /// Send the next queued event if we're not already waiting for an update.
    /// The event is restamped with the current generation at send time, so a
    /// stale queued event never trips RemGlk's generation check.
    private func pump() {
        guard !awaiting, !outQueue.isEmpty else { return }
        awaiting = true
        rawSend(outQueue.removeFirst().restamped(gen: generation))
    }

    private func rawSend(_ event: GlkEvent) {
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
        // RemGlk sends one update per event. This update frees the queue to
        // send the next event -- after `generation` is updated below, so the
        // next event carries the right generation.
        defer { awaiting = false; pump() }

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
        // A save/restore file prompt arrives as a top-level `specialinput`
        // instead of a normal line/char request; surface it for the UI to
        // answer (name dialog when writing, picker when reading).
        if let si = update.specialinput, si.type == "fileref_prompt" {
            pendingFilePrompt = si
        }
    }

    // MARK: Saved games (per-game namespaced)

    // Every game shares one sandbox dir, so all of a game's saves are prefixed
    // with its base name ("<base>_"): a save called "start" becomes the file
    // "dragon_start.glksave", letting the same name be reused across games
    // without clashing. The autosave slot is "<base>__auto.glksave", excluded
    // from the player's list. These must match the interpreter's `prefix`
    // (the .j2 basename) — see jacl.c jacl_autosave_ref.

    /// The game's base name, e.g. "dragon" for ".../dragon.j2".
    static func gameBase(forGamePath p: String) -> String {
        ((p as NSString).lastPathComponent as NSString).deletingPathExtension
    }

    /// The on-disk path of the game's silent autosave slot.
    static func autosavePath(forGamePath p: String) -> String {
        (p as NSString).deletingPathExtension + "__auto.glksave"
    }

    /// The fileref value to send for a player-entered save `name`.
    static func saveValue(forGamePath p: String, name: String) -> String {
        gameBase(forGamePath: p) + "_" + name
    }

    /// Display names (game prefix stripped, autosave excluded) of this game's
    /// named saves, newest first.
    static func savedGames(forGamePath p: String) -> [String] {
        let dir = (p as NSString).deletingLastPathComponent
        let base = gameBase(forGamePath: p)
        let prefix = base + "_"
        let autoBare = base + "__auto"
        let fm = FileManager.default
        let files = (try? fm.contentsOfDirectory(atPath: dir)) ?? []
        return files
            .filter { $0.hasSuffix(".glksave") }
            .map { String($0.dropLast(".glksave".count)) }     // bare name
            .filter { $0.hasPrefix(prefix) && $0 != autoBare }
            .sorted { lhs, rhs in
                let l = (try? fm.attributesOfItem(atPath: "\(dir)/\(lhs).glksave")[.modificationDate]) as? Date
                let r = (try? fm.attributesOfItem(atPath: "\(dir)/\(rhs).glksave")[.modificationDate]) as? Date
                return (l ?? .distantPast) > (r ?? .distantPast)
            }
            .map { String($0.dropFirst(prefix.count)) }        // strip "<base>_"
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
        // The status line is a fixed-width grid: the game lays it out to
        // width/charwidth columns and we render it monospaced, so charwidth
        // must match the glyph we actually draw or the bar overflows the
        // screen. Measure the real monospaced .body glyph. Height is reported
        // very tall so the game never pauses with a "[MORE]" prompt -- the
        // transcript scrolls instead, the right model for a touch UI. The 16pt
        // accounts for the status line's horizontal padding.
        let mono = UIFont.monospacedSystemFont(
            ofSize: CGFloat(statusFontSize), weight: .regular)
        let cw = ("0" as NSString).size(withAttributes: [.font: mono]).width
        return GlkMetrics(width: Double(size.width) - 16,
                          height: 100_000,
                          charwidth: Double(cw),
                          charheight: Double(mono.lineHeight))
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
