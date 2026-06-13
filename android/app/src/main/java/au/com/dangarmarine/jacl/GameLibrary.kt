package au.com.dangarmarine.jacl

import android.content.Context
import android.net.Uri
import java.io.File
import java.util.zip.ZipInputStream

/** An imported game: its .j2 file plus the title read from it. */
data class Game(val file: File, val title: String)

/**
 * Sandbox game storage -- the Android counterpart of the iOS GameLibrary.
 * Games live in filesDir; a .jaclgame is unpacked into its .j2 [+ .blorb +
 * dictionary CSVs]. Bundled starters in assets/ are seeded on first launch.
 */
object GameLibrary {

    fun documents(ctx: Context): File = ctx.filesDir

    private fun dataDir(ctx: Context): File = File(ctx.filesDir, "data").apply { mkdirs() }

    /** Imported .j2 games, newest first, each with its resolved title. */
    fun games(ctx: Context): List<Game> =
        (ctx.filesDir.listFiles { f -> f.extension.lowercase() == "j2" } ?: emptyArray())
            .sortedByDescending { it.lastModified() }
            .map { Game(it, title(it) ?: it.nameWithoutExtension) }

    /** Delete a game's .j2 + .blorb. (Shared data/ CSVs are left alone.) */
    fun delete(ctx: Context, game: Game) {
        game.file.delete()
        File(ctx.filesDir, game.file.nameWithoutExtension + ".blorb").delete()
    }

    /** Import a picked/opened file. A .jaclgame/.zip is unpacked; a bare
     *  .j2/.blorb is copied. Returns the playable .j2, if one resulted. */
    fun importGame(ctx: Context, uri: Uri): File? {
        val name = displayName(ctx, uri) ?: "game"
        val ext = name.substringAfterLast('.', "").lowercase()
        ctx.contentResolver.openInputStream(uri).use { input ->
            if (input == null) return null
            return when (ext) {
                "jaclgame", "zip" -> unpack(ctx, input)
                "j2" -> copyTo(File(ctx.filesDir, name), input).let { it }
                "blorb" -> { copyTo(File(ctx.filesDir, name), input); null }
                else -> null
            }
        }
    }

    private fun copyTo(dest: File, input: java.io.InputStream): File {
        dest.outputStream().use { input.copyTo(it) }
        return dest
    }

    /** Unpack a .jaclgame (zip of .j2 [+ .blorb + dictionary CSVs]) into filesDir. */
    private fun unpack(ctx: Context, input: java.io.InputStream): File? {
        var game: File? = null
        ZipInputStream(input).use { zip ->
            var entry = zip.nextEntry
            while (entry != null) {
                val base = File(entry.name).name
                val ext = base.substringAfterLast('.', "").lowercase()
                val dest: File? = when (ext) {
                    "j2", "blorb" -> File(ctx.filesDir, base)
                    "csv" -> File(dataDir(ctx), base)   // interpreter opens data/<lang>_words.csv
                    else -> null
                }
                if (dest != null) {
                    dest.outputStream().use { zip.copyTo(it) }
                    if (ext == "j2") game = dest
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return game
    }

    /** Seed games bundled in assets/ on first launch (tracked by name). */
    fun installBundledStarters(ctx: Context) {
        val prefs = ctx.getSharedPreferences("jacl", Context.MODE_PRIVATE)
        val seeded = prefs.getStringSet("seededStarters", emptySet())!!.toMutableSet()
        val bundled = ctx.assets.list("")?.filter { it.endsWith(".jaclgame") } ?: emptyList()
        for (asset in bundled) {
            if (asset in seeded) continue
            ctx.assets.open(asset).use { unpack(ctx, it) }
            seeded.add(asset)
        }
        prefs.edit().putStringSet("seededStarters", seeded).apply()
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
        if (parts.size < 2 || parts[0] != "constant" || parts[1] != name) return null
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
