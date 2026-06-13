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

/// In-app appearance choice for the reading screen, set in Settings. The shelf
/// stays dark regardless (its cover-art watermark needs light text); this only
/// drives the in-game transcript.
enum AppearanceMode: String, CaseIterable, Identifiable {
    case system, light, dark
    var id: String { rawValue }
    static let key = "appearanceMode"

    var label: String {
        switch self {
        case .system: return "System"
        case .light:  return "Light"
        case .dark:   return "Dark"
        }
    }

    /// The SwiftUI scheme to force, or nil to follow the device's OS setting.
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light:  return .light
        case .dark:   return .dark
        }
    }
}

/// Reading preferences shared between the Settings screen and the game view.
enum ReadingDefaults {
    /// Transcript text size in points. Default is roughly the system `.body`
    /// size at the default Dynamic Type setting.
    static let fontSize: Double = 17
    static let fontRange: ClosedRange<Double> = 12...28
    /// UserDefaults key for the persisted transcript font size.
    static let fontSizeKey = "transcriptFontSize"
}

/// The bundled glossary for a game: the single `<lang>_words.csv` matching the
/// game's own declared language (`game_language`), loaded for native long-press
/// "Define". Keys are lowercased words/phrases; values are the English gloss.
/// Matches the interpreter's CSV shape -- field[0] is the word (never quoted),
/// field[1] the definition, which may be quoted because it can hold commas
/// (e.g. `acara,"event, program"`).
struct GameDictionary {
    private let entries: [String: String]
    var isEmpty: Bool { entries.isEmpty }

    /// Map a game's `game_language` (BCP-47, e.g. "id-ID") to the CSV name. Only
    /// languages that ship a dictionary are listed; English and anything else
    /// return nil, which disables lookup for that game.
    private static let csvForLanguage = [
        "id": "indonesian_words.csv",
        "fr": "french_words.csv",
        "de": "german_words.csv",
        "es": "spanish_words.csv",
    ]

    /// Load the glossary for `language` (the game's `game_language`) from
    /// `dataDir`, or nil if that language has no matching CSV there.
    static func forGame(dataDir: String, language: String?) -> GameDictionary? {
        guard let sub = language?.split(separator: "-").first.map({ $0.lowercased() }),
              let name = Self.csvForLanguage[sub] else { return nil }
        let path = "\(dataDir)/\(name)"
        guard FileManager.default.fileExists(atPath: path) else { return nil }
        let dict = GameDictionary(csvPath: path)
        return dict.isEmpty ? nil : dict
    }

