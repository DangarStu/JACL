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
}

struct GameView: View {
    let gamePath: String

    @StateObject private var bridge = GlkBridge()
    @State private var inputText = ""
    @State private var started = false
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
                NSLog("JDBG GameView.onAppear started=%@ path=%@",
                      String(started), (gamePath as NSString).lastPathComponent)
                guard !started else { return }
                started = true
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
        let paras = bridge.buffers[id] ?? []
        return ScrollViewReader { proxy in
            ScrollView {
                // LazyVStack, not VStack: a long transcript in a plain VStack
                // lays out every paragraph eagerly and the content layer grows
                // past CoreAnimation's max backing-store size ("Failed to create
                // image slot"), stalling the main thread (the swap-time hang).
                // Lazy only realises the visible paragraphs.
                LazyVStack(alignment: .leading, spacing: 8) {
                    ForEach(paras) { para in
                        paragraphView(para)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id(para.id)
                    }
                }
                .padding()
            }
            .onChange(of: paras.count) { _, _ in
                if let last = paras.last {
                    withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
        }
    }

    // MARK: Input

    @ViewBuilder private var inputBar: some View {
        if let req = bridge.pendingInput, req.type == "line" {
            HStack {
                TextField("…", text: $inputText)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .focused($inputFocused)
                    .onSubmit(submit)
                Button("Enter", action: submit)
            }
            .padding(8)
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

    private func paragraphText(_ para: RenderedParagraph) -> Text {
        para.spans.reduce(Text("")) { $0 + styled($1) }
    }

    /// A buffer paragraph. If it contains an image span, lay the spans out
    /// vertically (images become Image views); otherwise take the fast path of
    /// a single concatenated Text.
    @ViewBuilder
    private func paragraphView(_ para: RenderedParagraph) -> some View {
        if para.spans.contains(where: { $0.image != nil }) {
            VStack(alignment: .leading, spacing: 6) {
                ForEach(para.spans) { span in
                    if let num = span.image {
                        if let img = BlorbImageCache.shared.image(num) {
                            Image(uiImage: img)
                                .resizable()
                                .scaledToFit()
                                .frame(maxWidth: .infinity, alignment: .center)
                        }
                    } else if !span.text.isEmpty {
                        styled(span)
                    }
                }
            }
        } else {
            paragraphText(para)
        }
    }

    /// Map a Glk style name to SwiftUI text styling.
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
