package au.com.dangarmarine.jacl

import android.media.MediaDataSource
import android.media.MediaPlayer
import android.util.Log

/**
 * Plays the game's sound channels. The interpreter (via RemGlk's rgschan.c)
 * sends play/stop/volume ops keyed by channel id; we resolve each sound number
 * to its blorb bytes ([sound]) and play it on a per-channel MediaPlayer.
 *
 * Blorb sounds are Ogg Vorbis / WAV (Android decodes these natively) fed
 * straight from memory via a MediaDataSource -- no temp files.
 */
class JaclAudio(private val sound: (Int) -> ByteArray?) {

    private val players = HashMap<Int, MediaPlayer>()
    /** Last requested volume per channel (0..1), applied once prepared. */
    private val volumes = HashMap<Int, Float>()
    /** When true (the Sound setting is off), play ops are ignored. */
    var muted = false
        set(value) {
            field = value
            if (value) releaseAll()
        }

    /** Play sound [snd] on [chan]; loops if [repeats] is -1. [vol] is the Glk
     *  0..0x10000 channel volume. Replaces whatever was on the channel. */
    fun play(chan: Int, snd: Int, repeats: Int, vol: Int) {
        stop(chan)
        if (muted) return
        val bytes = sound(snd) ?: return
        val v = (vol / 65536f).coerceIn(0f, 1f)
        volumes[chan] = v
        try {
            val mp = MediaPlayer()
            mp.setDataSource(ByteArrayMediaDataSource(bytes))
            mp.isLooping = (repeats == -1)
            mp.setOnPreparedListener {
                it.setVolume(v, v)
                it.start()
            }
            mp.setOnCompletionListener {
                if (!it.isLooping) { it.release(); if (players[chan] === it) players.remove(chan) }
            }
            mp.setOnErrorListener { p, _, _ -> p.release(); if (players[chan] === p) players.remove(chan); true }
            players[chan] = mp
            mp.prepareAsync()
        } catch (e: Exception) {
            Log.e("JACL", "sound play failed", e)
        }
    }

    fun stop(chan: Int) {
        players.remove(chan)?.let {
            try { it.reset() } catch (_: Exception) {}
            it.release()
        }
    }

    fun setVolume(chan: Int, vol: Int) {
        val v = (vol / 65536f).coerceIn(0f, 1f)
        volumes[chan] = v
        try { players[chan]?.setVolume(v, v) } catch (_: Exception) {}
    }

    /** Release every channel (leaving a game / Restart). */
    fun releaseAll() {
        players.values.forEach { try { it.reset(); it.release() } catch (_: Exception) {} }
        players.clear()
        volumes.clear()
    }
}

/** Feeds an in-memory byte array to MediaPlayer (API 23+). */
private class ByteArrayMediaDataSource(private val data: ByteArray) : MediaDataSource() {
    override fun readAt(position: Long, buffer: ByteArray, offset: Int, size: Int): Int {
        if (position >= data.size) return -1
        val n = minOf(size, data.size - position.toInt())
        System.arraycopy(data, position.toInt(), buffer, offset, n)
        return n
    }
    override fun getSize(): Long = data.size.toLong()
    override fun close() {}
}
