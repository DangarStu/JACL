package au.com.dangarmarine.jacl

import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONArray
import org.json.JSONObject
import java.io.FileInputStream
import java.io.FileOutputStream
import java.util.concurrent.Executors
import kotlin.concurrent.thread

/** A rendered run of text (or an image) in the transcript. Mirrors the Swift
 *  RenderedSpan. */
data class RenderedSpan(
    val text: String,
    val style: String,
    val hyperlink: Int?,
    val image: Int?,
)

/** One transcript paragraph: a list of spans. */
class RenderedParagraph(val spans: MutableList<RenderedSpan>)

/** The pending input request the terp is waiting on. */
data class GlkInput(val id: Int, val type: String)

/** A pending save/restore file prompt: the game called save/restore and the
 *  terp is waiting for a filename. [filemode] is "write" (saving) or "read"
 *  (restoring). Mirrors the iOS GlkSpecialInput. */
data class GlkSpecialInput(val filemode: String, val filetype: String)

/** A window in the current layout (we only need id + kind). */
data class GlkWindow(val id: Int, val type: String)

/**
 * Runs the embedded RemGlk interpreter (libjacl.so) and bridges its JSON to
 * Compose state -- the Android counterpart of GlkBridge.swift.
 *
 * The native nativeStart() makes a socketpair, runs jacl_bridge_run() on a
 * terp thread, and returns the app-side fd; we wrap it in a
 * ParcelFileDescriptor and exchange RemGlk JSON over it.
 */
class GlkBridge {

    // --- Native entry points (android_jni.c) --------------------------------
    private external fun nativeStart(gamePath: String): Int
    external fun nativeImage(num: Int): ByteArray?
    private external fun nativeSound(num: Int): ByteArray?
    external fun nativeVersion(): String
    private external fun nativeSetAutosaveSuppressed(suppressed: Boolean)

    /** Sound-channel playback, fed sound bytes straight from the blorb. */
    private val audio = JaclAudio { num -> nativeSound(num) }

    /** Suppress/allow the autosave that fires when the socket closes (Restart). */
    fun setAutosaveSuppressed(suppressed: Boolean) = nativeSetAutosaveSuppressed(suppressed)

    /** Apply the persistent Sound setting: when off, channel ops are ignored. */
    fun setSoundEnabled(on: Boolean) { audio.muted = !on }

    companion object {
        init { System.loadLibrary("jacl") }
        private const val TAG = "JACL"
        /** Interpreter version, read once for the shelf. */
        val version: String by lazy { GlkBridge().nativeVersion() }

        // Every game shares one sandbox dir, so all of a game's saves are
        // prefixed with its base name ("<base>_"): a save "start" becomes
        // "dragon_start.glksave", so the same name works across games. The
        // autosave slot is "<base>__auto.glksave", excluded from the list.
        // These must match the interpreter's `prefix` (the .j2 basename).

        /** The game's base name, e.g. "dragon" for "dragon.j2". */
        fun gameBase(gameFile: java.io.File): String = gameFile.nameWithoutExtension

        /** The game's silent autosave slot file. */
        fun autosaveFile(gameFile: java.io.File): java.io.File =
            java.io.File(gameFile.parentFile, gameBase(gameFile) + "__auto.glksave")

        /** The fileref value to send for a player-entered save [name]. */
        fun saveValue(gameFile: java.io.File, name: String): String =
            gameBase(gameFile) + "_" + name

        /** Display names (game prefix stripped, autosave excluded) of this
         *  game's named saves, newest first. */
        fun savedGames(gameFile: java.io.File): List<String> {
            val prefix = gameBase(gameFile) + "_"
            val autoBare = gameBase(gameFile) + "__auto"
            return (gameFile.parentFile?.listFiles { f -> f.extension == "glksave" } ?: emptyArray())
                .sortedByDescending { it.lastModified() }
                .map { it.nameWithoutExtension }
                .filter { it.startsWith(prefix) && it != autoBare }
                .map { it.removePrefix(prefix) }
        }
    }

    // --- Published display model (drives Compose) ---------------------------
    var windows by mutableStateOf<List<GlkWindow>>(emptyList()); private set
    var buffers by mutableStateOf<Map<Int, List<RenderedParagraph>>>(emptyMap()); private set
    var grids by mutableStateOf<Map<Int, List<List<RenderedSpan>>>>(emptyMap()); private set
    var pendingInput by mutableStateOf<GlkInput?>(null); private set
    /** A pending save/restore file prompt, if the game is waiting for a
     *  filename. Drives the name dialog / restore picker. */
    var pendingFilePrompt by mutableStateOf<GlkSpecialInput?>(null); private set
    /** The latest explored map (from the `map` command), or null if none yet. */
    var gameMap by mutableStateOf<GameMap?>(null); private set
    var finished by mutableStateOf(false); private set

