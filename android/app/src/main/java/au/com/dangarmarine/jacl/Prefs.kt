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
    const val FONT_SIZE = 21f
    val FONT_RANGE = 12f..28f
}

/**
 * App preferences (transcript text size + appearance), persisted to
 * SharedPreferences and exposed as Compose state -- the Android counterpart of
 * the iOS @AppStorage settings.
 */
class AppPrefs(private val sp: SharedPreferences) {
    var fontSize by mutableFloatStateOf(sp.getFloat("transcriptFontSize", ReadingDefaults.FONT_SIZE))
        private set
    var appearance by mutableStateOf(
        runCatching { AppearanceMode.valueOf(sp.getString("appearanceMode", "SYSTEM")!!) }
            .getOrDefault(AppearanceMode.SYSTEM)
    ); private set
    /** Whether game sound effects/music play. Persists across all games. */
    var soundEnabled by mutableStateOf(sp.getBoolean("soundEnabled", true))
        private set

    fun updateFontSize(v: Float) {
        fontSize = v
        sp.edit().putFloat("transcriptFontSize", v).apply()
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
