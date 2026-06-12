//  JACLApp.swift
//  App entry + the game shelf. Games are imported from Files into the app
//  sandbox (Documents/) and played from there.
//
//  Info.plist (generated from project.yml) declares iPad-only, file sharing,
//  open-in-place, and the .j2 document type / exported UTI so games can be
//  opened from Files and the share sheet.

import SwiftUI
import UniformTypeIdentifiers
import Darwin

@main
struct JACLApp: App {
    init() {
        // The terp talks to the app over a socketpair. When a game closes we
        // close our end; the terp's final writes (its glk_exit flush) would
        // otherwise raise SIGPIPE and kill the whole app. Ignore it process-
        // wide so those writes just fail with EPIPE instead.
        signal(SIGPIPE, SIG_IGN)
        // NB: do NOT seed bundled games here -- installBundledStarters() unzips
        // on first launch and would block the main thread before any UI paints
        // (a ~10s black screen on a fresh install). The shelf does it in .task.
    }

    var body: some Scene {
        WindowGroup {
            #if DEBUG
            // Debug-only hook: `simctl launch … -autoplay` opens the first
            // imported game straight into the game view, so the RemGlk bridge
            // can be exercised headlessly. Normal launches (and all release
            // builds) show the shelf.
            if ProcessInfo.processInfo.arguments.contains("-autoplay"),
               let game = GameLibrary.games().first {
                GameView(gamePath: game.url.path)
            } else {
                GameShelfView()
            }
            #else
            GameShelfView()
            #endif
        }
    }
}

// MARK: - Shelf

struct GameShelfView: View {
    @State private var games: [Game] = []
    @State private var loaded = false
    @State private var importing = false
    @State private var showingSettings = false

    /// Interpreter version (from version.h via the C bridge) and this build's
    /// link time, shown so you can confirm at a glance which build is running.
    private static let interpreterVersion = String(cString: jacl_interpreter_version())
    private static let buildStamp: String = {
        guard let url = Bundle.main.executableURL,
              let date = try? url.resourceValues(
                forKeys: [.contentModificationDateKey]).contentModificationDate
        else { return "?" }
        let f = DateFormatter()
        f.dateFormat = "MMM d HH:mm:ss"
        return f.string(from: date)
    }()

    var body: some View {
        NavigationStack {
            Group {
                if !loaded {
                    ProgressView("Loading…")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if games.isEmpty {
                    ContentUnavailableView(
                        "No games yet",
                        systemImage: "books.vertical",
                        description: Text("Tap + to import a JACL game (.j2) from Files."))
                } else {
                    // Value-based navigation: each pushed GameView is keyed to a
                    // distinct Game, so its @StateObject bridge / @State started
                    // are fresh per game. The older `NavigationLink { GameView }`
                    // form let SwiftUI share one destination's state across games.
                    List(games) { game in
                        NavigationLink(game.title, value: game)
                            .listRowBackground(Color.clear)
                    }
                    .scrollContentBackground(.hidden)   // let the artwork show through
                    .navigationDestination(for: Game.self) { game in
                        // Keep the in-game transcript on the light theme; only
                        // the shelf goes dark for the watermark.
                        GameView(gamePath: game.url.path)
                            .preferredColorScheme(.light)
                    }
                }
            }
            .background {
                // App artwork as a dark, muted watermark behind the shelf: the
                // full image dimmed under a black scrim, rather than a pale wash
                // (which a low opacity over the light system background gives).
                ZStack {
                    Color.black
                    // Fit the square artwork to the width (full image, centred),
                    // leaving black gaps above and below rather than cropping it.
                    Image("ShelfArtwork")
                        .resizable()
                        .scaledToFit()
                        .opacity(0.55)
                    Color.black.opacity(0.4)
                }
                .ignoresSafeArea()
                .allowsHitTesting(false)
            }
            .navigationTitle("JACL v\(Self.interpreterVersion)")
            .safeAreaInset(edge: .bottom) {
                // Build stamp: interpreter version + this build's link time, so
                // you can verify on-device that you're running the latest build.
                Text("v\(Self.interpreterVersion) · built \(Self.buildStamp)")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 4)
                    .background(.bar)
            }
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button { showingSettings = true } label: { Image(systemName: "gearshape") }
                        .accessibilityLabel("Settings")
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button { importing = true } label: { Image(systemName: "plus") }
                        .accessibilityLabel("Import game")
                }
            }
            .sheet(isPresented: $showingSettings) { SettingsView() }
            .fileImporter(isPresented: $importing,
                          allowedContentTypes: GameLibrary.gameTypes,
                          allowsMultipleSelection: true) { result in
                if case .success(let urls) = result {
                    for url in urls { _ = try? GameLibrary.importGame(from: url) }
                    games = GameLibrary.games()
                }
            }
            .onOpenURL { url in
                // "Open in JACL" from Files / AirDrop / Mail / Safari, or a
                // .jaclgame / .j2 tapped anywhere on the device.
                _ = try? GameLibrary.importGame(from: url)
                games = GameLibrary.games()
            }
            .onAppear {
                #if DEBUG
                // Debug: `simctl launch … -importpack grail.jaclgame` imports a
                // package already sitting in Documents (openurl can't drive the
                // share-sheet path on the simulator headlessly).
                let args = ProcessInfo.processInfo.arguments
                if let i = args.firstIndex(of: "-importpack"), i + 1 < args.count {
                    let url = GameLibrary.documents.appendingPathComponent(args[i + 1])
                    _ = try? GameLibrary.importGame(from: url)
                    games = GameLibrary.games()
                }
                #endif
            }
            .task {
                guard !loaded else { return }
                // First launch seeds the bundled starters (a zip unpack) and the
                // title scan reads every .j2. Do both off the main thread so the
                // shelf paints immediately (ProgressView) instead of a ~10s black
                // screen, then publish the result.
                let result = await Task.detached(priority: .userInitiated) { () -> [Game] in
                    GameLibrary.installBundledStarters()
                    return GameLibrary.games()
                }.value
                games = result
                loaded = true
            }
        }
        .preferredColorScheme(.dark)   // light text on the dark shelf watermark
    }
}

