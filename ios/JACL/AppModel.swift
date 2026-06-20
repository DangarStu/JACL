//  AppModel.swift
//  App-wide state shared across windows (the game window and, on Mac, the live
//  map window) and the Mac menu bar.
//
//  The reading screen (GameView) publishes the game it's running here, so the
//  menu-bar commands and the separate map window can reach the same live
//  GlkBridge. On iPhone/iPad this is still created and populated, but the map
//  stays a sheet and the in-game controls stay in the toolbar; it's the Catalyst
//  build that opens a real window and drives everything from the menu bar.

import SwiftUI

@MainActor final class AppModel: ObservableObject {
    /// Scene ids for the standalone Mac windows.
    static let mapWindowID = "jacl-map"
    static let settingsWindowID = "jacl-settings"

    /// The game currently on screen, or nil at the shelf. Set by GameView on
    /// appear and cleared on disappear.
    @Published var activeBridge: GlkBridge?
    @Published var activeGamePath: String?

    /// Whether the standalone map window is currently open, so a fresh map only
    /// *opens* it once -- after that the open window just redraws from gameMap.
    @Published var mapWindowOpen = false

    var hasActiveGame: Bool { activeBridge != nil }

    func setActive(_ bridge: GlkBridge, path: String) {
        activeBridge = bridge
        activeGamePath = path
    }

    /// Clear only if `bridge` is still the active one (a later game may have
    /// already replaced it before this one's onDisappear fires).
    func clearIfActive(_ bridge: GlkBridge) {
        if activeBridge === bridge {
            activeBridge = nil
            activeGamePath = nil
        }
    }

    /// Restart the active game: suppress its autosave, drop the autosave slot, and
    /// relaunch the terp from the intro. Mirrors the in-game Restart confirmation.
    func restartActiveGame() {
        guard let bridge = activeBridge, let path = activeGamePath else { return }
        jacl_autosave_set_suppressed(1)
        try? FileManager.default.removeItem(atPath: GlkBridge.autosavePath(forGamePath: path))
        bridge.restart()
    }

    /// Ask the active game to (re)emit its `<jacl-map>` block, refreshing the map.
    func requestMap() { activeBridge?.submitLine("map") }
}
