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

/// The bundled language glossary for a game: every `data/*_words.csv` merged
/// into one lookup, loaded once for native long-press "Define". Keys are
/// lowercased words/phrases; values are the English gloss. Matches the
/// interpreter's CSV shape -- field[0] is the word (never quoted), field[1] the
/// definition, which may be quoted because it can hold commas (e.g.
/// `acara,"event, program"`).
struct GameDictionary {
    private let entries: [String: String]
    var isEmpty: Bool { entries.isEmpty }

    init(dataDir: String) {
        var map: [String: String] = [:]
        let files = ((try? FileManager.default.contentsOfDirectory(atPath: dataDir)) ?? [])
            .filter { $0.hasSuffix("_words.csv") }
        for file in files {
            guard let text = try? String(contentsOfFile: "\(dataDir)/\(file)", encoding: .utf8)
            else { continue }
            var isHeader = true
            for line in text.split(whereSeparator: \.isNewline) {
                if isHeader { isHeader = false; continue }     // skip header row
                guard let comma = line.firstIndex(of: ",") else { continue }
                let key = line[..<comma].trimmingCharacters(in: .whitespaces).lowercased()
                guard !key.isEmpty else { continue }
                var def = line[line.index(after: comma)...].trimmingCharacters(in: .whitespaces)
                if def.count >= 2, def.hasPrefix("\""), def.hasSuffix("\"") {
                    def = String(def.dropFirst().dropLast())   // unquote a comma-bearing gloss
                }
                map[key] = def
            }
        }
        entries = map
    }

    /// The gloss for `word` (case-insensitive), or nil if it isn't in any CSV.
    func define(_ word: String) -> String? { entries[word.lowercased()] }
}

struct GameView: View {
    let gamePath: String

    @StateObject private var bridge = GlkBridge()
    @State private var inputText = ""
    @State private var started = false
    /// The bundled language glossary (merged `data/*_words.csv`), if this game
    /// ships one -- enables native long-press "Define". Loaded once on appear.
    @State private var dictionary: GameDictionary?
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
                // A dictionary lives at <gameDir>/data/<lang>_words.csv; load it
                // once for native long-press "Define" (no command, no turn).
                let dataDir = (gamePath as NSString).deletingLastPathComponent + "/data"
                let dict = GameDictionary(dataDir: dataDir)
                dictionary = dict.isEmpty ? nil : dict
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
        // copied, and so long-pressing a word can define it from the bundled
        // glossary (a native popover -- no command echo, no turn, and it works
        // any time, not just at a prompt). UITextView/TextKit also paginates
        // long transcripts efficiently, sidestepping the old backing-store
        // overflow.
        return TranscriptTextView(paragraphs: paras, dictionary: dictionary)
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
    let dictionary: GameDictionary?

    func makeUIView(context: Context) -> UITextView {
        let tv = UITextView()
        tv.isEditable = false
        tv.isSelectable = true
        tv.backgroundColor = .clear
        tv.textContainerInset = UIEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        tv.alwaysBounceVertical = true
        tv.delegate = context.coordinator   // adds the "Define" edit-menu item
        return tv
    }

    func updateUIView(_ tv: UITextView, context: Context) {
        context.coordinator.dictionary = dictionary
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

    final class Coordinator: NSObject, UITextViewDelegate, UIPopoverPresentationControllerDelegate {
        var dictionary: GameDictionary?
        var lastSig = ""

        // Long-press selects text and opens the edit menu; when the game ships a
        // glossary, add a "Define" item next to Copy. Tapping it shows the gloss
        // in a popover anchored to the selection -- no command, no turn, and it
        // works any time (even mid-game), unlike the old `lookup` verb path.
        func textView(_ textView: UITextView, editMenuForTextIn range: NSRange,
                      suggestedActions: [UIMenuElement]) -> UIMenu? {
            guard dictionary != nil, range.length > 0 else { return nil }
            // Fold the selection into a lookup key: trim and collapse internal
            // whitespace/line-wraps to single spaces, so a wrapped phrase still
            // matches a CSV key like "abu abu". Cap the length so a giant
            // multi-paragraph selection doesn't try to be a single word.
            let key = (textView.text as NSString).substring(with: range)
                .components(separatedBy: .whitespacesAndNewlines)
                .filter { !$0.isEmpty }.joined(separator: " ")
            guard !key.isEmpty, key.count <= 40 else { return nil }
            let define = UIAction(title: "Define") { [weak self, weak textView] _ in
                self?.define(key, in: textView)
            }
            return UIMenu(children: [define] + suggestedActions)
        }

        private func define(_ word: String, in textView: UITextView?) {
            guard let textView else { return }
            let body = dictionary?.define(word).map { "\(word) — \($0)" }
                ?? "“\(word)” isn’t in the dictionary."
            let popover = DefinitionViewController(text: body)
            popover.modalPresentationStyle = .popover
            if let pop = popover.popoverPresentationController {
                pop.sourceView = textView
                pop.sourceRect = textView.selectedTextRange
                    .map { textView.firstRect(for: $0) }
                    ?? CGRect(x: textView.bounds.midX, y: textView.bounds.midY, width: 1, height: 1)
                pop.permittedArrowDirections = [.up, .down]
                pop.delegate = self
            }
            Coordinator.presenter(for: textView)?.present(popover, animated: true)
        }

        // Keep it a popover (arrow, anchored) even on compact widths, rather
        // than letting iOS promote it to a full-screen sheet.
        func adaptivePresentationStyle(for controller: UIPresentationController)
            -> UIModalPresentationStyle { .none }

        /// Nearest presenting view controller, walked up the responder chain
        /// (the UITextView lives inside SwiftUI's UIHostingController).
        private static func presenter(for view: UIView) -> UIViewController? {
            var responder: UIResponder? = view
            while let r = responder {
                if let vc = r as? UIViewController { return vc }
                responder = r.next
            }
            return view.window?.rootViewController
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

// MARK: - Definition popover

/// A small popover that shows a word's gloss for long-press "Define". Sizes
/// itself to the text so the popover hugs the definition.
private final class DefinitionViewController: UIViewController {
    private let text: String
    init(text: String) { self.text = text; super.init(nibName: nil, bundle: nil) }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .secondarySystemBackground
        let pad: CGFloat = 14
        let maxTextWidth: CGFloat = 252
        let font = UIFont.preferredFont(forTextStyle: .body)

        let label = UILabel()
        label.numberOfLines = 0
        label.text = text
        label.font = font
        label.preferredMaxLayoutWidth = maxTextWidth
        label.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(label)
        NSLayoutConstraint.activate([
            label.topAnchor.constraint(equalTo: view.topAnchor, constant: pad),
            label.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -pad),
            label.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: pad),
            label.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -pad),
        ])
        let textSize = (text as NSString).boundingRect(
            with: CGSize(width: maxTextWidth, height: .greatestFiniteMagnitude),
            options: [.usesLineFragmentOrigin, .usesFontLeading],
            attributes: [.font: font], context: nil)
        preferredContentSize = CGSize(width: ceil(textSize.width) + pad * 2,
                                      height: ceil(textSize.height) + pad * 2)
    }
}