    // --- Plumbing -----------------------------------------------------------
    private val main = Handler(Looper.getMainLooper())
    private val writer = Executors.newSingleThreadExecutor()
    private var pfd: ParcelFileDescriptor? = null
    private var output: FileOutputStream? = null
    private var generation = 0

    /** RemGlk wants exactly one event per update; queue while awaiting. */
    private var awaiting = false
    private val outQueue = ArrayDeque<JSONObject>()
    private var splitter = JsonObjectStream()
    /** Remembered launch args so the game can be restarted in place. */
    private var gamePath = ""
    /** Current Glk timer interval in ms (0 = off). The game sets it via the
     *  "timer" field of an update; we then post a {"type":"timer"} event every
     *  interval until it's cancelled (a null "timer"). */
    private var timerIntervalMs = 0

    private var fontSize = 17.0
    /** Status-grid cell metrics are measured at this size by the UI and set
     *  here before start()/resize(), matching the iOS statusFontSize. */
    fun setFontSize(pt: Double) { fontSize = pt }

    private var sizeW = 0
    private var sizeH = 0
    private var cellW = 10.0
    private var cellH = 20.0

    /** Launch [gamePath] and send the initial metrics for the given pixel size
     *  and measured monospaced cell. */
    fun start(gamePath: String, widthPx: Int, heightPx: Int, cellWidthPx: Double, cellHeightPx: Double) {
        this.gamePath = gamePath
        sizeW = widthPx; sizeH = heightPx; cellW = cellWidthPx; cellH = cellHeightPx
        // Reset state so this is safe to call again for a Restart (fresh terp).
        windows = emptyList(); buffers = emptyMap(); grids = emptyMap()
        pendingInput = null; pendingFilePrompt = null; finished = false
        generation = 0; awaiting = false; outQueue.clear(); splitter = JsonObjectStream()
        val appFd = nativeStart(gamePath)
        if (appFd < 0) { Log.e(TAG, "nativeStart failed"); finished = true; return }
        val descriptor = ParcelFileDescriptor.adoptFd(appFd)
        pfd = descriptor
        output = FileOutputStream(descriptor.fileDescriptor)
        startReader(FileInputStream(descriptor.fileDescriptor))
        enqueue(initEvent())
    }

    /** Stop the terp: closing our socket end gives it EOF -> glk_exit. */
    fun stop() {
        main.removeCallbacks(timerRunnable)
        audio.releaseAll()
        try { pfd?.close() } catch (_: Exception) {}
        pfd = null
        finished = true
    }

    // --- Glk timer ----------------------------------------------------------
    private val timerRunnable = object : Runnable {
        override fun run() {
            if (timerIntervalMs <= 0) return
            // Coalesce: one pending tick at a time, so a slow turn can't make
            // ticks pile up.
            if (outQueue.none { it.optString("type") == "timer" }) {
                enqueue(JSONObject().put("type", "timer").put("gen", generation))
            }
            main.postDelayed(this, timerIntervalMs.toLong())
        }
    }

    private fun rescheduleTimer() {
        main.removeCallbacks(timerRunnable)
        if (timerIntervalMs > 0) main.postDelayed(timerRunnable, timerIntervalMs.toLong())
    }

    /** Restart the current game from scratch (Restart control). The caller first
     *  suppresses the autosave and deletes the slot, so the relaunched terp
     *  finds no autosave and runs the intro fresh. */
    fun restart() {
        if (gamePath.isEmpty()) return
        stop()
        start(gamePath, sizeW, sizeH, cellW, cellH)
    }

    fun submitLine(value: String) {
        val req = pendingInput ?: return
        if (req.type != "line") return
        enqueue(JSONObject().put("type", "line").put("gen", generation)
            .put("window", req.id).put("value", value))
        pendingInput = null
    }

    fun submitChar(value: String) {
        val req = pendingInput ?: return
        if (req.type != "char") return
        enqueue(JSONObject().put("type", "char").put("gen", generation)
            .put("window", req.id).put("value", value))
        pendingInput = null
    }

    /** Answer a pending save/restore file prompt with [name] (a bare filename).
     *  An empty [name] cancels the save/restore. */
    fun submitFileref(name: String) {
        if (pendingFilePrompt == null) return
        enqueue(JSONObject().put("type", "specialresponse").put("gen", generation)
            .put("response", "fileref_prompt").put("value", name))
        pendingFilePrompt = null
    }