    private init(csvPath: String) {
        var map: [String: String] = [:]
        if let text = try? String(contentsOfFile: csvPath, encoding: .utf8) {
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

    /// The gloss for `word` (case-insensitive), or nil.
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
    /// The game's `constant header_colour`, used to tint the top chrome (nav bar
    /// and status line) so it matches the colour the game's banner art fades
    /// into -- the same per-game header colour the web interface uses. Read once
    /// on appear; nil for a game that declares none.
    @State private var headerColor: Color?
    /// Transcript text size, set in Settings and persisted app-wide.
    @AppStorage(ReadingDefaults.fontSizeKey) private var transcriptFontSize = ReadingDefaults.fontSize
    /// In-game appearance (System / Light / Dark), set in Settings. Applied from
    /// inside the reading screen so it drives the window even though the shelf's
    /// navigationDestination otherwise detaches it from the stack's scheme.
    @AppStorage(AppearanceMode.key) private var appearance = AppearanceMode.system
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
                // Load the glossary for native long-press "Define" -- only the
                // CSV matching THIS game's declared language (game_language), so
                // an English game never offers another game's definitions. nil
                // when the language ships no dictionary (English etc.).
                let dataDir = (gamePath as NSString).deletingLastPathComponent + "/data"
                let url = URL(fileURLWithPath: gamePath)
                dictionary = GameDictionary.forGame(
                    dataDir: dataDir, language: GameLibrary.stringConstant("game_language", in: url))
                // The per-game header colour the web interface uses for its
                // title band; tint the iPad's top chrome to match.
                headerColor = GameLibrary.stringConstant(
                    "header_colour", in: URL(fileURLWithPath: gamePath)).flatMap(Color.init(hex:))
                // The status grid is laid out by the interpreter to a column
                // count derived from the cell metrics we send; measure those at
                // the chosen reading size so the bar matches the status font.
                bridge.statusFontSize = transcriptFontSize
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
            .onChange(of: transcriptFontSize) { _, newSize in
                // Reading size changed: re-measure the status cell and tell the
                // interpreter (via arrange), so it re-lays-out the fixed-width
                // status line to the new column count instead of overflowing.
                bridge.statusFontSize = newSize
                bridge.resize(to: geo.size)
            }
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
        .preferredColorScheme(appearance.colorScheme)
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
                    .font(.system(size: transcriptFontSize, design: .monospaced))
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
        return TranscriptTextView(paragraphs: paras, dictionary: dictionary,
                                  headerColor: headerColor.map { UIColor($0) },
                                  fontSize: transcriptFontSize)
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
                    .font(.system(size: transcriptFontSize))
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
            t = t.font(.system(size: transcriptFontSize, design: .monospaced))
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
    /// The game's header colour, painted behind the opening banner image (the
    /// same per-game colour the web header band uses). nil leaves images plain.
    let headerColor: UIColor?
    /// Base transcript text size in points (from Settings).
    let fontSize: Double

    func makeUIView(context: Context) -> UITextView {
        let tv = UITextView()
        tv.isEditable = false
        tv.isSelectable = true
        tv.backgroundColor = .clear
        // No horizontal inset here: the body text is inset 12pt via paragraph
        // indents (see `attributed`) so the opening banner can run full-bleed to
        // the screen edges while the text keeps its margin.
        tv.textContainerInset = UIEdgeInsets(top: 12, left: 0, bottom: 12, right: 0)
        tv.textContainer.lineFragmentPadding = 0
        tv.alwaysBounceVertical = true
        tv.delegate = context.coordinator   // adds the "Define" edit-menu item
        return tv
    }

    func updateUIView(_ tv: UITextView, context: Context) {
        context.coordinator.dictionary = dictionary
        // Rebuild when the text or the chosen font size changes, so an
        // in-progress selection isn't dropped by an unrelated SwiftUI update.
        let lastLen = paragraphs.last?.spans.reduce(0) { $0 + $1.text.count } ?? 0
        let sig = "\(paragraphs.count)|\(lastLen)|\(fontSize)"
        guard sig != context.coordinator.lastSig else { return }
        context.coordinator.lastSig = sig
        tv.attributedText = Self.attributed(paragraphs,
                                            baseFont: Self.transcriptFont(ofSize: fontSize),
                                            headerColor: headerColor)
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

    /// Body font for the transcript: an old-style serif (Garamond-like, novel
    /// feel) for comfortable reading, falling back to the system font if it
    /// isn't available. Bold/italic/monospaced runs derive from this via
    /// `attributedString(baseFont:)`; preformatted text stays monospaced.
    static func transcriptFont(ofSize size: CGFloat) -> UIFont {
        UIFont(name: "Hoefler Text", size: size) ?? UIFont.systemFont(ofSize: size)
    }

    private static func attributed(_ paragraphs: [RenderedParagraph],
                                   baseFont: UIFont,
                                   headerColor: UIColor?) -> NSAttributedString {
        let out = NSMutableAttributedString()
        let screenW = UIScreen.main.bounds.width
        let inset: CGFloat = 12
        var bannerLocation: Int?
        var placedBanner = false
        for (i, para) in paragraphs.enumerated() {
            if i > 0 { out.append(NSAttributedString(string: "\n\n")) }
            for span in para.spans {
                if let num = span.image, let img = BlorbImageCache.shared.image(num) {
                    let att = NSTextAttachment()
                    if !placedBanner, let bg = headerColor {
                        // The opening banner: full-bleed. Its left edge sits at
                        // the screen edge (the art's left edge) and the header
                        // colour fills from the art's right edge to the screen's
                        // right edge, exactly the image's height -- so the art
                        // blends into the colour as in the web header band.
                        let scale = min(1, screenW / max(1, img.size.width))
                        let h = img.size.height * scale
                        att.image = bannerComposite(img, width: screenW, height: h, bg: bg)
                        att.bounds = CGRect(x: 0, y: 0, width: screenW, height: h)
                        bannerLocation = out.length
                    } else {
                        // In-game image: sits within the inset body column.
                        let maxW = screenW - 2 * inset
                        let scale = min(1, maxW / max(1, img.size.width))
                        att.image = img
                        att.bounds = CGRect(x: 0, y: 0,
                                            width: img.size.width * scale,
                                            height: img.size.height * scale)
                    }
                    placedBanner = true
                    out.append(NSAttributedString(attachment: att))
                } else if !span.text.isEmpty {
                    out.append(span.attributedString(baseFont: baseFont))
                }
            }
        }

        // Indent every paragraph 12pt left/right (the margin the text view no
        // longer applies), except the full-bleed banner paragraph, which runs
        // edge to edge.
        let body = NSMutableParagraphStyle()
        body.firstLineHeadIndent = inset
        body.headIndent = inset
        body.tailIndent = -inset
        let bannerStyle = NSParagraphStyle()
        let ns = out.string as NSString
        ns.enumerateSubstrings(in: NSRange(location: 0, length: ns.length),
                               options: .byParagraphs) { _, _, enclosing, _ in
            let isBanner = bannerLocation.map { NSLocationInRange($0, enclosing) } ?? false
            out.addAttribute(.paragraphStyle, value: isBanner ? bannerStyle : body, range: enclosing)
        }
        return out
    }

    /// Draw `img` (scaled to `height`, left-aligned at x=0) onto a band of `bg`
    /// that is `width` wide and exactly `height` tall, so the header colour
    /// shows from the art's right edge onward but never to its left.
    private static func bannerComposite(_ img: UIImage, width: CGFloat,
                                        height: CGFloat, bg: UIColor) -> UIImage {
        let size = CGSize(width: width, height: max(1, height))
        return UIGraphicsImageRenderer(size: size).image { ctx in
            bg.setFill()
            ctx.fill(CGRect(origin: .zero, size: size))
            let imgWidth = img.size.width * (size.height / max(1, img.size.height))
            img.draw(in: CGRect(x: 0, y: 0, width: imgWidth, height: size.height))
        }
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

extension Color {
    /// Parse a CSS-style hex colour as written in a game's `header_colour`
    /// constant: `#RRGGBB` or the 3-digit shorthand `#RGB` (e.g. "#777").
    init?(hex: String) {
        var s = hex.trimmingCharacters(in: .whitespaces)
        if s.hasPrefix("#") { s.removeFirst() }
        if s.count == 3 { s = s.map { "\($0)\($0)" }.joined() }   // #RGB -> #RRGGBB
        guard s.count == 6, let v = UInt64(s, radix: 16) else { return nil }
        self = Color(red:   Double((v >> 16) & 0xff) / 255,
                     green: Double((v >> 8) & 0xff) / 255,
                     blue:  Double(v & 0xff) / 255)
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
