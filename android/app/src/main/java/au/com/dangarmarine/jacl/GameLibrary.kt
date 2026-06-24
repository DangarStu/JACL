package au.com.dangarmarine.jacl

import android.content.Context
import android.net.Uri
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipInputStream

/** An imported game: its .j2, the title, and the language it's written in. */
data class Game(val file: File, val title: String, val language: String)

/**
 * Sandbox game storage -- the Android counterpart of the iOS GameLibrary.
 * Games live in filesDir; a .jaclgame is unpacked into its .j2 [+ .blorb +
 * dictionary CSVs]. Bundled starters in assets/ are seeded on first launch.
 */
object GameLibrary {

    fun documents(ctx: Context): File = ctx.filesDir

    private fun dataDir(ctx: Context): File = File(ctx.filesDir, "data").apply { mkdirs() }

    /** Imported .j2 games, newest first, each with its title and language. */
    fun games(ctx: Context): List<Game> =
        (ctx.filesDir.listFiles { f -> f.extension.lowercase() == "j2" } ?: emptyArray())
            .sortedByDescending { it.lastModified() }
            .map { f ->
                val meta = metadata(f)
                Game(f,
                     meta?.optString("title")?.ifEmpty { null } ?: title(f) ?: f.nameWithoutExtension,
                     meta?.optString("language")?.ifEmpty { null } ?: languageOf(f))
            }

    // --- Package metadata (game.json) ---------------------------------------
    // The release .j2 XOR-obfuscates its `constant game_title`, so it can't be
    // grepped. The .jaclgame instead ships an un-obfuscated game.json (written
    // from the .jacl source by mkjaclgame.sh); on import we persist it as a
    // sidecar beside the .j2 so the shelf shows the real title.

    /** Sidecar path for a stored game's metadata: grail.j2 -> grail.meta.json. */
    private fun metaFile(j2: File): File = File(j2.parentFile, j2.nameWithoutExtension + ".meta.json")

    private fun metadata(j2: File): JSONObject? = try {
        metaFile(j2).takeIf { it.isFile }?.let { JSONObject(it.readText()) }
    } catch (e: Exception) { null }

    /** The game's language name (English / Indonesian / French / German /
     *  Spanish), from its game_language constant -- the same labels the online
     *  list shows. Anything unmapped or absent reads as English, matching the
     *  make-apache landing-page script's default. */
    fun languageOf(file: File): String = when (stringConstant(file, "game_language")
        ?.substringBefore('-')?.lowercase()) {
        "id" -> "Indonesian"
        "fr" -> "French"
        "de" -> "German"
        "es" -> "Spanish"
        else -> "English"
    }

    /** Delete a game's .j2 + .blorb. (Shared data/ CSVs are left alone.) */
    fun delete(ctx: Context, game: Game) {
        game.file.delete()
        File(ctx.filesDir, game.file.nameWithoutExtension + ".blorb").delete()
        metaFile(game.file).delete()
    }

    /** Import a picked/opened file. A .jaclgame/.zip is unpacked; a bare
     *  .j2/.blorb is copied. Returns the playable .j2, if one resulted.
     *
     *  Recognises the type by extension AND by content: a browser download may
     *  arrive with the wrong/no extension (e.g. "game" or "game.zip") or as an
     *  opaque content:// uri, so we also sniff the bytes -- ZIP magic ("PK") for
     *  a .jaclgame, the "#!" / "#processed" / "#encrypted" header for a .j2. */
    fun importGame(ctx: Context, uri: Uri): File? {
        val name = displayName(ctx, uri) ?: "game"
        val ext = name.substringAfterLast('.', "").lowercase()
        val base = name.substringBeforeLast('.', name).ifEmpty { "game" }
        val bytes = ctx.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return null

        val isZip = bytes.size >= 2 && bytes[0] == 'P'.code.toByte() && bytes[1] == 'K'.code.toByte()
        return when {
            ext == "blorb" -> { File(ctx.filesDir, "$base.blorb").writeBytes(bytes); null }
            ext == "jaclgame" || ext == "zip" || isZip -> unpack(ctx, bytes.inputStream())
            ext == "j2" || looksLikeJ2(bytes) ->
                File(ctx.filesDir, "$base.j2").also { it.writeBytes(bytes) }
            else -> null
        }
    }

    private fun looksLikeJ2(bytes: ByteArray): Boolean {
        val head = String(bytes.copyOf(minOf(64, bytes.size)), Charsets.US_ASCII)
        return head.startsWith("#!") || head.contains("#processed") || head.contains("#encrypted")
    }

