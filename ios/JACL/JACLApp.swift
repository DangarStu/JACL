//  JACLApp.swift
//  App entry + the game shelf. Games are imported from Files into the app
//  sandbox (Documents/) and played from there.
//
//  Info.plist (generated from project.yml) declares iPad-only, file sharing,
//  open-in-place, and the .j2 document type / exported UTI so games can be
//  opened from Files and the share sheet.

import SwiftUI
import UniformTypeIdentifiers

@main
struct JACLApp: App {
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
    @State private var games: [Game] = GameLibrary.games()
    @State private var importing = false

    var body: some View {
        NavigationStack {
            Group {
                if games.isEmpty {
                    ContentUnavailableView(
                        "No games yet",
                        systemImage: "books.vertical",
                        description: Text("Tap + to import a JACL game (.j2) from Files."))
                } else {
                    List(games) { game in
                        NavigationLink(game.title) {
                            GameView(gamePath: game.url.path)
                        }
                    }
                }
            }
            .navigationTitle("JACL")
            .toolbar {
                Button { importing = true } label: { Image(systemName: "plus") }
            }
            .fileImporter(isPresented: $importing,
                          allowedContentTypes: [GameLibrary.gameType],
                          allowsMultipleSelection: true) { result in
                if case .success(let urls) = result {
                    for url in urls { try? GameLibrary.importGame(from: url) }
                    games = GameLibrary.games()
                }
            }
        }
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
    /// The UTI to import. Falls back to plain data until the exported type is
    /// declared in Info.plist.
    static var gameType: UTType { UTType(filenameExtension: "j2") ?? .data }

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

    /// Copy a picked file into Documents/ so the terp has persistent,
    /// seekable access (and a place beside it for saves). The picker hands
    /// us a transient security-scoped URL, so copy under scoped access.
    @discardableResult
    static func importGame(from url: URL) throws -> URL {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }

        let dest = documents.appendingPathComponent(url.lastPathComponent)
        if FileManager.default.fileExists(atPath: dest.path) {
            try FileManager.default.removeItem(at: dest)
        }
        try FileManager.default.copyItem(at: url, to: dest)
        return dest
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
