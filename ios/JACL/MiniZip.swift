//  MiniZip.swift
//  A tiny, dependency-free reader for the slice of ZIP we need: flat files,
//  stored or DEFLATE-compressed. Enough to unpack a `.jaclgame` (a zip of a
//  `.j2` plus an optional `.blorb`). Not a general ZIP library -- it ignores
//  directories and keeps only each entry's basename.
//
//  DEFLATE is handled by the system Compression framework (COMPRESSION_ZLIB is
//  raw DEFLATE per RFC 1951, which is exactly ZIP method 8), so there is no
//  third-party dependency.

import Foundation
import Compression

enum MiniZip {
    struct Entry { let name: String; let data: Data }
    enum ZipError: Error { case notZip, badEntry, inflateFailed }

    /// Extract every (flat) file from a zip's bytes.
    static func entries(of zip: Data) throws -> [Entry] {
        let b = [UInt8](zip)
        guard let eocd = findEOCD(b) else { throw ZipError.notZip }

        let count = readU16(b, eocd + 10)
        var p = Int(readU32(b, eocd + 16))            // central-directory offset
        var result: [Entry] = []

        for _ in 0..<count {
            guard p + 46 <= b.count, readU32(b, p) == 0x0201_4b50 else { throw ZipError.badEntry }
            let method     = readU16(b, p + 10)
            let compSize   = Int(readU32(b, p + 20))
            let uncompSize = Int(readU32(b, p + 24))
            let nameLen    = readU16(b, p + 28)
            let extraLen   = readU16(b, p + 30)
            let commentLen = readU16(b, p + 32)
            let localOff   = Int(readU32(b, p + 42))
            let name = String(bytes: b[(p + 46)..<(p + 46 + nameLen)], encoding: .utf8) ?? ""
            p += 46 + nameLen + extraLen + commentLen

            if name.isEmpty || name.hasSuffix("/") { continue }   // skip directories

            // Local header: payload starts past its own name + extra fields.
            guard localOff + 30 <= b.count, readU32(b, localOff) == 0x0403_4b50 else { throw ZipError.badEntry }
            let dataStart = localOff + 30 + readU16(b, localOff + 26) + readU16(b, localOff + 28)
            guard dataStart + compSize <= b.count else { throw ZipError.badEntry }
            let comp = Array(b[dataStart..<(dataStart + compSize)])

            let out: Data
            switch method {
            case 0: out = Data(comp)                               // stored
            case 8: out = try inflate(comp, expected: uncompSize)  // deflate
            default: throw ZipError.badEntry
            }
            result.append(Entry(name: (name as NSString).lastPathComponent, data: out))
        }
        return result
    }

    // MARK: - helpers

    private static func findEOCD(_ b: [UInt8]) -> Int? {
        guard b.count >= 22 else { return nil }
        var i = b.count - 22
        let lo = max(0, b.count - 22 - 0xFFFF)        // max comment length
        while i >= lo {
            if readU32(b, i) == 0x0605_4b50 { return i }
            i -= 1
        }
        return nil
    }

    private static func readU16(_ b: [UInt8], _ o: Int) -> Int {
        Int(b[o]) | (Int(b[o + 1]) << 8)
    }
    private static func readU32(_ b: [UInt8], _ o: Int) -> UInt32 {
        UInt32(b[o]) | (UInt32(b[o + 1]) << 8) | (UInt32(b[o + 2]) << 16) | (UInt32(b[o + 3]) << 24)
    }

    private static func inflate(_ src: [UInt8], expected: Int) throws -> Data {
        if expected == 0 { return Data() }
        var dst = [UInt8](repeating: 0, count: expected)
        let n = src.withUnsafeBufferPointer { s in
            dst.withUnsafeMutableBufferPointer { d in
                compression_decode_buffer(d.baseAddress!, expected,
                                          s.baseAddress!, src.count,
                                          nil, COMPRESSION_ZLIB)
            }
        }
        guard n == expected else { throw ZipError.inflateFailed }
        return Data(dst)
    }
}
