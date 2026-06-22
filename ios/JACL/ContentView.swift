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
    /// Reading width in characters. The font size is derived so this many
    /// columns fill the window width -- so choosing a column count is really
    /// choosing the text size (fewer columns = bigger text), and it rescales
    /// with the window. The Settings slider goes 40...80, centred on 60.
    /// A phone is far narrower than a tablet, so the same column count gives tiny
    /// text; phones get fewer columns (bigger text) and a lower range.
    static var isPhone: Bool { UIDevice.current.userInterfaceIdiom == .phone }
    static var columns: Double { isPhone ? 30 : 50 }
    static var columnRange: ClosedRange<Double> { isPhone ? 20...45 : 30...70 }
    /// UserDefaults key for the persisted column count.
    static let columnsKey = "readingColumns"
    /// UserDefaults key for the side-margin width (narrow/normal/wide).
    static let marginsKey = "readingMargins"
    /// Default margin: narrow on a phone (every point of width matters for the
    /// font size), normal on the roomier iPad / Mac. Matches Wryter's options.
    static var defaultMargins: MarginWidth { isPhone ? .narrow : .normal }
    /// A base side padding kept even at the narrowest margin (points).
    static let horizontalPadding: Double = 16
    /// Monospace cell width as a fraction of the point size (≈ "0" advance), used
    /// only as a first guess; the exact advance is measured in applyColumns.
    static let charWidthRatio: Double = 0.6
    /// Cap the reading column's width (points) as a last resort on a very wide
    /// window, so even "narrow" margins don't give one ballooned line.
    static let maxContentWidth: Double = 900
}

/// Side-margin width, mirroring Wryter so JACL's reading settings match and the
/// two feel like a suite. `fraction` is the share of the window kept as margin on
/// each side; the column centres in what's left and the scroll bar rides the
/// window edge out in the margin.
enum MarginWidth: String, CaseIterable, Identifiable {
    case narrow, normal, wide
    var id: String { rawValue }
    var label: String { rawValue.capitalized }
    var fraction: Double {
        switch self {
        case .narrow: return 0.04
        case .normal: return 0.10
        case .wide:   return 0.16
        }
    }
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
    @EnvironmentObject private var appModel: AppModel
    @Environment(\.openWindow) private var openWindow
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
    /// Reading width in columns, set in Settings and persisted app-wide.
    @AppStorage(ReadingDefaults.columnsKey) private var columns = ReadingDefaults.columns
    /// Side-margin width (narrow/normal/wide), set in Settings (matches Wryter).
    @AppStorage(ReadingDefaults.marginsKey) private var margins: MarginWidth = ReadingDefaults.defaultMargins
    /// Font size derived from `columns` + the window width, so the chosen number
    /// of columns fills the screen and rescales with it (see applyColumns).
    @State private var derivedFontSize: Double = 18
    /// Side inset that centres the reading column; the transcript text view spans
    /// the full width with this inset, so the scroll bar rides the window's right
    /// edge. Derived in applyColumns from the column width and the margin setting.
    @State private var sideInset: CGFloat = 0
    /// In-game appearance (System / Light / Dark), set in Settings. Applied from
    /// inside the reading screen so it drives the window even though the shelf's
    /// navigationDestination otherwise detaches it from the stack's scheme.
    @AppStorage(AppearanceMode.key) private var appearance = AppearanceMode.system
    /// Whether game sound plays. Persistent across all games (see Settings).
    @AppStorage("soundEnabled") private var soundEnabled = true
    @FocusState private var inputFocused: Bool
    /// Whether the in-game text-size popover (the top-bar "Aa" button) is open.
    @State private var showTextSize = false
    /// Bumped on each command submit, so the transcript anchors its scroll on the
    /// start of the new turn (see TranscriptTextView).
    @State private var turnCount = 0
    /// Screen-y of the on-screen keyboard's top (.infinity = no keyboard). We take
    /// over keyboard avoidance (see body) so the transcript shrinks to the real
    /// visible region instead of sliding under the nav bar -- which makes its
    /// bounds.height the true viewport the scroll logic needs.
    @State private var keyboardTop: CGFloat = .infinity
    /// The transcript view's frame in global (screen) coords, measured by SwiftUI
    /// (reliable, unlike UIKit frame conversion). Combined with keyboardTop it
    /// gives the true visible height for the scroll maths.
    @State private var bufferFrame: CGRect = .zero
    /// Working text for the "name this save" dialog.
    @State private var saveName = ""
    /// Whether the "restart this game?" confirmation is showing.
    @State private var showRestartConfirm = false
    /// Whether the map sheet is showing.
    @State private var showMap = false