// MARK: - Sandbox game storage

/// An imported game: its file plus the display title read from the `.j2`.
struct Game: Identifiable, Hashable {
    let url: URL
    let title: String
    var id: URL { url }
}

enum GameLibrary {
    /// Types the importer accepts: a bare `.j2`, a `.jaclgame` package (zip of
    /// .j2 [+ .blorb]), or a `.blorb` to sit beside an imported `.j2`.
    static var gameTypes: [UTType] {
        ["j2", "jaclgame", "blorb"].compactMap { UTType(filenameExtension: $0) }
    }

    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    /// Imported `.j2` games, newest first, each with its title resolved.
    static func games() -> [Game] {
        let keys: [URLResourceKey] = [.contentModificationDateKey]
        let urls = (try? FileManager.default.contentsOfDirectory(
            at: documents, includingPropertiesForKeys: keys)) ?? []
        return urls
            .filter { $0.pathExtension.lowercased() == "j2" }
            .sorted { a, b in
                let da = (try? a.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                let db = (try? b.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                return da > db
            }
            .map { Game(url: $0,
                        title: title(of: $0) ?? $0.deletingPathExtension().lastPathComponent) }
    }

    /// Import a picked or opened file into Documents/. A `.jaclgame` (or `.zip`)
    /// is unpacked into its `.j2` + `.blorb`; a bare `.j2`/`.blorb` is copied.
    /// The picker/share-sheet hands us a transient security-scoped URL, so we
    /// read under scoped access. Returns the playable `.j2`, if one resulted.
    @discardableResult
    static func importGame(from url: URL) throws -> URL? {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }

        let ext = url.pathExtension.lowercased()
        if ext == "jaclgame" || ext == "zip" {
            return try unpack(url)
        }
        let dest = documents.appendingPathComponent(url.lastPathComponent)
        try? FileManager.default.removeItem(at: dest)
        try FileManager.default.copyItem(at: url, to: dest)
        return ext == "j2" ? dest : nil
    }

    /// Unpack a `.jaclgame` (zip of `.j2` [+ `.blorb`]) into Documents/.
    private static func unpack(_ url: URL) throws -> URL? {
        let entries = try MiniZip.entries(of: Data(contentsOf: url))
        var game: URL?
        for entry in entries {
            let name = (entry.name as NSString).lastPathComponent
            let ext = (name as NSString).pathExtension.lowercased()
            guard ext == "j2" || ext == "blorb" else { continue }   // ignore stray files
            let dest = documents.appendingPathComponent(name)
            try? FileManager.default.removeItem(at: dest)
            try entry.data.write(to: dest)
            if ext == "j2" { game = dest }
        }
        return game
    }

    /// Copy the games bundled with the app into Documents on first launch.
    /// Tracks which starters have been seeded (by filename) so deleting one
    /// doesn't bring it back, while a new starter shipped in an app update
    /// still seeds once.
    static func installBundledStarters() {
        let key = "seededStarters"
        var seeded = Set(UserDefaults.standard.stringArray(forKey: key) ?? [])
        let bundled = Bundle.main.urls(forResourcesWithExtension: "jaclgame", subdirectory: nil) ?? []
        for url in bundled where !seeded.contains(url.lastPathComponent) {
            _ = try? importGame(from: url)
            seeded.insert(url.lastPathComponent)
        }
        UserDefaults.standard.set(Array(seeded), forKey: key)
    }

    // MARK: Title extraction

    /// Read `constant game_title "..."` from a `.j2`. Release files XOR-
    /// obfuscate every line after the `#encrypted` marker (JACL's
    /// jacl_obfuscate is a byte-wise ^0xFF), so those are de-obfuscated before
    /// matching. Returns nil if no title line is found. Scans only until the
    /// title line, which sits near the top of the game source.
    static func title(of url: URL) -> String? {
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else { return nil }
        let marker = Array("#encrypted".utf8)
        var encrypted = false
        var lineStart = 0
        let count = data.count
        var i = 0
        while i < count {
            if data[i] != 0x0A { i += 1; continue }
            var line = [UInt8](data[lineStart..<i])
            lineStart = i + 1
            i += 1
            if encrypted {
                for j in line.indices { line[j] ^= 0xFF }
            } else if line.starts(with: marker) {
                encrypted = true
                continue
            }
            if let t = titleInLine(line) { return t }
        }
        return nil
    }

    /// Extract the title from a (decoded) `constant game_title "..."` line.
    private static func titleInLine(_ bytes: [UInt8]) -> String? {
        guard let line = String(bytes: bytes, encoding: .utf8) else { return nil }
        let parts = line.split(whereSeparator: { $0 == " " || $0 == "\t" })
        guard parts.count >= 2, parts[0] == "constant", parts[1] == "game_title" else { return nil }
        guard let a = line.firstIndex(of: "\"") else { return nil }
        let after = line.index(after: a)
        guard let b = line[after...].firstIndex(of: "\"") else { return nil }
        return String(line[after..<b])
    }
}