    /** Cancel a pending save/restore prompt (sends an empty filename). */
    fun cancelFileref() = submitFileref("")

    fun resize(widthPx: Int, heightPx: Int, cellWidthPx: Double, cellHeightPx: Double) {
        sizeW = widthPx; sizeH = heightPx; cellW = cellWidthPx; cellH = cellHeightPx
        enqueue(JSONObject().put("type", "arrange").put("gen", generation)
            .put("metrics", metrics()))
    }

    /** Blorb image bytes for resource [num], or null. */
    fun image(num: Int): ByteArray? = nativeImage(num)

    // --- Events -------------------------------------------------------------
    private fun metrics(): JSONObject = JSONObject()
        .put("width", sizeW.toDouble())
        .put("height", 100_000.0)              // tall: scroll instead of [MORE]
        .put("charwidth", cellW)
        .put("charheight", cellH)

    private fun initEvent(): JSONObject = JSONObject()
        .put("type", "init").put("gen", 0)
        .put("metrics", metrics())
        .put("support", JSONArray(listOf("timer", "hyperlinks", "graphics")))

    private fun enqueue(event: JSONObject) {
        // Coalesce consecutive arranges (keyboard/rotation bursts).
        val last = outQueue.lastOrNull()
        if (event.optString("type") == "arrange" && last?.optString("type") == "arrange") {
            outQueue[outQueue.size - 1] = event
        } else {
            outQueue.addLast(event)
        }
        pump()
    }

    private fun pump() {
        if (awaiting || outQueue.isEmpty()) return
        awaiting = true
        val event = outQueue.removeFirst()
        event.put("gen", generation)   // restamp at send time
        val bytes = (event.toString()).toByteArray(Charsets.UTF_8)
        val out = output ?: return
        writer.execute {
            try { out.write(bytes); out.flush() } catch (e: Exception) { Log.e(TAG, "write", e) }
        }
    }

    // --- Reader -------------------------------------------------------------
    private fun startReader(input: FileInputStream) {
        thread(name = "jacl.remglk.reader", isDaemon = true) {
            val buf = ByteArray(16 * 1024)
            try {
                while (true) {
                    val n = input.read(buf)
                    if (n <= 0) break
                    for (obj in splitter.append(buf, n)) {
                        val update = try { JSONObject(obj) } catch (e: Exception) { continue }
                        main.post { apply(update) }
                    }
                }
            } catch (_: Exception) { }
            main.post { finished = true }
        }
    }

