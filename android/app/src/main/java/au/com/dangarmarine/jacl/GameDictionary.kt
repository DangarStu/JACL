package au.com.dangarmarine.jacl

import java.io.File

/**
 * The bundled language glossary for a game: every data/<lang>_words.csv merged
 * into one lookup, for long-press "Define". Port of the iOS GameDictionary.
 * Keys are lowercased words/phrases; values the English gloss. field[0] is the
 * word (never quoted); field[1] the definition, which may be quoted because it
 * can hold commas (e.g. `acara,"event, program"`).
 */
class GameDictionary private constructor(private val entries: Map<String, String>) {

    val isEmpty: Boolean get() = entries.isEmpty()

    /** The gloss for [word] (case-insensitive), or null. */
    fun define(word: String): String? = entries[word.lowercase()]

    companion object {
        fun load(dataDir: File): GameDictionary {
            val map = HashMap<String, String>()
            val files = dataDir.listFiles { f -> f.name.endsWith("_words.csv") } ?: emptyArray()
            for (f in files) {
                val text = runCatching { f.readText() }.getOrNull() ?: continue
                var header = true
                for (raw in text.lineSequence()) {
                    if (header) { header = false; continue }   // skip header row
                    val comma = raw.indexOf(',')
                    if (comma < 0) continue
                    val key = raw.substring(0, comma).trim().lowercase()
                    if (key.isEmpty()) continue
                    var def = raw.substring(comma + 1).trim()
                    if (def.length >= 2 && def.startsWith("\"") && def.endsWith("\"")) {
                        def = def.substring(1, def.length - 1)   // unquote a comma-bearing gloss
                    }
                    map[key] = def
                }
            }
            return GameDictionary(map)
        }
    }
}
