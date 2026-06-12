//  ContentView.swift
//  The in-game screen: grid (status / board) windows on top, buffer
//  (transcript) windows below, and an input bar.
//
//  STATUS: v0 scaffold. Assemble in Xcode; not yet compiled. Graphics/sound
//  spans render as a placeholder for now (blorb resource wiring is later).

import SwiftUI
import UIKit

/// Decodes and caches blorb images by resource number. RemGlk sends the image
/// number/size in its JSON; the pixels come from the game's blorb via the C
/// bridge (jacl_bridge_image). Accessed on the main thread during rendering;
/// the terp is blocked on input at that point, so it isn't touching the blorb.
final class BlorbImageCache {
    static let shared = BlorbImageCache()
    private var store: [Int: UIImage] = [:]

    func image(_ num: Int) -> UIImage? {
        if let cached = store[num] { return cached }
        var len: UInt32 = 0
        guard let ptr = jacl_bridge_image(UInt32(num), &len), len > 0 else { return nil }
        let img = UIImage(data: Data(bytes: ptr, count: Int(len)))
        store[num] = img
        return img
    }

    /// Drop all cached images. MUST be called when switching games: the cache
    /// is keyed by blorb resource number only, and every game numbers its title
    /// image #1 etc., so without this the new game's "draw image 1" returns the
    /// PREVIOUS game's cached image (e.g. grail showing the Down Dragon banner).
    func clear() { store.removeAll() }
}

struct GameView: View {
    let gamePath: String

    @StateObject private var bridge = GlkBridge()
    @State private var inputText = ""
    @State private var started = false
    /// True if a `*_words.csv` dictionary is bundled next to this game, which
    /// enables tap-a-word lookup. Computed once on appear.
    @State private var hasDictionary = false
    @FocusState private var inputFocused: Bool

    // DEBUG-only scripted input (via `-autocommands "no;look;…"`), used to
    // exercise the bridge's input round-trip headlessly. Empty in release.
    @State private var autoCommands: [String] = GameView.parsedAutoCommands()
    @State private var autoIndex = 0

