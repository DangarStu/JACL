//  JaclAudio.swift
//  Plays the game's Glk sound channels.
//
//  The interpreter (via RemGlk's rgschan.c) emits play/stop/volume ops keyed by
//  channel id; we resolve each sound number to its blorb bytes, decode the Ogg
//  Vorbis to WAV (jacl_ogg_to_wav, AVFoundation can't play Ogg), and play it on
//  a per-channel AVAudioPlayer.

import Foundation
import AVFoundation

final class JaclAudio {
    /// Resolves a sound resource number to its raw blorb bytes (Ogg Vorbis).
    private let sound: (Int) -> Data?
    private var players: [Int: AVAudioPlayer] = [:]
    private var volumes: [Int: Float] = [:]

    /// When true (the Sound setting is off), play ops are ignored.
    var muted = false {
        didSet { if muted { stopAll() } }
    }

    init(sound: @escaping (Int) -> Data?) {
        self.sound = sound
        // Mix with other audio and respect the silent switch (ambient).
        try? AVAudioSession.sharedInstance().setCategory(.ambient, options: [.mixWithOthers])
        try? AVAudioSession.sharedInstance().setActive(true)
    }

    /// Play sound `snd` on `chan`; loops if `repeats` is -1. `vol` is the Glk
    /// 0..0x10000 channel volume. Replaces whatever was on the channel.
    func play(chan: Int, snd: Int, repeats: Int, vol: Int) {
        stop(chan: chan)
        guard !muted, let ogg = sound(snd), let wav = JaclAudio.oggToWav(ogg) else { return }
        let v = max(0, min(1, Float(vol) / 65536))
        volumes[chan] = v
        do {
            let player = try AVAudioPlayer(data: wav)
            player.volume = v
            player.numberOfLoops = (repeats == -1) ? -1 : 0
            player.prepareToPlay()
            player.play()
            players[chan] = player
        } catch {
            NSLog("JACL sound: %@", String(describing: error))
        }
    }

    func stop(chan: Int) {
        players[chan]?.stop()
        players[chan] = nil
    }

    func setVolume(chan: Int, vol: Int) {
        let v = max(0, min(1, Float(vol) / 65536))
        volumes[chan] = v
        players[chan]?.volume = v
    }

    /// Stop every channel (leaving a game / Restart / muting).
    func stopAll() {
        players.values.forEach { $0.stop() }
        players.removeAll()
    }

    /// Decode Ogg Vorbis bytes to WAV bytes AVAudioPlayer can take, via the C
    /// stb_vorbis helper. nil if the bytes aren't decodable.
    private static func oggToWav(_ ogg: Data) -> Data? {
        ogg.withUnsafeBytes { raw -> Data? in
            guard let base = raw.baseAddress else { return nil }
            var len: Int32 = 0
            guard let wav = jacl_ogg_to_wav(base, Int32(ogg.count), &len), len > 0 else { return nil }
            let data = Data(bytes: wav, count: Int(len))
            free(wav)
            return data
        }
    }
}
