//  RemGlkProtocol.swift
//  Codable models for the RemGlk JSON protocol.
//
//  These are written directly against real output captured from the
//  iosjacl-remglk desktop build (see ios/README.md, "The RemGlk JSON
//  contract"). RemGlk implements Glk 0.7.6; full spec:
//  https://www.eblong.com/zarf/glk/remglk/docs.html
//
//  STATUS: v0 scaffold. Assemble in Xcode; not yet compiled.

import Foundation

// MARK: - Incoming  (RemGlk -> app)

/// One message from RemGlk. `type` is "update" in normal play, or "error".
struct GlkUpdate: Decodable {
    let type: String
    let gen: Int?
    let windows: [GlkWindow]?
    let content: [GlkContent]?
    let input: [GlkInput]?
    let specialinput: GlkSpecialInput?   // present when the game prompts for a file (save/restore)
    let disable: Bool?
    let message: String?          // present on {"type":"error","message":…}
}

/// A file-reference prompt: the game called save/restore and RemGlk is waiting
/// for a filename. `filemode` is "write" (saving) or "read" (restoring);
/// `filetype` is "save" for the save/restore verbs.
struct GlkSpecialInput: Decodable, Equatable {
    let type: String              // "fileref_prompt"
    let filemode: String?         // "write" | "read" | "readwrite" | "writeappend"
    let filetype: String?         // "save" | "transcript" | "command" | "data"
}

/// Geometry + kind of one window. Coordinates are in the same units we send
/// in the `init`/`arrange` metrics (we use points).
struct GlkWindow: Decodable, Identifiable {
    let id: Int
    let type: String              // "grid" | "buffer" | "graphics"
    let rock: Int?
    let gridwidth: Int?           // grid windows: size in characters
    let gridheight: Int?
    let left: Double
    let top: Double
    let width: Double
    let height: Double
}

/// Content for one window: grid windows carry `lines`, buffer windows `text`.
struct GlkContent: Decodable {
    let id: Int
    let clear: Bool?
    let lines: [GlkGridLine]?     // grid windows (absolute, by row)
    let text: [GlkParagraph]?     // buffer windows (append / new paragraphs)
}

/// One row of a grid window.
struct GlkGridLine: Decodable {
    let line: Int
    let content: [GlkSpan]?       // nil => blank row
}

/// One paragraph in a buffer window. `append:true` continues the previous
/// line; otherwise it starts a new one. Empty (`{}`) is a blank line.
struct GlkParagraph: Decodable {
    let append: Bool?
    let flowbreak: Bool?
    let content: [GlkSpan]?
}

/// A run of styled text, a hyperlink, or an inline image.
struct GlkSpan: Decodable {
    // text run
    let style: String?            // "normal" | "user1" | "alert" | "header" | …
    let text: String?
    let hyperlink: Int?           // linkval; non-nil => tappable
    // image run (buffer/graphics): {"special":"image","image":N,…}
    let special: String?
    let image: Int?
    let width: Double?
    let height: Double?
    let alttext: String?
}

/// A pending input request on a window.
struct GlkInput: Decodable, Equatable {
    let id: Int
    let gen: Int?
    let type: String              // "line" | "char"
    let maxlen: Int?
    let initial: String?
}

// MARK: - Outgoing  (app -> RemGlk)

/// Display metrics. RemGlk uses these to lay out windows and to compute grid
/// sizes (gridwidth = floor(width / charwidth), roughly).
struct GlkMetrics {
    var width: Double
    var height: Double
    var charwidth: Double         // fixed-font cell size, in the same units
    var charheight: Double

    var dictionary: [String: Any] {
        ["width": width, "height": height,
         "charwidth": charwidth, "charheight": charheight,
         "buffercharwidth": charwidth, "buffercharheight": charheight]
    }
}

/// An event we send back to RemGlk. Each is one JSON object.
enum GlkEvent {
    case initialize(metrics: GlkMetrics, support: [String])
    case arrange(gen: Int, metrics: GlkMetrics)
    case line(gen: Int, window: Int, value: String)
    case char(gen: Int, window: Int, value: String)
    case hyperlink(gen: Int, window: Int, value: Int)
    case redraw(gen: Int)
    /// Answer a `fileref_prompt` (save/restore). `value` is the bare filename
    /// the player chose; RemGlk confines it to the game's sandbox dir and
    /// appends ".glksave". An empty value cancels (no file -> the game reports
    /// it couldn't save/restore).
    case specialResponse(gen: Int, value: String)

    private var dictionary: [String: Any] {
        switch self {
        case let .initialize(metrics, support):
            return ["type": "init", "gen": 0,
                    "metrics": metrics.dictionary, "support": support]
        case let .arrange(gen, metrics):
            return ["type": "arrange", "gen": gen, "metrics": metrics.dictionary]
        case let .line(gen, window, value):
            return ["type": "line", "gen": gen, "window": window, "value": value]
        case let .char(gen, window, value):
            return ["type": "char", "gen": gen, "window": window, "value": value]
        case let .hyperlink(gen, window, value):
            return ["type": "hyperlink", "gen": gen, "window": window, "value": value]
        case let .redraw(gen):
            return ["type": "redraw", "gen": gen]
        case let .specialResponse(gen, value):
            return ["type": "specialresponse", "gen": gen,
                    "response": "fileref_prompt", "value": value]
        }
    }

    var isArrange: Bool {
        if case .arrange = self { return true }
        return false
    }

    /// A copy with the generation stamped to `gen`. Every outgoing event must
    /// carry the generation of the most recently received update, or RemGlk
    /// aborts with "Input generation number does not match." (`init` is always
    /// generation 0, so it is returned unchanged.)
    func restamped(gen: Int) -> GlkEvent {
        switch self {
        case .initialize:             return self
        case let .arrange(_, m):      return .arrange(gen: gen, metrics: m)
        case let .line(_, w, v):      return .line(gen: gen, window: w, value: v)
        case let .char(_, w, v):      return .char(gen: gen, window: w, value: v)
        case let .hyperlink(_, w, v): return .hyperlink(gen: gen, window: w, value: v)
        case .redraw:                 return .redraw(gen: gen)
        case let .specialResponse(_, v): return .specialResponse(gen: gen, value: v)
        }
    }

    /// JSON bytes for this event, newline-terminated. RemGlk reads one
    /// balanced JSON object per event (rgdata.c: data_raw_blockread).
    func jsonData() -> Data {
        let obj = try? JSONSerialization.data(withJSONObject: dictionary, options: [])
        return (obj ?? Data()) + Data([0x0A])
    }
}
