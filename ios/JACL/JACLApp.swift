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

    @StateObject private var appModel = AppModel()

    var body: some Scene {
        WindowGroup {
            rootView.environmentObject(appModel)
        }
        .commands { GameCommands(appModel: appModel) }

        #if targetEnvironment(macCatalyst)
        // A persistent, live map window on Mac: keep it open beside the game and
        // it redraws as the map updates -- no modal sheet to open and dismiss.
        // (Catalyst uses the iOS SDK, so it's a WindowGroup, not a macOS `Window`.)
        WindowGroup("Map", id: AppModel.mapWindowID) {
            MapWindow().environmentObject(appModel)
        }
        #endif
    }

    @ViewBuilder private var rootView: some View {
        #if DEBUG
        // Debug-only hook: `simctl launch … -autoplay` opens the first imported
        // game straight into the game view, so the RemGlk bridge can be exercised
        // headlessly. Normal launches (and all release builds) show the shelf.
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

// MARK: - Mac menu-bar commands

/// Drives the game in the front window from the Mac menu bar. On Catalyst these
/// replace the in-game toolbar popovers/sheets, which crash UIKit when presented
/// over the focused command field; opening a menu resigns that field, so the
/// commands (and any follow-on dialog) are safe. Harmless on iPhone/iPad, where
/// the toolbar controls remain the primary path.
struct GameCommands: Commands {
    @ObservedObject var appModel: AppModel
    @AppStorage(ReadingDefaults.columnsKey) private var columns = ReadingDefaults.columns
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandMenu("Game") {
            Button("Bigger Text") {
                columns = max(ReadingDefaults.columnRange.lowerBound, columns - 4)
            }
            .keyboardShortcut("+", modifiers: .command)
            Button("Smaller Text") {
                columns = min(ReadingDefaults.columnRange.upperBound, columns + 4)
            }
            .keyboardShortcut("-", modifiers: .command)

            Divider()

            Button("Show Map") {
                appModel.requestMap()
                #if targetEnvironment(macCatalyst)
                openWindow(id: AppModel.mapWindowID)
                #endif
            }
            .keyboardShortcut("m", modifiers: [.command, .shift])
            .disabled(!appModel.hasActiveGame)

            Divider()

            Button("Restart Game") { appModel.restartActiveGame() }
                .disabled(!appModel.hasActiveGame)
        }
    }
}

#if targetEnvironment(macCatalyst)
// MARK: - Standalone map window (Mac)

/// The map window's content: observes the active game's bridge so it redraws
/// whenever the map data changes, and a toolbar Refresh re-asks for the map.
struct MapWindow: View {
    @EnvironmentObject var appModel: AppModel

    var body: some View {
        Group {
            if let bridge = appModel.activeBridge {
                MapWindowContent(bridge: bridge)
            } else {
                ContentUnavailableView("No Game Open", systemImage: "map",
                    description: Text("Open a game, then choose Game ▸ Show Map."))
            }
        }
        .navigationTitle("Map")
    }
}

private struct MapWindowContent: View {
    @ObservedObject var bridge: GlkBridge

    var body: some View {
        Group {
            if let map = bridge.gameMap {
                MapCanvas(map: map)
            } else {
                ContentUnavailableView("No Map Yet", systemImage: "map",
                    description: Text("Move around, then Refresh."))
            }
        }
        .toolbar {
            Button { bridge.submitLine("map") } label: {
                Label("Refresh", systemImage: "arrow.clockwise")
            }
        }
    }
}
#endif

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
                    List {
                        ForEach(games) { game in
                            NavigationLink(value: game) {
                                HStack(spacing: 6) {
                                    Text(game.title)
                                    // The language, like the online game list.
                                    Text("(\(game.language))")
                                        .font(.subheadline)
                                        .foregroundStyle(.secondary)
                                }
                            }
                            .listRowBackground(Color.clear)
                        }
                        .onDelete { offsets in
                            offsets.map { games[$0] }.forEach(GameLibrary.delete)
                            games.remove(atOffsets: offsets)
                        }
                    }
                    .scrollContentBackground(.hidden)   // let the artwork show through
                    .navigationDestination(for: Game.self) { game in
                        // The reading screen inherits the window scheme, which
                        // the Appearance setting drives (the stack modifier
                        // below). System follows the device's Light/Dark.
                        GameView(gamePath: game.url.path)
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
                if !games.isEmpty {
                    ToolbarItem(placement: .topBarTrailing) { EditButton() }
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
            // Render the shelf content dark for its cover-art watermark (light
            // text on a dark background), via an environment override so it
            // holds regardless of the window's actual scheme. The reading
            // screen's appearance is set from inside GameView itself.
            .environment(\.colorScheme, .dark)
        }
    }
}

// MARK: - Sandbox game storage