    var body: some View {
        GeometryReader { geo in
            VStack(spacing: 0) {
                ForEach(bridge.windows.filter { $0.type == "grid" }) { w in
                    gridView(id: w.id)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color(.secondarySystemBackground))
                }

                ForEach(bridge.windows.filter { $0.type == "buffer" }) { w in
                    bufferView(id: w.id)
                }

                inputBar
            }
            .onAppear {
                guard !started else { return }
                started = true
                // A dictionary lives at <gameDir>/data/<lang>_words.csv.
                let dataDir = (gamePath as NSString).deletingLastPathComponent + "/data"
                hasDictionary = ((try? FileManager.default.contentsOfDirectory(atPath: dataDir)) ?? [])
                    .contains { $0.hasSuffix("_words.csv") }
                bridge.start(gamePath: gamePath, size: geo.size)
            }
            .onDisappear {
                // Leaving the game (back to the shelf): stop this terp so its
                // thread exits and frees the shared JACL/RemGlk globals before
                // another game starts. Without this, swapping games leaves two
                // interpreters running in one process and the app hangs/crashes.
                bridge.stop()
            }
            .onChange(of: geo.size) { _, newSize in bridge.resize(to: newSize) }
            .onChange(of: bridge.pendingInput) { _, input in
                // Put the cursor in the command line whenever the game asks for
                // one (so you can just type), and replay any scripted command.
                inputFocused = (input?.type == "line")
                guard let input, input.type == "line", autoIndex < autoCommands.count else { return }
                let cmd = autoCommands[autoIndex]
                autoIndex += 1
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    inputText = cmd
                    submit()
                }
            }
        }
        .navigationBarTitleDisplayMode(.inline)
    }

    /// Parse `-autocommands "no;look;…"` from the launch args (DEBUG only).
    private static func parsedAutoCommands() -> [String] {
        #if DEBUG
        let args = ProcessInfo.processInfo.arguments
        guard let i = args.firstIndex(of: "-autocommands"), i + 1 < args.count else { return [] }
        return args[i + 1].split(separator: ";").map { $0.trimmingCharacters(in: .whitespaces) }
        #else
        return []
        #endif
    }

    // MARK: Grid window (status line / game board) — fixed monospaced rows

    private func gridView(id: Int) -> some View {
        let rows = bridge.grids[id] ?? []
        return VStack(alignment: .leading, spacing: 0) {
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                rowText(row)
                    .font(.system(.body, design: .monospaced))
                    .lineLimit(1)
                    .minimumScaleFactor(0.5)   // never let the status bar overflow
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }

    // MARK: Buffer window (scrolling transcript), auto-scrolled to bottom

    private func bufferView(id: Int) -> some View {
        var paras = bridge.buffers[id] ?? []
        // While the game waits for a command, JACL has already written its bare
        // prompt ("> ") as the trailing paragraph. Hide it: the input bar below
        // is the live prompt, and the command is echoed back ("> look") on
        // submit. Only drop a genuinely-short trailing line, so real output (or
        // an already-echoed "> look") is never removed.
        if bridge.pendingInput?.type == "line",
           let last = paras.last,
           last.spans.allSatisfy({ $0.image == nil }),
           last.spans.map(\.text).joined()
               .trimmingCharacters(in: .whitespacesAndNewlines).count <= 2 {
            paras.removeLast()
        }
        // A UITextView (not SwiftUI Text) so the transcript can be selected and
        // copied, and so a tap can resolve to the word under it for dictionary
        // lookup. Lookup is enabled only when the game bundles a dictionary and
        // we're at a command prompt; UITextView also handles long transcripts
        // efficiently (TextKit), so it sidesteps the old backing-store overflow.
        return TranscriptTextView(
            paragraphs: paras,
            lookupEnabled: hasDictionary && bridge.pendingInput?.type == "line",
            onLookup: { bridge.lookUp($0) })
    }

    // MARK: Input

    @ViewBuilder private var inputBar: some View {
        if bridge.pendingInput?.type == "line" {
            // Console-style prompt: you type beside a ">", and the command is
            // echoed into the transcript on submit (the bare ">" there is hidden
            // by bufferView until then).
            HStack(spacing: 6) {
                Text(">").foregroundColor(.secondary)
                TextField("", text: $inputText)
                    .textFieldStyle(.plain)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .focused($inputFocused)
                    .onSubmit(submit)
                Button("Enter", action: submit)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
        } else if bridge.pendingInput?.type == "char" {
            // A "press any key" / "[MORE]" pause. Rare now that the buffer is
            // reported tall, but handle it so the game can't get stuck.
            Button { bridge.submitChar(" ") } label: {
                Text("Tap to continue").frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .padding(8)
        } else if bridge.finished {
            Text("The game has ended.")
                .foregroundColor(.secondary)
                .padding(8)
        }
    }

    private func submit() {
        let line = inputText
        inputText = ""
        bridge.submitLine(line)
    }

    // MARK: Styled-text helpers

    private func rowText(_ spans: [RenderedSpan]) -> Text {
        spans.reduce(Text("")) { $0 + styled($1) }
    }

    /// Map a Glk style name to SwiftUI text styling (used by the grid window;
    /// the buffer transcript renders via TranscriptTextView).
    private func styled(_ span: RenderedSpan) -> Text {
        var t = Text(span.text)
        switch span.style {
        case "header", "subheader":      t = t.bold()
        case "emphasized", "note":       t = t.italic()
        case "alert":                    t = t.foregroundColor(.red)
        case "input":                    t = t.foregroundColor(.accentColor)
        case "preformatted", "user1", "user2":
            t = t.font(.system(.body, design: .monospaced))
        default:                         break
        }
        if span.hyperlink != nil {
            t = t.foregroundColor(.accentColor).underline()
        }
        return t
    }
}

// MARK: - Transcript (UITextView: selection/copy + tap-to-lookup)

/// The scrolling game transcript, in a UITextView so it can be selected and
/// copied, and (for dictionary games) so a tap resolves to the word under it.
/// UITextView/TextKit also paginates long transcripts efficiently.
struct TranscriptTextView: UIViewRepresentable {
    let paragraphs: [RenderedParagraph]
    let lookupEnabled: Bool
    let onLookup: (String) -> Void

    func makeUIView(context: Context) -> UITextView {
        let tv = UITextView()
        tv.isEditable = false
        tv.isSelectable = true
        tv.backgroundColor = .clear
        tv.textContainerInset = UIEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        tv.alwaysBounceVertical = true
        let tap = UITapGestureRecognizer(target: context.coordinator,
                                         action: #selector(Coordinator.handleTap(_:)))
        tap.cancelsTouchesInView = false   // don't swallow selection or scrolling
        tv.addGestureRecognizer(tap)
        return tv
    }

    func updateUIView(_ tv: UITextView, context: Context) {
        context.coordinator.lookupEnabled = lookupEnabled
        context.coordinator.onLookup = onLookup
        // Rebuild only when the text actually changed, so an in-progress
        // selection isn't dropped by an unrelated SwiftUI update.
        let lastLen = paragraphs.last?.spans.reduce(0) { $0 + $1.text.count } ?? 0
        let sig = "\(paragraphs.count)|\(lastLen)"
        guard sig != context.coordinator.lastSig else { return }
        context.coordinator.lastSig = sig
        tv.attributedText = Self.attributed(paragraphs,
                                            baseFont: UIFont.preferredFont(forTextStyle: .body))
        DispatchQueue.main.async {
            guard tv.attributedText.length > 0 else { return }
            tv.scrollRangeToVisible(NSRange(location: tv.attributedText.length - 1, length: 1))
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    final class Coordinator: NSObject {
        var lookupEnabled = false
        var onLookup: (String) -> Void = { _ in }
        var lastSig = ""

        @objc func handleTap(_ g: UITapGestureRecognizer) {
            guard lookupEnabled, let tv = g.view as? UITextView else { return }
            if let sel = tv.selectedTextRange, !sel.isEmpty { return }   // selecting -> leave it
            let pt = g.location(in: tv)
            guard let pos = tv.closestPosition(to: pt) else { return }
            for dir in [UITextDirection.storage(.forward), UITextDirection.storage(.backward)] {
                if let range = tv.tokenizer.rangeEnclosingPosition(pos, with: .word, inDirection: dir),
                   let word = tv.text(in: range), !word.isEmpty {
                    onLookup(word)
                    return
                }
            }
        }
    }

    private static func attributed(_ paragraphs: [RenderedParagraph],
                                   baseFont: UIFont) -> NSAttributedString {
        let out = NSMutableAttributedString()
        let maxImageW = UIScreen.main.bounds.width - 24
        for (i, para) in paragraphs.enumerated() {
            if i > 0 { out.append(NSAttributedString(string: "\n\n")) }
            for span in para.spans {
                if let num = span.image, let img = BlorbImageCache.shared.image(num) {
                    let att = NSTextAttachment()
                    att.image = img
                    let scale = min(1, maxImageW / max(1, img.size.width))
                    att.bounds = CGRect(x: 0, y: 0,
                                        width: img.size.width * scale,
                                        height: img.size.height * scale)
                    out.append(NSAttributedString(attachment: att))
                } else if !span.text.isEmpty {
                    out.append(span.attributedString(baseFont: baseFont))
                }
            }
        }
        return out
    }
}

private extension RenderedSpan {
    /// NSAttributedString form of GameView.styled (Glk style -> text attributes).
    func attributedString(baseFont: UIFont) -> NSAttributedString {
        var font = baseFont
        var color = UIColor.label
        var underline = false
        switch style {
        case "header", "subheader": font = baseFont.withTraits(.traitBold)
        case "emphasized", "note":  font = baseFont.withTraits(.traitItalic)
        case "alert":               color = .systemRed
        case "input":               color = .tintColor
        case "preformatted", "user1", "user2":
            font = .monospacedSystemFont(ofSize: baseFont.pointSize, weight: .regular)
        default: break
        }
        if hyperlink != nil { color = .tintColor; underline = true }
        var attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color]
        if underline { attrs[.underlineStyle] = NSUnderlineStyle.single.rawValue }
        return NSAttributedString(string: text, attributes: attrs)
    }
}

private extension UIFont {
    func withTraits(_ traits: UIFontDescriptor.SymbolicTraits) -> UIFont {
        guard let d = fontDescriptor.withSymbolicTraits(traits) else { return self }
        return UIFont(descriptor: d, size: pointSize)
    }
}
