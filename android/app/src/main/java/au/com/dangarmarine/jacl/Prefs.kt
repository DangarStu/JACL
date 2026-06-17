package au.com.dangarmarine.jacl

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** In-app appearance choice for the reading screen (the shelf stays dark). */
enum class AppearanceMode(val label: String) {
    SYSTEM("System"), LIGHT("Light"), DARK("Dark");

    /** Resolve to a dark/light boolean; SYSTEM follows the device. */
    @Composable
    fun isDark(): Boolean = when (this) {
        SYSTEM -> isSystemInDarkTheme()
        LIGHT -> false
        DARK -> true
    }
}

object ReadingDefaults {
    // The reading width, in characters. The font size is derived so that this
    // many columns fill the window -- so choosing a column count is really
    // choosing the text size (fewer columns = bigger text), and the text always
    // spans the width and rescales with the window.
    const val COLUMNS = 60f
    val COLUMN_RANGE = 40f..80f
}

/**
 * App preferences (transcript text size + appearance), persisted to
 * SharedPreferences and exposed as Compose state -- the Android counterpart of
 * the iOS @AppStorage settings.
 */
class AppPrefs(private val sp: SharedPreferences) {
    var columns by mutableFloatStateOf(sp.getFloat("readingColumns", ReadingDefaults.COLUMNS))
        private set
    var appearance by mutableStateOf(
        runCatching { AppearanceMode.valueOf(sp.getString("appearanceMode", "SYSTEM")!!) }
            .getOrDefault(AppearanceMode.SYSTEM)
    ); private set
    /** Whether game sound effects/music play. Persists across all games. */
    var soundEnabled by mutableStateOf(sp.getBoolean("soundEnabled", true))
        private set

    fun updateColumns(v: Float) {
        columns = v
        sp.edit().putFloat("readingColumns", v).apply()
    }

    fun updateSoundEnabled(on: Boolean) {
        soundEnabled = on
        sp.edit().putBoolean("soundEnabled", on).apply()
    }

    fun updateAppearance(a: AppearanceMode) {
        appearance = a
        sp.edit().putString("appearanceMode", a.name).apply()
    }

    companion object {
        fun of(ctx: Context) = AppPrefs(ctx.getSharedPreferences("jacl", Context.MODE_PRIVATE))
    }
}
