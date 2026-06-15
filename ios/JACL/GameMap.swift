//  GameMap.swift
//  The explored map parsed from mapping.library's <jacl-map> data block.

import Foundation

/// A room node: (x,y) is the top-left in the web map's pixel space
/// (120 px/cell, 80 px nodes); `here` marks the current room.
struct MapNode { let x, y: Int; let here: Bool; let label: String }

/// An exit line between two room centres; `updown` draws an up/down arrow.
struct MapEdge { let x1, y1, x2, y2: Int; let updown: Bool }

/// The explored map: canvas `w`x`h` (px) plus nodes and edges.
struct GameMap { let w, h: Int; let nodes: [MapNode]; let edges: [MapEdge] }

/// Parse the lines between `<jacl-map>` and `</jacl-map>` (records "M w h",
/// "E x1 y1 x2 y2 ud", "N x y here label…"). nil if nothing is drawable.
func parseGameMap(_ lines: [String]) -> GameMap? {
    var w = 0, h = 0
    var nodes: [MapNode] = []
    var edges: [MapEdge] = []
    for raw in lines {
        let s = raw.trimmingCharacters(in: .whitespaces)
        if s.hasPrefix("M ") {
            let p = s.split(separator: " ")
            if p.count >= 3 { w = Int(p[1]) ?? 0; h = Int(p[2]) ?? 0 }
        } else if s.hasPrefix("E ") {
            let p = s.split(separator: " ")
            if p.count >= 6, let x1 = Int(p[1]), let y1 = Int(p[2]),
               let x2 = Int(p[3]), let y2 = Int(p[4]) {
                edges.append(MapEdge(x1: x1, y1: y1, x2: x2, y2: y2, updown: p[5] == "1"))
            }
        } else if s.hasPrefix("N ") {
            // "N x y here label…" -- label (rest) may contain spaces.
            let p = s.split(separator: " ", maxSplits: 4, omittingEmptySubsequences: true)
            if p.count >= 5, let x = Int(p[1]), let y = Int(p[2]) {
                nodes.append(MapNode(x: x, y: y, here: p[3] == "1", label: String(p[4])))
            }
        }
    }
    guard !nodes.isEmpty else { return nil }
    return GameMap(w: max(w, 1), h: max(h, 1), nodes: nodes, edges: edges)
}