    // DEBUG-only scripted input (via `-autocommands "no;look;…"`), used to
    // exercise the bridge's input round-trip headlessly. Empty in release.
    @State private var autoCommands: [String] = GameView.parsedAutoCommands()
    @State private var autoIndex = 0

    var body: some View {
        GeometryReader { geo in
            // True visible height of the transcript: from its top down to the
            // keyboard top (or its own bottom when no keyboard). Measured in
            // SwiftUI, so it's right regardless of how the keyboard shifts things.
            let visibleHeight = currentVisibleHeight()
            // `sideInset` is derived in applyColumns from the column width and the
            // margin setting; the transcript spans the full width with the text
            // inset by it, so the scroll bar rides the window's right edge.
            VStack(spacing: 0) {
                // Status grid(s): full window width. A status bar is chrome -- it
                // spans the window edge to edge (on a wide Mac/iPad it must not be
                // boxed into the centred reading column). fixedSize keeps it hugging
                // its rows so it never balloons taller than the status text.
                ForEach(bridge.windows.filter { $0.type == "grid" }) { w in
                    gridView(id: w.id)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.leading, sideInset)   // align status text with the centred column
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color(.secondarySystemBackground))
                }

                // Transcript: full-width text view (forced), text inset to the
                // centred column, so the scroll bar sits out in the right margin.
                ForEach(bridge.windows.filter { $0.type == "buffer" }) { w in
                    bufferView(id: w.id, visibleHeight: visibleHeight, horizontalInset: sideInset)
                        .frame(maxWidth: .infinity)
                        .background(GeometryReader { g in
                            Color.clear
                                .onAppear { bufferFrame = g.frame(in: .global) }
                                .onChange(of: g.frame(in: .global)) { _, f in bufferFrame = f }
                        })
                }

                // Input: inset to align under the centred text column.
                inputBar
                    .padding(.horizontal, sideInset)

                // Mac: a control bar across the bottom (full width, like the status
                // bar up top), so the reading-size / map / settings / restart
                // controls live in the window instead of only the menu bar -- and
                // the input prompt isn't left sitting on bare space.
                #if targetEnvironment(macCatalyst)
                macControlBar
                #endif
            }
            .onAppear {
                // Publish this game so the menu bar / map window reach its bridge.
                appModel.setActive(bridge, path: gamePath)
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
                applyColumns(width: geo.size.width)
                bridge.setSoundEnabled(soundEnabled)
                bridge.start(gamePath: gamePath, size: geo.size)
            }
            .onDisappear {
                // Leaving the game (back to the shelf): stop this terp so its
                // thread exits and frees the shared JACL/RemGlk globals before
                // another game starts. Without this, swapping games leaves two
                // interpreters running in one process and the app hangs/crashes.
                bridge.stop()
                appModel.clearIfActive(bridge)
            }
            .onChange(of: geo.size) { _, newSize in
                applyColumns(width: newSize.width)   // re-derive the font for the new width
                bridge.resize(to: newSize)
            }
            .onChange(of: columns) { _, _ in
                // Column count changed (the slider): re-derive the font and the
                // status cell, then re-arrange so the grid follows the new width.
                applyColumns(width: geo.size.width)
                bridge.resize(to: geo.size)
            }
            .onChange(of: margins) { _, _ in
                // Margin width changed: re-derive the column and re-arrange.
                applyColumns(width: geo.size.width)
                bridge.resize(to: geo.size)
            }
            .onChange(of: soundEnabled) { _, on in bridge.setSoundEnabled(on) }
            // A fresh map arrived (the player typed `map`, or the map button/menu
            // asked for it). iOS raises the sheet; Mac opens the map window once,
            // then leaves it to redraw from gameMap.
            #if targetEnvironment(macCatalyst)
            .onChange(of: bridge.mapVersion) { _, _ in
                if !appModel.mapWindowOpen { openWindow(id: AppModel.mapWindowID) }
            }
            #else
            .onChange(of: bridge.mapVersion) { _, _ in showMap = true }
            #endif
            // Re-focus the command line after a sheet closes, so you can keep
            // typing without tapping the field again.
            .onChange(of: showMap) { _, shown in if !shown { focusInput() } }
            .onChange(of: showTextSize) { _, shown in if !shown { focusInput() } }
            .onChange(of: bridge.pendingInput) { _, input in
                // Put the cursor in the command line whenever the game asks for
                // one (so you can just type), and replay any scripted command.
                // Between turns pendingInput briefly goes nil; keep the field
                // focused through that gap so the keyboard doesn't drop and
                // re-raise after every command. Only a char prompt resigns it.
                if input?.type == "line" { focusInput() }
                else if input?.type == "char" { inputFocused = false }
                guard let input, input.type == "line", autoIndex < autoCommands.count else { return }
                let cmd = autoCommands[autoIndex]
                autoIndex += 1
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    inputText = cmd
                    submit()
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillChangeFrameNotification)) { note in
            if let f = (note.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? NSValue)?.cgRectValue {
                keyboardTop = f.minY
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillHideNotification)) { _ in
            keyboardTop = .infinity
        }
        .navigationBarTitleDisplayMode(.inline)
        .preferredColorScheme(appearance.colorScheme)
        // Mac drives text-size / map / restart from the menu bar (GameCommands)
        // and shows the map in its own window: presenting a popover/sheet/alert
        // over the focused command field crashes UIKit's keyboard scene delegate
        // on Catalyst. iPhone/iPad keep these in the toolbar.
        #if !targetEnvironment(macCatalyst)
        .toolbar {
            // A text-size control beside the back arrow, so the reading size can
            // be changed without leaving the game for Settings.
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    // Resign the command field before presenting. On Mac Catalyst,
                    // presenting this popover while the input holds the keyboard
                    // crashes UIKit in the keyboard scene delegate (it pins the
                    // input views during the presentation transition). Harmless on
                    // iOS/iPadOS, where the field just gives up focus for the popover.
                    UIApplication.shared.sendAction(
                        #selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
                    showTextSize = true
                } label: {
                    Image(systemName: "textformat.size")
                }
                .accessibilityLabel("Text size")
                .popover(isPresented: $showTextSize) {
                    TextSizePopover(columns: $columns)
                }
            }
            // Map: ask the game to emit its map, then open the map sheet.
            ToolbarItem(placement: .topBarTrailing) {
                Button { bridge.submitLine("map"); showMap = true } label: {
                    Image(systemName: "map")
                }
                .accessibilityLabel("Map")
            }
            // Restart: wipe the autosave and begin the game again from the intro.
            ToolbarItem(placement: .topBarTrailing) {
                Button { showRestartConfirm = true } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .accessibilityLabel("Restart game")
            }
        }
        .sheet(isPresented: $showMap) {
            MapSheet(bridge: bridge)
        }
        #endif
        // Saving: the game asked for a name (save verb). Restoring uses the
        // picker below. Both answer the same RemGlk file prompt.
        .alert("Save Game", isPresented: writePromptBinding) {
            TextField("Save name", text: $saveName)
                .textInputAutocapitalization(.never)
            Button("Save") {
                let n = saveName.trimmingCharacters(in: .whitespaces)
                // Prefix with the game so the same name works across games; an
                // empty name cancels.
                bridge.submitFileref(n.isEmpty ? "" : GlkBridge.saveValue(forGamePath: gamePath, name: n))
                saveName = ""
            }
            Button("Cancel", role: .cancel) { bridge.cancelFileref(); saveName = "" }
        } message: {
            Text("Name this saved game.")
        }
        .sheet(isPresented: readPromptBinding) {
            RestorePicker(saves: GlkBridge.savedGames(forGamePath: gamePath),
                          onPick: { bridge.submitFileref(GlkBridge.saveValue(forGamePath: gamePath, name: $0)) },
                          onCancel: { bridge.cancelFileref() })
        }
        .alert("Restart Game?", isPresented: $showRestartConfirm) {
            Button("Restart", role: .destructive) { restartGame() }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Start over from the beginning. Your current progress (the autosave) is lost; named saves are kept.")
        }
    }

    /// True while the game is waiting for a save name (write-mode file prompt).
    private var writePromptBinding: Binding<Bool> {
        Binding(get: { bridge.pendingFilePrompt?.filemode == "write" },
                set: { show in
                    if !show, bridge.pendingFilePrompt?.filemode == "write" {
                        bridge.cancelFileref()
                    }
                })
    }

    /// True while the game is waiting for a save to restore (read-mode prompt).
    /// Swiping the picker away cancels the restore.
    private var readPromptBinding: Binding<Bool> {
        Binding(get: { bridge.pendingFilePrompt?.filemode == "read" },
                set: { show in
                    if !show, bridge.pendingFilePrompt?.filemode == "read" {
                        bridge.cancelFileref()
                    }
                })
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

    /// Derive the reading font from the chosen column count: size it so `columns`
    /// columns fill the window width, and set the cell metrics the interpreter
    /// lays the status grid out to, so the grid is exactly `columns` wide. The
    /// drawn glyph runs a hair wider than the measured advance, so the font
    /// targets ~0.5pt under the cell to keep the status text inside the grid.
    private func applyColumns(width: CGFloat) {
        let w = Double(width)
        guard w > 0 else { return }
        let cols = min(max(columns, ReadingDefaults.columnRange.lowerBound),
                       ReadingDefaults.columnRange.upperBound)
        // Margins (Wryter-style) reserve a share of the window on each side; the
        // font then scales so `cols` columns fill what's left (capped to a
        // readable size), and the column centres in the remainder -- the surplus
        // becomes the margin, with the scroll bar riding the window edge in it.
        let side0 = w * margins.fraction + ReadingDefaults.horizontalPadding
        let usable = max(w - 2 * side0, 1)
        let target = max(1, usable / cols - 0.5)
        func advance(_ pt: Double) -> Double {
            let f = UIFont.monospacedSystemFont(ofSize: CGFloat(pt), weight: .regular)
            return Double(("0" as NSString).size(withAttributes: [.font: f]).width)
        }
        let refSize = 20.0
        var font = min(44, max(7, refSize * target / advance(refSize)))
        font = min(44, max(7, font * target / advance(font)))   // refine (advance ~linear)
        let cellW = advance(font)
        let content = min(usable, Double(cols) * cellW)         // column width (font-capped)
        derivedFontSize = font
        bridge.cellWidth = cellW                                // grid = floor(content/cellW) = columns
        bridge.contentWidth = content                           // the terp's window width
        bridge.cellHeight = Double(
            UIFont.monospacedSystemFont(ofSize: CGFloat(font), weight: .regular).lineHeight)
        // Centre the column; the transcript text view spans the full width with
        // this inset so the scroll bar sits out in the right margin.
        sideInset = CGFloat(max((w - content) / 2, ReadingDefaults.horizontalPadding))
    }

    // MARK: Grid window (status line / game board) — fixed monospaced rows

    private func gridView(id: Int) -> some View {
        let rows = bridge.grids[id] ?? []
        return VStack(alignment: .leading, spacing: 0) {
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                rowText(row)
                    .font(.system(size: derivedFontSize, design: .monospaced))
                    .lineLimit(1)
                    .minimumScaleFactor(0.5)   // never let the status bar overflow
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }

    // MARK: Buffer window (scrolling transcript), auto-scrolled to bottom

    /// The transcript's true visible height: top of the measured buffer frame down
    /// to the keyboard top (or its own bottom when there's no keyboard).
    private func currentVisibleHeight() -> CGFloat {
        guard keyboardTop.isFinite, bufferFrame.height > 0 else { return bufferFrame.height }
        let bottomLimit = min(bufferFrame.maxY, keyboardTop)
        return max(1, bottomLimit - bufferFrame.minY)
    }

    private func bufferView(id: Int, visibleHeight: CGFloat, horizontalInset: CGFloat) -> some View {
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
                                  fontSize: derivedFontSize, turnCount: turnCount,
                                  visibleHeight: visibleHeight, horizontalInset: horizontalInset)
    }

    // MARK: Input

    /// Whether to show the line-input field. True at a "line" prompt AND through
    /// the brief gap between turns (pendingInput momentarily nil), so the field --
    /// and the keyboard attached to it -- stays put instead of being torn down and
    /// rebuilt every command (which dropped and re-raised the keyboard). A "char"
    /// prompt, a file prompt, or the end of the game hides it.
    private var showsLineInput: Bool {
        if bridge.finished || bridge.pendingFilePrompt != nil { return false }
        switch bridge.pendingInput?.type {
        case "line", nil: return true
        default:          return false   // "char" (press-a-key) etc.
        }
    }

    @ViewBuilder private var inputBar: some View {
        if showsLineInput {
            // Console-style prompt: you type beside a ">", and the command is
            // echoed into the transcript on submit (the bare ">" there is hidden
            // by bufferView until then).
            HStack(spacing: 6) {
                Text(">").foregroundColor(.secondary)
                TextField("", text: $inputText)
                    .textFieldStyle(.plain)
                    .font(.system(size: derivedFontSize))
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
        turnCount += 1          // anchor the transcript scroll on this new turn
        bridge.submitLine(line)
    }

    #if targetEnvironment(macCatalyst)
    /// Bottom control bar (Mac): reading size, map, settings, restart. Every action
    /// is direct or opens a separate window -- never a modal over the focused
    /// command field (which crashes UIKit's keyboard scene delegate on Catalyst).
    @ViewBuilder private var macControlBar: some View {
        HStack(spacing: 2) {
            Button { adjustColumns(by: 4) } label: { Image(systemName: "textformat.size.smaller") }
                .help("Smaller text")
            Button { adjustColumns(by: -4) } label: { Image(systemName: "textformat.size.larger") }
                .help("Larger text")

            Divider().frame(height: 18).padding(.horizontal, 10)

            Button { bridge.submitLine("map") } label: { Image(systemName: "map") }
                .help("Show map")

            Spacer()

            Button { openWindow(id: AppModel.settingsWindowID) } label: { Image(systemName: "gearshape") }
                .help("Settings")
            Button { restartGame() } label: { Image(systemName: "arrow.clockwise") }
                .help("Restart game")
        }
        .buttonStyle(.plain)
        .foregroundStyle(.primary)
        .font(.system(size: 18))
        .imageScale(.large)
        .padding(.horizontal, 18)
        .padding(.vertical, 9)
        .frame(maxWidth: .infinity)
        .background(Color(.secondarySystemBackground))
        .overlay(alignment: .top) { Divider() }
    }
    #endif

    /// Clamp the reading width by `delta` columns (more columns = smaller text).
    private func adjustColumns(by delta: Double) {
        columns = min(ReadingDefaults.columnRange.upperBound,
                      max(ReadingDefaults.columnRange.lowerBound, columns + delta))
    }

    /// Restart the game: suppress + drop its autosave, relaunch the terp at the intro.
    private func restartGame() {
        jacl_autosave_set_suppressed(1)
        try? FileManager.default.removeItem(atPath: GlkBridge.autosavePath(forGamePath: gamePath))
        bridge.restart()
    }

    /// Focus the command line when the game is waiting for a line of input, so
    /// you can just type. A short delay lets the field appear (on first open)
    /// or a dismissing sheet settle before the focus is requested.
    private func focusInput() {
        guard bridge.pendingInput?.type == "line" else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
            if bridge.pendingInput?.type == "line" { inputFocused = true }
        }
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
        case "emphasized", "note", "blockquote": t = t.italic()
        case "alert":                    t = t.foregroundColor(.red)
        case "input":                    t = t.foregroundColor(.accentColor)
        case "preformatted", "user1", "user2":
            t = t.font(.system(size: derivedFontSize, design: .monospaced))
        default:                         break
        }
        if span.hyperlink != nil {
            t = t.foregroundColor(.accentColor).underline()
        }
        return t
    }
}