    /** Unpack a .jaclgame (zip of .j2 [+ .blorb + dictionary CSVs]) into filesDir. */
    private fun unpack(ctx: Context, input: java.io.InputStream): File? {
        var game: File? = null
        var metaJson: ByteArray? = null   // game.json, written as a sidecar below
        ZipInputStream(input).use { zip ->
            var entry = zip.nextEntry
            while (entry != null) {
                val base = File(entry.name).name
                val ext = base.substringAfterLast('.', "").lowercase()
                if (base == "game.json") {
                    metaJson = zip.readBytes()
                } else {
                    val dest: File? = when (ext) {
                        "j2", "blorb" -> File(ctx.filesDir, base)
                        "csv" -> File(dataDir(ctx), base)   // interpreter opens data/<lang>_words.csv
                        else -> null
                    }
                    if (dest != null) {
                        dest.outputStream().use { zip.copyTo(it) }
                        if (ext == "j2") game = dest
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        // Persist the plain-text title/language beside the .j2 (or clear a stale
        // sidecar from an earlier import that lacked one).
        game?.let { g ->
            val sidecar = metaFile(g)
            metaJson?.let { sidecar.writeBytes(it) } ?: sidecar.delete()
        }
        return game
    }

    /** Seed games bundled in assets/ on first launch (tracked by name). */
    fun installBundledStarters(ctx: Context) {
        val prefs = ctx.getSharedPreferences("jacl", Context.MODE_PRIVATE)
        val bundled = ctx.assets.list("")?.filter { it.endsWith(".jaclgame") } ?: emptyList()
        val edit = prefs.edit()
        for (asset in bundled) {
            // Re-unpack when first seen OR when the bundled game changed (its
            // size differs), so an app update pushes updated games to existing
            // installs. Re-unpacking overwrites the .j2/.blorb but leaves the
            // player's .glksave saves alone. (available() is the uncompressed
            // asset size -- a cheap fingerprint.)
            val size = try { ctx.assets.open(asset).use { it.available() } } catch (e: Exception) { -1 }
            if (size >= 0 && prefs.getInt("seedSize_$asset", -1) == size) continue
            ctx.assets.open(asset).use { unpack(ctx, it) }
            edit.putInt("seedSize_$asset", size)
        }
        edit.apply()
    }

    // --- Reading constants from a .j2 ---------------------------------------
    // Release .j2 XOR-obfuscate every line after #encrypted (byte-wise ^0xFF).
    fun stringConstant(file: File, name: String): String? {
        val data = try { file.readBytes() } catch (e: Exception) { return null }
        val marker = "#encrypted".toByteArray(Charsets.US_ASCII)
        var encrypted = false
        var lineStart = 0
        var i = 0
        while (i < data.size) {
            if (data[i] != '\n'.code.toByte()) { i++; continue }
            val line = data.copyOfRange(lineStart, i)
            lineStart = i + 1; i++
            if (encrypted) {
                for (j in line.indices) line[j] = (line[j].toInt() xor 0xFF).toByte()
            } else if (startsWith(line, marker)) {
                encrypted = true; continue
            }
            quotedConstant(line, name)?.let { return it }
        }
        return null
    }

    /** The game's display title (constant game_title "..."). */
    fun title(file: File): String? = stringConstant(file, "game_title")

    private fun startsWith(a: ByteArray, prefix: ByteArray): Boolean {
        if (a.size < prefix.size) return false
        for (i in prefix.indices) if (a[i] != prefix[i]) return false
        return true
    }

    private fun quotedConstant(bytes: ByteArray, name: String): String? {
        val line = String(bytes, Charsets.UTF_8)
        val parts = line.split(' ', '\t').filter { it.isNotEmpty() }
        // game_title / header_colour are `constant`; game_language is a `string`.
        if (parts.size < 2 || (parts[0] != "constant" && parts[0] != "string") || parts[1] != name) return null
        val a = line.indexOf('"'); if (a < 0) return null
        val b = line.indexOf('"', a + 1); if (b < 0) return null
        return line.substring(a + 1, b)
    }

    private fun displayName(ctx: Context, uri: Uri): String? {
        if (uri.scheme == "file") return uri.lastPathSegment
        ctx.contentResolver.query(uri, null, null, null, null)?.use { c ->
            val idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && c.moveToFirst()) return c.getString(idx)
        }
        return uri.lastPathSegment
    }
}
