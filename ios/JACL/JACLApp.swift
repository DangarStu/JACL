//  JACLApp.swift
//  App entry + the game shelf. Games are imported from Files into the app
//  sandbox (Documents/) and played from there.
//
//  Info.plist for the target should declare iPad-only (UIDeviceFamily=[2]),
//  LSSupportsOpeningDocumentsInPlace=YES, UIFileSharingEnabled=YES, and a
//  document type / exported UTI for the .j2 (and later .jaclgame) extension
//  so games can be opened from Files / the share sheet.
//
//  STATUS: v0 scaffold. Assemble in Xcode; not yet compiled.

import SwiftUI
import UniformTypeIdentifiers

@main
struct JACLApp: App {
    var body: some Scene {
        WindowGroup {
            GameShelfView()
        }
    }
}

// MARK: - Shelf

struct GameShelfView: View {
    @State private var games: [URL] = GameLibrary.games()
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
                    List(games, id: \.self) { url in
                        NavigationLink(url.deletingPathExtension().lastPathComponent) {
                            GameView(gamePath: url.path)
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

enum GameLibrary {
    /// The UTI to import. Falls back to plain data until the exported type is
    /// declared in Info.plist.
    static var gameType: UTType { UTType(filenameExtension: "j2") ?? .data }

    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    /// Imported `.j2` games, newest first.
    static func games() -> [URL] {
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
}