// MARK: - In-game text size

/// The column-width slider shown from the reading screen's top-bar "Aa" button.
/// It binds the same persisted `columns` as Settings, so changing it here
/// rescales the live transcript and sticks app-wide (fewer columns = bigger text).
private struct TextSizePopover: View {
    @Binding var columns: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("Columns")
                Spacer()
                Text("\(Int(columns))").foregroundStyle(.secondary)
            }
            HStack(spacing: 12) {
                Image(systemName: "textformat.size.larger").foregroundStyle(.secondary)
                Slider(value: $columns, in: ReadingDefaults.columnRange, step: 1)
                    .accessibilityLabel("Columns")
                Image(systemName: "textformat.size.smaller").foregroundStyle(.secondary)
            }
        }
        .padding()
        .frame(width: 280)
        .presentationCompactAdaptation(.popover)
    }
}

// MARK: - Restore picker

/// A list of the game's saved games, shown when the player types "restore".
/// Picking one answers the file prompt with its name; the bundled binding
/// cancels the restore if the sheet is dismissed without a choice.
private struct RestorePicker: View {
    let saves: [String]
    let onPick: (String) -> Void
    let onCancel: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if saves.isEmpty {
                    ContentUnavailableView("No Saved Games", systemImage: "tray",
                        description: Text("Type “save” during play to create one."))
                } else {
                    List(saves, id: \.self) { name in
                        Button { onPick(name); dismiss() } label: {
                            Label(name, systemImage: "doc")
                        }
                    }
                }
            }
            .navigationTitle("Restore Game")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { onCancel(); dismiss() }
                }
            }
        }
        .presentationDetents([.medium, .large])
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
    /// Bumped each time the player submits a command, so the view anchors the
    /// scroll on the turn's start regardless of the echo's text format.
    let turnCount: Int
    /// The transcript's true visible height (SwiftUI-measured, keyboard-aware),
    /// used directly for the scroll maths.
    var visibleHeight: CGFloat = 0
    /// Side inset that centres the reading column inside a full-width text view,
    /// so the scroll bar rides the window's right edge out in the margin (like a
    /// desktop editor) instead of hugging the text. 0 on a narrow/phone window.
    var horizontalInset: CGFloat = 0

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
        tv.delegate = context.coordinator
        // Dictionary games: a long-press on a word shows its definition straight
        // away (like the Android app), instead of selecting text and making you
        // tap a "Define" menu item. The handler no-ops for games without a
        // glossary, which stay selectable for copy (see updateUIView).
        let press = UILongPressGestureRecognizer(
            target: context.coordinator, action: #selector(Coordinator.handleLongPress(_:)))
        press.delegate = context.coordinator
        tv.addGestureRecognizer(press)
        return tv
    }

    func updateUIView(_ tv: UITextView, context: Context) {
        context.coordinator.dictionary = dictionary
        // A dictionary game's long-press is owned by our Define gesture, so turn
        // off the built-in selection there (it would otherwise pop the system
        // edit menu and fight the definition popover). Other games stay
        // selectable so the transcript can still be copied.
        tv.isSelectable = (dictionary == nil)
        // Centre the reading column inside the full-width text view, so the
        // scroll bar sits at the window's right edge out in the margin (not
        // hugging the text). 0 on a narrow window, where the column fills it.
        if abs(tv.textContainerInset.left - horizontalInset) > 0.5 {
            tv.textContainerInset.left = horizontalInset
            tv.textContainerInset.right = horizontalInset
        }
        // Console-version scroll logic: when a command is submitted (turnCount
        // bumps), anchor on the CURRENT content height -- where this turn's output
        // will append -- before any of it arrives. Echo and response come in
        // separate updates, so the fixed anchor lets the whole turn scroll from
        // the command line: taller than a screen -> command at top; shorter ->
        // show it all at the bottom. Keyed off the submit, not the echo text, so
        // it's robust to the echo's exact format.
        if turnCount != context.coordinator.lastTurn {
            context.coordinator.lastTurn = turnCount
            tv.layoutManager.ensureLayout(for: tv.textContainer)
            context.coordinator.commandAnchor = tv.contentSize.height
            context.coordinator.userHasScrolled = false                 // new turn: auto-position again
        }
        // Rebuild when the text or the chosen font size changes, so an
        // in-progress selection isn't dropped by an unrelated SwiftUI update.
        let lastLen = paragraphs.last?.spans.reduce(0) { $0 + $1.text.count } ?? 0
        let sig = "\(paragraphs.count)|\(lastLen)|\(fontSize)"
        let contentChanged = sig != context.coordinator.lastSig
        // Re-position on a content change OR a viewport change -- the keyboard
        // shrinking visibleHeight after the scroll already ran is why a short
        // response landed behind the keyboard.
        let viewportChanged = abs(visibleHeight - context.coordinator.lastVisibleHeight) > 0.5
        guard contentChanged || viewportChanged else { return }
        context.coordinator.lastSig = sig
        context.coordinator.lastVisibleHeight = visibleHeight
        if contentChanged {
            let viewW = tv.bounds.width > 0 ? tv.bounds.width : UIScreen.main.bounds.width
            tv.attributedText = Self.attributed(paragraphs,
                                                baseFont: Self.transcriptFont(ofSize: fontSize),
                                                headerColor: headerColor,
                                                width: max(1, viewW - 2 * horizontalInset))
        }
        let anchor = context.coordinator.commandAnchor
        DispatchQueue.main.async {
            guard tv.attributedText.length > 0,
                  !context.coordinator.userHasScrolled else { return }   // don't fight a manual scroll
            Self.scrollToAnchor(tv, anchor: anchor, visibleH: visibleHeight)
        }
    }

    /// Position the latest output the way the console version does: if it's taller
    /// than the visible area, put its command line (`anchor`) at the top and let
    /// the rest scroll; if it fits, show it all (scrolled to the bottom). `anchor`
    /// is the content height captured just before this turn's text was appended.
    private static func scrollToAnchor(_ tv: UITextView, anchor: CGFloat, visibleH: CGFloat) {
        tv.layoutManager.ensureLayout(for: tv.textContainer)
        let contentH = tv.layoutManager.usedRect(for: tv.textContainer).maxY
            + tv.textContainerInset.top + tv.textContainerInset.bottom
        // SwiftUI measured the true visible height (keyboard-aware). Fall back to
        // the full bounds before the first measure. Inset the bottom by the hidden
        // part so a long response can still scroll fully clear of the keyboard.
        let v = visibleH > 1 ? visibleH : tv.bounds.height
        let occluded = max(0, tv.bounds.height - v)
        tv.contentInset.bottom = occluded
        tv.verticalScrollIndicatorInsets.bottom = occluded
        let bottom = max(0, contentH - v)
        let y = (contentH - anchor) > v ? min(anchor, bottom) : bottom
        tv.setContentOffset(CGPoint(x: 0, y: y), animated: false)
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    final class Coordinator: NSObject, UITextViewDelegate,
                             UIPopoverPresentationControllerDelegate, UIGestureRecognizerDelegate {
        var dictionary: GameDictionary?
        var lastSig = ""
        /// Content-y of the current command, so its whole output (echo + response,
        /// which arrive in separate updates) scrolls from one fixed anchor.
        var commandAnchor: CGFloat = 0
        var lastTurn = -1
        var lastVisibleHeight: CGFloat = -1
        /// Set once the player drags the transcript, so incidental re-renders (the
        /// prompt settling, the keyboard opening, a resize) don't yank the scroll
        /// back. Reset when a new command starts a fresh turn.
        var userHasScrolled = false

        func scrollViewWillBeginDragging(_ scrollView: UIScrollView) { userHasScrolled = true }

        // Long-press a word in a dictionary game and its definition appears
        // immediately in a popover -- no selection, no edit menu, no extra tap
        // (matching the Android app). Works any time, even mid-game, with no
        // command and no turn. No-ops for games without a glossary, which keep
        // the normal selectable/copy behaviour.
        @objc func handleLongPress(_ gesture: UILongPressGestureRecognizer) {
            guard gesture.state == .began, dictionary != nil,
                  let textView = gesture.view as? UITextView else { return }
            let point = gesture.location(in: textView)
            guard let position = textView.closestPosition(to: point) else { return }
            let offset = textView.offset(from: textView.beginningOfDocument, to: position)
            let text = textView.text as NSString
            guard let range = Coordinator.wordRange(in: text, at: offset) else { return }
            let word = text.substring(with: range)
            // Anchor the popover to the word's on-screen rect; fall back to the
            // touch point if a text range can't be formed.
            var anchor = CGRect(x: point.x, y: point.y, width: 1, height: 1)
            if let start = textView.position(from: textView.beginningOfDocument, offset: range.location),
               let end = textView.position(from: start, offset: range.length),
               let wordRange = textView.textRange(from: start, to: end) {
                anchor = textView.firstRect(for: wordRange)
            }
            UISelectionFeedbackGenerator().selectionChanged()   // acknowledge the press
            define(word, anchor: anchor, in: textView)
        }

        /// The word (run of letters/digits) surrounding a character offset, as an
        /// NSRange into `text`, or nil if the offset isn't on a word. Mirrors the
        /// Android app's `wordAt`.
        static func wordRange(in text: NSString, at offset: Int) -> NSRange? {
            guard text.length > 0 else { return nil }
            let letters = CharacterSet.alphanumerics
            func isWord(_ i: Int) -> Bool {
                guard i >= 0, i < text.length,
                      let scalar = Unicode.Scalar(text.character(at: i)) else { return false }
                return letters.contains(scalar)
            }
            var start = min(offset, text.length - 1)
            if !isWord(start) && isWord(start - 1) { start -= 1 }   // pressed just past a word
            guard isWord(start) else { return nil }
            var end = start
            while isWord(start - 1) { start -= 1 }
            while isWord(end + 1) { end += 1 }
            return NSRange(location: start, length: end - start + 1)
        }

        private func define(_ word: String, anchor: CGRect, in textView: UITextView) {
            let body = dictionary?.define(word).map { "\(word) — \($0)" }
                ?? "“\(word)” isn’t in the dictionary."
            let popover = DefinitionViewController(text: body)
            popover.modalPresentationStyle = .popover
            if let pop = popover.popoverPresentationController {
                pop.sourceView = textView
                pop.sourceRect = anchor
                pop.permittedArrowDirections = [.up, .down]
                pop.delegate = self
            }
            Coordinator.presenter(for: textView)?.present(popover, animated: true)
        }

        // Let the long-press coexist with the scroll view's pan, so scrolling the
        // transcript still works.
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
            shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }

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
                                   headerColor: UIColor?,
                                   width: CGFloat) -> NSAttributedString {
        let out = NSMutableAttributedString()
        // Full-bleed banner / in-game images span the text view's actual width --
        // the app *window* on Mac (not UIScreen, which is the whole monitor), and
        // the capped reading column in landscape (not the full screen).
        let screenW = width
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
        case "emphasized", "note", "blockquote": font = baseFont.withTraits(.traitItalic)
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