    private fun apply(update: JSONObject) {
        try {
            if (update.optString("type") == "error") {
                Log.e(TAG, "RemGlk error: ${update.optString("message")}"); return
            }
            if (update.has("gen")) generation = update.getInt("gen")

            update.optJSONArray("windows")?.let { ws ->
                val list = ArrayList<GlkWindow>(ws.length())
                val live = HashSet<Int>()
                for (i in 0 until ws.length()) {
                    val w = ws.getJSONObject(i)
                    list.add(GlkWindow(w.getInt("id"), w.optString("type")))
                    live.add(w.getInt("id"))
                }
                windows = list
                buffers = buffers.filterKeys { it in live }
                grids = grids.filterKeys { it in live }
            }

            update.optJSONArray("content")?.let { content ->
                val newBuffers = buffers.toMutableMap()
                val newGrids = grids.toMutableMap()
                for (i in 0 until content.length()) {
                    val c = content.getJSONObject(i)
                    val id = c.getInt("id")
                    c.optJSONArray("lines")?.let { lines ->        // grid window
                        val rows = ArrayList(newGrids[id] ?: emptyList())
                        for (j in 0 until lines.length()) {
                            val gl = lines.getJSONObject(j)
                            val ln = gl.getInt("line")
                            while (rows.size <= ln) rows.add(emptyList())
                            rows[ln] = renderSpans(gl.optJSONArray("content"))
                        }
                        newGrids[id] = rows
                    }
                    c.optJSONArray("text")?.let { text ->          // buffer window
                        var paras = if (c.optBoolean("clear")) ArrayList()
                                    else ArrayList(newBuffers[id] ?: emptyList())
                        for (j in 0 until text.length()) {
                            val p = text.getJSONObject(j)
                            val spans = renderSpans(p.optJSONArray("content"))
                            if (p.optBoolean("append") && paras.isNotEmpty()) {
                                val last = paras.last()
                                val merged = RenderedParagraph((last.spans + spans).toMutableList())
                                paras[paras.size - 1] = merged
                            } else {
                                paras.add(RenderedParagraph(spans.toMutableList()))
                            }
                        }
                        newBuffers[id] = paras
                    }
                }
                // Pull any <jacl-map> data block out of the transcript (the
                // `map` command emits it) and keep it for the map sheet.
                for ((id, paras) in newBuffers) newBuffers[id] = stripMapBlock(paras)
                buffers = newBuffers
                grids = newGrids
            }

            val inputs = update.optJSONArray("input")
            pendingInput = if (inputs != null && inputs.length() > 0) {
                val inp = inputs.getJSONObject(0)
                GlkInput(inp.getInt("id"), inp.optString("type"))
            } else null

            // A save/restore prompt arrives as a top-level `specialinput`
            // instead of a normal input request; surface it for the UI.
            val special = update.optJSONObject("specialinput")
            if (special != null && special.optString("type") == "fileref_prompt") {
                pendingFilePrompt = GlkSpecialInput(
                    filemode = special.optString("filemode", "read"),
                    filetype = special.optString("filetype", "save"))
            }

            // The "timer" field appears only when the game changes its timer:
            // a number sets/restarts the interval, null cancels it. Absent =
            // no change.
            if (update.has("timer")) {
                timerIntervalMs = if (update.isNull("timer")) 0 else update.getInt("timer")
                rescheduleTimer()
            }

            // Sound-channel ops (play/stop/volume), played from the blorb.
            update.optJSONArray("schannel")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    val chan = o.optInt("chan")
                    when (o.optString("op")) {
                        "play" -> audio.play(chan, o.optInt("snd"),
                            o.optInt("repeats", 1), o.optInt("vol", 65536))
                        "stop" -> audio.stop(chan)
                        "volume" -> audio.setVolume(chan, o.optInt("vol", 65536))
                    }
                }
            }
        } finally {
            awaiting = false
            pump()
        }
    }

    /** Find a <jacl-map>...</jacl-map> run in the paragraphs, parse it into
     *  [gameMap], and return the paragraphs with that run removed (so the raw
     *  data never shows in the transcript). */
    private fun stripMapBlock(paras: List<RenderedParagraph>): List<RenderedParagraph> {
        fun text(p: RenderedParagraph) = p.spans.joinToString("") { it.text }.trim()
        val start = paras.indexOfFirst { text(it) == "<jacl-map>" }
        if (start < 0) return paras
        val end = ((start + 1) until paras.size).firstOrNull { text(paras[it]) == "</jacl-map>" }
            ?: return paras
        parseGameMap((start + 1 until end).map { text(paras[it]) })?.let { gameMap = it }
        return paras.filterIndexed { i, _ -> i < start || i > end }
    }

    private fun renderSpans(content: JSONArray?): List<RenderedSpan> {
        if (content == null) return emptyList()
        val out = ArrayList<RenderedSpan>(content.length())
        for (i in 0 until content.length()) {
            // A grid "content" entry can be a plain run object; a buffer one too.
            val span = content.optJSONObject(i) ?: continue
            val special = span.optString("special", "")
            val image = if (special == "image" && span.has("image")) span.getInt("image") else null
            out.add(RenderedSpan(
                text = span.optString("text", ""),
                style = span.optString("style", "normal"),
                hyperlink = if (span.has("hyperlink")) span.getInt("hyperlink") else null,
                image = image,
            ))
        }
        return out
    }
}

/** Accumulates bytes and yields each complete top-level JSON object as RemGlk
 *  emits them. Port of the Swift JSONObjectStream. */
class JsonObjectStream {
    private val buffer = StringBuilder()

    fun append(data: ByteArray, len: Int): List<String> {
        buffer.append(String(data, 0, len, Charsets.UTF_8))
        val objects = ArrayList<String>()
        var depth = 0; var inString = false; var escape = false
        var start = -1; var consumed = 0
        var i = 0
        while (i < buffer.length) {
            val ch = buffer[i]
            if (inString) {
                when {
                    escape -> escape = false
                    ch == '\\' -> escape = true
                    ch == '"' -> inString = false
                }
            } else when (ch) {
                '"' -> inString = true
                '{' -> { if (depth == 0) start = i; depth++ }
                '}' -> {
                    depth--
                    if (depth == 0 && start >= 0) {
                        objects.add(buffer.substring(start, i + 1))
                        consumed = i + 1
                        start = -1
                    }
                }
            }
            i++
        }
        if (consumed > 0) buffer.delete(0, consumed)
        return objects
    }
}
