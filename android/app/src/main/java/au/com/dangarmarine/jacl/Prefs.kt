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
    const val COLUMNS = 50f
    val COLUMN_RANGE = 30f..70f

    // Cap the reading column's width (dp). The chosen columns fill *this* width,
    // not the whole window, so the font stays a consistent reading size across
    // orientations and the surplus width of a wide landscape tablet becomes
    // centred margins instead of one long, ballooned line. Auto-scales: portrait
    // shows little or no margin, landscape shows generous margins.
    //
    // NB: Android deliberately has NO narrow/normal/wide margin picker (unlike
    // desktop's MARGINS in desktop/main.js and iOS's MarginWidth in
    // ContentView.swift) -- this single cap does the job. If you ever add one,
    // mirror those fractions (0.04 / 0.10 / 0.16) so all three platforms match.
    const val MAX_CONTENT_WIDTH_DP = 800f
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