/// An imported game: its file, the display title, and the language it's in.
struct Game: Identifiable, Hashable {
    let url: URL
    let title: String
    let language: String
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
                        title: title(of: $0) ?? $0.deletingPathExtension().lastPathComponent,
                        language: language(of: $0)) }
    }

    /// The game's language name, from its `game_language` constant -- the same
    /// labels the online list shows. Anything unmapped or absent reads as
    /// English, matching the make-apache landing-page script's default.
    static func language(of url: URL) -> String {
        switch stringConstant("game_language", in: url)?.split(separator: "-").first?.lowercased() {
        case "id": return "Indonesian"
        case "fr": return "French"
        case "de": return "German"
        case "es": return "Spanish"
        default:   return "English"
        }
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

    /// Delete a game: its `.j2`, matching `.blorb`, and any bundled dictionary
    /// no longer used by another installed game (see `pruneDictionaries`).
    /// (To *update* a game, just re-import the new .jaclgame: the importer
    /// overwrites the same-named .j2/.blorb and adds any new data/ files.)
    static func delete(_ game: Game) {
        let fm = FileManager.default
        try? fm.removeItem(at: game.url)
        try? fm.removeItem(at: game.url.deletingPathExtension().appendingPathExtension("blorb"))
        pruneDictionaries(removing: game.url.deletingPathExtension().lastPathComponent)
    }

    /// Key for the dictionary manifest: game base name -> the `*_words.csv`
    /// files that game's package bundled. Lets `delete` garbage-collect a CSV
    /// once no installed game needs it, while a glossary shared by two games of
    /// the same language survives until the last one is removed.
    private static let manifestKey = "dictManifest"

    private static func dictManifest() -> [String: [String]] {
        UserDefaults.standard.dictionary(forKey: manifestKey) as? [String: [String]] ?? [:]
    }

    /// Drop `name` from the manifest, then delete any CSV it brought that no
    /// remaining game references. Skips pruning entirely if any installed game
    /// predates the manifest (imported by an older build): we can't tell which
    /// CSV such a game needs, so we keep them all rather than risk pulling a
    /// dictionary out from under it. Re-importing those games fills the manifest.
    private static func pruneDictionaries(removing name: String) {
        var manifest = dictManifest()
        let removed = manifest.removeValue(forKey: name) ?? []
        UserDefaults.standard.set(manifest, forKey: manifestKey)
        guard !removed.isEmpty else { return }

        let remaining = Set(games().map { $0.url.deletingPathExtension().lastPathComponent })
        guard remaining.isSubset(of: Set(manifest.keys)) else { return }

        let stillNeeded = Set(manifest.values.flatMap { $0 })
        let dataDir = documents.appendingPathComponent("data", isDirectory: true)
        for csv in removed where !stillNeeded.contains(csv) {
            try? FileManager.default.removeItem(at: dataDir.appendingPathComponent(csv))
        }
    }

    /// Unpack a `.jaclgame` (zip of `.j2` [+ `.blorb`]) into Documents/.
    private static func unpack(_ url: URL) throws -> URL? {
        let entries = try MiniZip.entries(of: Data(contentsOf: url))
        var game: URL?
        var bundledCSVs: [String] = []
        for entry in entries {
            let base = (entry.name as NSString).lastPathComponent
            let ext = (base as NSString).pathExtension.lowercased()
            let dest: URL
            switch ext {
            case "j2", "blorb":
                dest = documents.appendingPathComponent(base)   // flat at the sandbox root
            case "csv":
                // A dictionary for the click-to-define feature. The interpreter
                // opens it as `data/<lang>_words.csv` under the game dir, so it
                // must land in Documents/data/, not the root.
                let dataDir = documents.appendingPathComponent("data", isDirectory: true)
                try? FileManager.default.createDirectory(at: dataDir, withIntermediateDirectories: true)
                dest = dataDir.appendingPathComponent(base)
                bundledCSVs.append(base)
            default:
                continue   // ignore stray files
            }
            try? FileManager.default.removeItem(at: dest)
            try entry.data.write(to: dest)
            if ext == "j2" { game = dest }
        }
        // Record which dictionaries this game brought, so deleting it can later
        // garbage-collect them (but only once no other game still needs them).
        if let game {
            var manifest = dictManifest()
            manifest[game.deletingPathExtension().lastPathComponent] = bundledCSVs
            UserDefaults.standard.set(manifest, forKey: manifestKey)
        }
        return game
    }

    /// Copy the games bundled with the app into Documents. A game is (re)seeded
    /// when first seen OR when the bundled package changed (its size differs) --
    /// so an app update pushes updated games to existing installs without a
    /// delete+reinstall. Re-seeding overwrites the .j2/.blorb but leaves the
    /// player's .glksave saves alone (unpack only rewrites the package's files).
    static func installBundledStarters() {
        let defaults = UserDefaults.standard
        let bundled = Bundle.main.urls(forResourcesWithExtension: "jaclgame", subdirectory: nil) ?? []
        for url in bundled {
            let key = "seedSize_" + url.lastPathComponent
            let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? -1
            if size > 0, defaults.integer(forKey: key) == size { continue }   // unchanged
            _ = try? importGame(from: url)
            defaults.set(size, forKey: key)
        }
    }

    // MARK: Reading constants from a .j2

    /// Read a quoted `constant <name> "..."` value from a `.j2`. Release files
    /// XOR-obfuscate every line after the `#encrypted` marker (JACL's
    /// jacl_obfuscate is a byte-wise ^0xFF), so those are de-obfuscated before
    /// matching. Returns the first match, or nil. Scans only until the constant
    /// is found; the ones this reads (game_title, header_colour) sit near the
    /// top of the source.
    static func stringConstant(_ name: String, in url: URL) -> String? {
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
            if let v = quotedConstant(name, in: line) { return v }
        }
        return nil
    }

    /// The game's display title (`constant game_title "..."`).
    static func title(of url: URL) -> String? { stringConstant("game_title", in: url) }

    /// Extract the quoted value from a decoded `constant <name> "..."` line.
    private static func quotedConstant(_ name: String, in bytes: [UInt8]) -> String? {
        guard let line = String(bytes: bytes, encoding: .utf8) else { return nil }
        let parts = line.split(whereSeparator: { $0 == " " || $0 == "\t" })
        // game_title / header_colour are `constant`; game_language is a `string`.
        guard parts.count >= 2, parts[0] == "constant" || parts[0] == "string",
              parts[1] == name else { return nil }
        guard let a = line.firstIndex(of: "\"") else { return nil }
        let after = line.index(after: a)
        guard let b = line[after...].firstIndex(of: "\"") else { return nil }
        return String(line[after..<b])
    }
}
