package au.com.dangarmarine.jacl

import java.io.File

/**
 * The bundled language glossary for a game -- the single <lang>_words.csv that
 * matches the game's own declared language, for long-press "Define". Keys are
 * lowercased words/phrases; values the English gloss. field[0] is the word
 * (never quoted); field[1] the definition, which may be quoted because it can
 * hold commas (e.g. `acara,"event, program"`).
 */
class GameDictionary private constructor(private val entries: Map<String, String>) {

    val isEmpty: Boolean get() = entries.isEmpty()

    /** The gloss for [word] (case-insensitive), or null. */
    fun define(word: String): String? = entries[word.lowercase()]

    companion object {
        /** Map a game's `game_language` (BCP-47, e.g. "id-ID") to the CSV name.
         *  Only languages that ship a dictionary are listed; English and any
         *  other language return null, which disables lookup for that game. */
        private val CSV_FOR_LANG = mapOf(
            "id" to "indonesian_words.csv",
            "fr" to "french_words.csv",
            "de" to "german_words.csv",
            "es" to "spanish_words.csv",
        )

        fun csvName(gameLanguage: String?): String? {
            val sub = gameLanguage?.substringBefore('-')?.lowercase() ?: return null
            return CSV_FOR_LANG[sub]
        }

        /** Load the glossary for [game] from its declared language, or null if
         *  the game's language has no matching CSV in [dataDir]. */
        fun forGame(dataDir: File, gameLanguage: String?): GameDictionary? {
            val name = csvName(gameLanguage) ?: return null
            val csv = File(dataDir, name)
            if (!csv.exists()) return null
            val dict = load(csv)
            return dict.takeUnless { it.isEmpty }
        }

        private fun load(csv: File): GameDictionary {
            val map = HashMap<String, String>()
            val text = runCatching { csv.readText() }.getOrNull() ?: return GameDictionary(map)
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
            return GameDictionary(map)
        }
    }
}
