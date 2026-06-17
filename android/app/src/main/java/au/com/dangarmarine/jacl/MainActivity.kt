package au.com.dangarmarine.jacl

import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Place
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.rememberTextMeasurer
import kotlinx.coroutines.delay
import java.io.File

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // "Open in JACL" VIEW intent (a .jaclgame / .j2 tapped elsewhere).
        intent?.data?.let { uri -> runCatching { GameLibrary.importGame(this, uri) } }
        setContent { JaclRoot() }
    }
}

/** Material theme wrapper: dark or light, with a themed surface background. */
@Composable
fun AppTheme(dark: Boolean, content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (dark) darkColorScheme() else lightColorScheme()) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) { content() }
    }
}

@Composable
fun JaclRoot() {
    val ctx = LocalContext.current
    val prefs = remember { AppPrefs.of(ctx) }
    var games by remember { mutableStateOf<List<Game>>(emptyList()) }
    var current by remember { mutableStateOf<Game?>(null) }
    var showSettings by remember { mutableStateOf(false) }

    fun refresh() { games = GameLibrary.games(ctx) }

    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            val game = runCatching { GameLibrary.importGame(ctx, uri) }.getOrNull()
            refresh()
            android.widget.Toast.makeText(
                ctx,
                if (game != null) "Game imported" else "That file isn’t a JACL game",
                android.widget.Toast.LENGTH_SHORT,
            ).show()
        }
    }

    LaunchedEffect(Unit) {
        GameLibrary.installBundledStarters(ctx)
        refresh()
    }

    when {
        showSettings -> AppTheme(prefs.appearance.isDark()) {
            SettingsScreen(prefs) { showSettings = false }
        }
        current != null -> AppTheme(prefs.appearance.isDark()) {
            GameScreen(current!!, prefs) { current = null }
        }
        else -> AppTheme(dark = true) {   // the shelf stays dark for its watermark
            ShelfScreen(
                games = games,
                onOpen = { current = it },
                onImport = { importer.launch(arrayOf("*/*")) },
                onSettings = { showSettings = true },
                onDelete = { GameLibrary.delete(ctx, it); refresh() },
            )
        }
    }
}

// MARK: - Shelf

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ShelfScreen(
    games: List<Game>,
    onOpen: (Game) -> Unit,
    onImport: () -> Unit,
    onSettings: () -> Unit,
    onDelete: (Game) -> Unit,
) {
    var pendingDelete by remember { mutableStateOf<Game?>(null) }
    Box(Modifier.fillMaxSize().background(Color.Black)) {
        // Cover-art watermark: the WHOLE image fitted within the screen (never
        // cropped) and dimmed under a black scrim -- matching the iPad's
        // scaledToFit. Fit centres it, leaving black gaps top/bottom in portrait
        // or left/right in landscape rather than overflowing the square art.
        Image(
            painter = painterResource(R.drawable.shelf_artwork),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Fit,
            alignment = Alignment.Center,
            alpha = 0.55f,
        )
        Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.4f)))

        Scaffold(
            containerColor = Color.Transparent,
            topBar = {
                TopAppBar(
                    title = { Text("JACL v${GlkBridge.version}") },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = Color.Transparent,
                        titleContentColor = Color.White,
                        actionIconContentColor = Color.White,
                    ),
                    actions = {
                        IconButton(onClick = onSettings) { Icon(Icons.Filled.Settings, "Settings") }
                        IconButton(onClick = onImport) { Icon(Icons.Filled.Add, "Import game") }
                    },
                )
            },
        ) { pad ->
            if (games.isEmpty()) {
                Box(Modifier.fillMaxSize().padding(pad), contentAlignment = Alignment.Center) {
                    Text("Tap + to import a JACL game.", color = Color.White)
                }
            } else {
                LazyColumn(Modifier.fillMaxSize().padding(pad)) {
                    items(games, key = { it.file.path }) { g ->
                        Row(
                            Modifier.fillMaxWidth()
                                .combinedClickable(
                                    onClick = { onOpen(g) },
                                    onLongClick = { pendingDelete = g },
                                )
                                .padding(horizontal = 16.dp, vertical = 16.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(g.title, color = Color.White,
                                style = MaterialTheme.typography.titleMedium)
                            Spacer(Modifier.width(8.dp))
                            // The language, like the online game list.
                            Text("(${g.language})", color = Color.White.copy(alpha = 0.6f),
                                style = MaterialTheme.typography.bodyMedium)
                        }
                        HorizontalDivider(color = Color.White.copy(alpha = 0.15f))
                    }
                }
            }
        }
    }

    pendingDelete?.let { g ->
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text("Delete game?") },
            text = { Text("Remove “${g.title}” from this device?") },
            confirmButton = { TextButton(onClick = { onDelete(g); pendingDelete = null }) { Text("Delete") } },
            dismissButton = { TextButton(onClick = { pendingDelete = null }) { Text("Cancel") } },
        )
    }
}

// MARK: - Settings

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(prefs: AppPrefs, onClose: () -> Unit) {
    BackHandler(onBack = onClose)
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onClose) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
                },
            )
        },
        containerColor = MaterialTheme.colorScheme.surfaceContainerLowest,
    ) { pad ->
        val uri = LocalUriHandler.current
        Column(
            Modifier.fillMaxSize().padding(pad)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            SettingsSection("Reading") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Text Size", Modifier.weight(1f))
                    Text("${prefs.fontSize.toInt()} pt", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Slider(
                    value = prefs.fontSize,
                    onValueChange = { prefs.updateFontSize(it) },
                    valueRange = ReadingDefaults.FONT_RANGE,
                    steps = (ReadingDefaults.FONT_RANGE.endInclusive - ReadingDefaults.FONT_RANGE.start).toInt() - 1,
                )
                Text("The quick brown fox jumps over the lazy dog.",
                    fontFamily = FontFamily.Serif, fontSize = prefs.fontSize.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }

            SettingsSection("Appearance",
                footer = "Appearance applies to the reading screen; the shelf stays dark.") {
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                    AppearanceMode.entries.forEachIndexed { i, mode ->
                        SegmentedButton(
                            selected = prefs.appearance == mode,
                            onClick = { prefs.updateAppearance(mode) },
                            shape = SegmentedButtonDefaults.itemShape(i, AppearanceMode.entries.size),
                        ) { Text(mode.label) }
                    }
                }
            }

            SettingsSection("Sound",
                footer = "Play game sound effects and music. Applies to every game.") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Sound", Modifier.weight(1f))
                    Switch(
                        checked = prefs.soundEnabled,
                        onCheckedChange = { prefs.updateSoundEnabled(it) },
                    )
                }
            }

            SettingsSection("Games",
                footer = "Browse the games at jacl.dangarmarine.com.au, then choose “Open in JACL” to install one.") {
                SettingsLinkRow("Get more games") { uri.openUri("https://jacl.dangarmarine.com.au/#get") }
            }

            SettingsSection("Privacy",
                footer = "JACL collects no personal data. Games and saved games stay on your device; "
                       + "the app makes no network connections and has no accounts or sign-in.") {
                SettingsLinkRow("Privacy Policy") { uri.openUri("https://jacl.dangarmarine.com.au/privacy.html") }
            }

            SettingsSection("About") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Version", Modifier.weight(1f))
                    Text(GlkBridge.version, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Text("JACL by Stuart Allen", color = MaterialTheme.colorScheme.onSurfaceVariant)
                SettingsLinkRow("jacl.dangarmarine.com.au") { uri.openUri("https://jacl.dangarmarine.com.au/") }
            }
        }
    }
}

/** A grouped settings card with an uppercase header and an optional footer
 *  note -- mirrors the inset, grouped look of the iPad's SwiftUI Form. */
@Composable
private fun SettingsSection(
    title: String,
    footer: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Text(
        title.uppercase(),
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(start = 8.dp, top = 12.dp, bottom = 6.dp),
    )
    Card(
        Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp), content = content)
    }
    if (footer != null) {
        Text(footer, style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(start = 8.dp, top = 6.dp))
    }
}

/** A tappable link row inside a settings card (opens a URL). */
@Composable
private fun SettingsLinkRow(label: String, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, Modifier.weight(1f), color = MaterialTheme.colorScheme.primary)
        Icon(Icons.AutoMirrored.Filled.OpenInNew, contentDescription = null,
            tint = MaterialTheme.colorScheme.primary)
    }
}

// MARK: - Game

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GameScreen(game: Game, prefs: AppPrefs, onBack: () -> Unit) {
    val ctx = LocalContext.current
    val bridge = remember(game.file.path) { GlkBridge() }
    val density = LocalDensity.current
    val measurer = rememberTextMeasurer()
    var started by remember { mutableStateOf(false) }
    var containerW by remember { mutableStateOf(0) }
    val fontSize = prefs.fontSize

    val headerColor = remember(game.file.path) {
        GameLibrary.stringConstant(game.file, "header_colour")?.let { parseHexColor(it) }
    }
    // Glossary for long-press Define -- ONLY the CSV matching the game's own
    // declared language (game_language); null (lookup disabled) for English or
    // any language without a bundled dictionary.
    val dictionary = remember(game.file.path) {
        val lang = GameLibrary.stringConstant(game.file, "game_language")
        GameDictionary.forGame(File(ctx.filesDir, "data"), lang)
    }
    var definition by remember { mutableStateOf<Pair<String, String>?>(null) }
    val onDefine: ((String) -> Unit)? = dictionary?.let { dict ->
        { word -> definition = word to (dict.define(word) ?: "isn’t in the dictionary.") }
    }

    var showTextSize by remember { mutableStateOf(false) }
    var saveName by remember { mutableStateOf("") }
    var showRestartConfirm by remember { mutableStateOf(false) }
    var showMap by remember { mutableStateOf(false) }

    BackHandler(onBack = onBack)
    DisposableEffect(game.file.path) { onDispose { bridge.stop() } }
    // Apply the persistent Sound setting (and react to it changing live).
    LaunchedEffect(prefs.soundEnabled) { bridge.setSoundEnabled(prefs.soundEnabled) }
    // A fresh map arrived (typed `map` or the Map button) -- open the sheet.
    LaunchedEffect(bridge.mapVersion) { if (bridge.mapVersion > 0) showMap = true }

    fun cell() = measurer.measure(
        AnnotatedString("0"),
        style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = fontSize.sp)
    ).size

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(game.title, maxLines = 1) },
                navigationIcon = {
                    IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
                },
                actions = {
                    // Map: ask the game to emit its map, then open the map sheet.
                    IconButton(onClick = { bridge.submitLine("map"); showMap = true }) {
                        Icon(Icons.Filled.Place, "Map")
                    }
                    // Restart: wipe the autosave and begin again from the intro.
                    IconButton(onClick = { showRestartConfirm = true }) {
                        Icon(Icons.Filled.Refresh, "Restart game")
                    }
                    // An "Aa" button that drops the text-size slider, so the
                    // reading size can be changed without leaving the game.
                    Box {
                        IconButton(onClick = { showTextSize = true }) {
                            Text("Aa", fontWeight = FontWeight.SemiBold)
                        }
                        DropdownMenu(
                            expanded = showTextSize,
                            onDismissRequest = { showTextSize = false },
                        ) {
                            Column(Modifier.width(260.dp).padding(horizontal = 16.dp, vertical = 4.dp)) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Text("Text Size", Modifier.weight(1f))
                                    Text("${fontSize.toInt()} pt",
                                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                                Slider(
                                    value = fontSize,
                                    onValueChange = { prefs.updateFontSize(it) },
                                    valueRange = ReadingDefaults.FONT_RANGE,
                                    steps = (ReadingDefaults.FONT_RANGE.endInclusive
                                        - ReadingDefaults.FONT_RANGE.start).toInt() - 1,
                                )
                            }
                        }
                    }
                },
            )
        },
        // Pad content for the system bars AND the keyboard, so the input line
        // rises above the IME instead of hiding under it (the manifest's
        // adjustResize is ignored once the app draws edge-to-edge on API 35+).
        // union() takes the larger inset per edge -- no double-padding, and it's
        // zero when a hardware keyboard means no on-screen keyboard shows.
        contentWindowInsets = WindowInsets.systemBars.union(WindowInsets.ime),
    ) { pad ->
        Column(
            Modifier.fillMaxSize().padding(pad).onSizeChanged { sz ->
                if (sz.width <= 0) return@onSizeChanged
                val w = sz.width - with(density) { 16.dp.toPx() }.toInt()
                val c = cell()
                // Cell WIDTH is the monospace pitch, measured from a run / count
                // (a single glyph's box is narrower than its advance) plus a
                // small safety margin. The measured pitch runs a touch under what
                // is actually drawn (each glyph snaps to a whole pixel), so
                // without the margin RemGlk -- which sets the grid columns to
                // floor(width / charwidth) -- fits a couple too many columns and
                // the right-aligned status text (Moves) clips off the edge.
                // Erring ~2px wide costs at most one column of right padding.
                val advance = measurer.measure(
                    AnnotatedString("0".repeat(40)),
                    style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = fontSize.sp)
                ).size.width.toDouble() / 40 + 2.0
                if (!started) {
                    bridge.start(game.file.path, w, sz.height, advance, c.height.toDouble())
                    started = true; containerW = sz.width
                } else if (sz.width != containerW) {
                    bridge.resize(w, sz.height, advance, c.height.toDouble())
                    containerW = sz.width
                }
            }
        ) {
            for (w in bridge.windows.filter { it.type == "grid" }) {
                GridView(bridge.grids[w.id] ?: emptyList(), fontSize)
            }
            for (w in bridge.windows.filter { it.type == "buffer" }) {
                Box(Modifier.weight(1f).fillMaxWidth()) {
                    TranscriptView(bridge, bridge.buffers[w.id] ?: emptyList(), headerColor, fontSize, onDefine)
                }
            }
            InputBar(bridge, fontSize)
        }
    }

    definition?.let { (word, gloss) ->
        AlertDialog(
            onDismissRequest = { definition = null },
            confirmButton = { TextButton(onClick = { definition = null }) { Text("Done") } },
            title = { Text(word) },
            text = { Text(gloss) },
        )
    }

    // Save/restore file prompts. The game's save verb asks for a name (write);
    // the restore verb shows the existing saves to pick from (read). Both
    // answer the same RemGlk file prompt.
    val prompt = bridge.pendingFilePrompt
    if (prompt?.filemode == "write") {
        AlertDialog(
            onDismissRequest = { bridge.cancelFileref(); saveName = "" },
            title = { Text("Save Game") },
            text = {
                OutlinedTextField(
                    value = saveName,
                    onValueChange = { saveName = it },
                    singleLine = true,
                    label = { Text("Save name") },
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    // Prefix with the game so the same name works across games.
                    val n = saveName.trim()
                    bridge.submitFileref(if (n.isEmpty()) "" else GlkBridge.saveValue(game.file, n))
                    saveName = ""
                }) { Text("Save") }
            },
            dismissButton = {
                TextButton(onClick = { bridge.cancelFileref(); saveName = "" }) { Text("Cancel") }
            },
        )
    } else if (prompt?.filemode == "read") {
        val saves = remember(prompt) { GlkBridge.savedGames(game.file) }
        AlertDialog(
            onDismissRequest = { bridge.cancelFileref() },
            title = { Text("Restore Game") },
            text = {
                if (saves.isEmpty()) {
                    Text("No saved games. Type “save” during play to create one.")
                } else {
                    Column {
                        for (name in saves) {
                            TextButton(
                                onClick = { bridge.submitFileref(GlkBridge.saveValue(game.file, name)) },
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Text(name, Modifier.fillMaxWidth())
                            }
                        }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { bridge.cancelFileref() }) { Text("Cancel") }
            },
        )
    }

    if (showMap) {
        MapSheet(map = bridge.gameMap, onDismiss = { showMap = false })
    }

    if (showRestartConfirm) {
        AlertDialog(
            onDismissRequest = { showRestartConfirm = false },
            title = { Text("Restart Game?") },
            text = {
                Text("Start over from the beginning. Your current progress (the " +
                     "autosave) is lost; named saves are kept.")
            },
            confirmButton = {
                TextButton(onClick = {
                    showRestartConfirm = false
                    // Don't autosave the discarded game, drop its autosave slot,
                    // then relaunch the terp so it runs the intro fresh.
                    bridge.setAutosaveSuppressed(true)
                    GlkBridge.autosaveFile(game.file).delete()
                    bridge.restart()
                }) { Text("Restart") }
            },
            dismissButton = {
                TextButton(onClick = { showRestartConfirm = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
fun GridView(rows: List<List<RenderedSpan>>, fontSize: Float) {
    Surface(color = MaterialTheme.colorScheme.surfaceVariant) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp)) {
            for (row in rows) {
                Text(
                    buildSpans(row, MaterialTheme.colorScheme.primary),
                    fontFamily = FontFamily.Monospace,
                    fontSize = fontSize.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
fun TranscriptView(
    bridge: GlkBridge,
    paras: List<RenderedParagraph>,
    headerColor: Color?,
    fontSize: Float,
    onDefine: ((String) -> Unit)?,
) {
    val scroll = rememberScrollState()
    val imeBottom = WindowInsets.ime.getBottom(LocalDensity.current)

    // While waiting for a command, JACL has already written its bare "> " prompt
    // as the trailing paragraph. Hide it -- the input bar below is the live
    // prompt, and the command is echoed ("> look") on submit. Only drop a
    // genuinely short non-image trailing line, so real output is never removed.
    val visible = if (bridge.pendingInput?.type == "line" && paras.isNotEmpty() &&
        paras.last().spans.all { it.image == null } &&
        paras.last().spans.joinToString("") { it.text }.trim().length <= 2) {
        paras.dropLast(1)
    } else paras

    // Pin the just-typed command ("> look", echoed by JACL) to the top of the
    // transcript after each turn, so a long response is read from its start and
    // scrolled down for the rest. scrollTo() clamps to the bottom, so a short
    // response still sits snug above the keyboard with nothing cut off. Before
    // the first command (the intro) and for any non-command output, keep the
    // latest line in view instead.
    val cmdEcho = visible.indexOfLast { p ->
        p.spans.all { it.image == null } &&
            p.spans.joinToString("") { it.text }
                .let { it.trimStart().startsWith(">") && it.trim().length > 2 }
    }
    var viewportTop by remember { mutableStateOf(0f) }
    var cmdOffset by remember { mutableStateOf<Int?>(null) }
    // Re-pin on a new turn (cmdOffset/visible change) and when the keyboard
    // opens or closes (the IME inset resizes the viewport).
    LaunchedEffect(cmdEcho, visible.size, imeBottom, cmdOffset) {
        if (cmdEcho >= 0 && cmdOffset != null) scroll.scrollTo(cmdOffset!!)
        else scroll.scrollTo(scroll.maxValue)
    }

    var bannerShown = false
    val accent = MaterialTheme.colorScheme.primary
    Column(
        Modifier.fillMaxSize()
            .onGloballyPositioned { viewportTop = it.positionInWindow().y }
            .verticalScroll(scroll)
            .padding(vertical = 12.dp)
    ) {
        visible.forEachIndexed { i, p ->
            // Measure the command echo's offset within the scrolling content:
            // its window position minus the viewport top, plus the current
            // scroll. This is invariant under scrolling, so it doesn't loop.
            val posMod = if (i == cmdEcho) {
                Modifier.onGloballyPositioned { c ->
                    cmdOffset = (c.positionInWindow().y - viewportTop + scroll.value).toInt()
                }
            } else Modifier
            Column(posMod.fillMaxWidth()) {
                val img = p.spans.firstOrNull { it.image != null }?.image
                if (img != null) {
                    if (!bannerShown) { BannerImage(bridge, img, headerColor); bannerShown = true }
                    else BlorbImage(bridge, img)
                }
                val text = buildSpans(p.spans, accent)
                if (text.isNotEmpty()) {
                    DefinableText(text, fontSize, onDefine)
                }
                Spacer(Modifier.height(8.dp))
            }
        }
    }
}

/** A transcript paragraph. Long-press a word for its definition (dictionary
 *  games); otherwise just selectable text for copy. */
@Composable
fun DefinableText(text: AnnotatedString, fontSize: Float, onDefine: ((String) -> Unit)?) {
    if (onDefine == null) {
        SelectionContainer {
            Text(text, fontFamily = FontFamily.Serif, fontSize = fontSize.sp,
                modifier = Modifier.padding(horizontal = 12.dp))
        }
        return
    }
    var layout by remember { mutableStateOf<TextLayoutResult?>(null) }
    Text(
        text, fontFamily = FontFamily.Serif, fontSize = fontSize.sp,
        onTextLayout = { layout = it },
        modifier = Modifier.padding(horizontal = 12.dp).pointerInput(onDefine) {
            detectTapGestures(onLongPress = { pos ->
                val l = layout ?: return@detectTapGestures
                val off = l.getOffsetForPosition(pos)
                wordAt(text.text, off)?.let(onDefine)
            })
        },
    )
}

/** The word (letters/digits) around [offset] in [text], or null. */
fun wordAt(text: String, offset: Int): String? {
    if (text.isEmpty()) return null
    val o = offset.coerceIn(0, text.length - 1)
    var start = o
    var end = o
    while (start > 0 && text[start - 1].isLetterOrDigit()) start--
    while (end < text.length && text[end].isLetterOrDigit()) end++
    if (start >= end) return null
    return text.substring(start, end)
}

@Composable
fun BannerImage(bridge: GlkBridge, num: Int, headerColor: Color?) {
    val bmp: ImageBitmap = remember(num) {
        bridge.image(num)?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
    } ?: return
    Box(
        Modifier.fillMaxWidth().then(if (headerColor != null) Modifier.background(headerColor) else Modifier),
        contentAlignment = Alignment.CenterStart
    ) {
        Image(bmp, contentDescription = null)
    }
    Spacer(Modifier.height(8.dp))
}

@Composable
fun BlorbImage(bridge: GlkBridge, num: Int) {
    val bmp: ImageBitmap? = remember(num) {
        bridge.image(num)?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
    }
    if (bmp != null) {
        Image(bmp, contentDescription = null, modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp))
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
fun InputBar(bridge: GlkBridge, fontSize: Float) {
    val input = bridge.pendingInput
    when (input?.type) {
        "line" -> {
            var text by remember(bridge.buffers) { mutableStateOf("") }
            val focus = remember { FocusRequester() }
            LaunchedEffect(Unit) { delay(120); runCatching { focus.requestFocus() } }
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("> ", color = MaterialTheme.colorScheme.onSurfaceVariant)
                val submit = { bridge.submitLine(text); text = "" }
                BasicTextField(
                    value = text,
                    onValueChange = { text = it },
                    textStyle = TextStyle(fontSize = fontSize.sp, color = MaterialTheme.colorScheme.onSurface),
                    cursorBrush = SolidColor(MaterialTheme.colorScheme.onSurface),
                    singleLine = true,
                    // A password-type IME reliably disables autocorrect and
                    // suggestions on every keyboard (autoCorrect=false alone is
                    // ignored by Gboard, which otherwise "corrects" non-English
                    // game words like Indonesian "payung" to English). The text
                    // stays visible -- Compose only masks via visualTransformation,
                    // which we leave at its default (None).
                    keyboardOptions = KeyboardOptions(
                        autoCorrect = false,
                        capitalization = KeyboardCapitalization.None,
                        keyboardType = KeyboardType.Password,
                        imeAction = ImeAction.Send,
                    ),
                    keyboardActions = KeyboardActions(onSend = { submit() }),
                    modifier = Modifier.weight(1f).focusRequester(focus),
                )
                TextButton(onClick = { submit() }) { Text("Enter") }
            }
        }
        "char" -> {
            Button(onClick = { bridge.submitChar(" ") }, modifier = Modifier.fillMaxWidth().padding(8.dp)) {
                Text("Tap to continue")
            }
        }
        else -> if (bridge.finished) {
            Text("The game has ended.", Modifier.padding(8.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** Parse a CSS-style hex colour from a header_colour constant: #RRGGBB or #RGB. */
fun parseHexColor(hex: String): Color? {
    var s = hex.trim()
    if (s.startsWith("#")) s = s.substring(1)
    if (s.length == 3) s = s.map { "$it$it" }.joinToString("")
    if (s.length != 6) return null
    return try { Color(("ff$s").toLong(16)) } catch (e: Exception) { null }
}

/** Build an AnnotatedString from spans; `inputColor` tints the echoed command. */
fun buildSpans(spans: List<RenderedSpan>, inputColor: Color): AnnotatedString = buildAnnotatedString {
    for (s in spans) {
        if (s.text.isEmpty()) continue
        val style = when (s.style) {
            "header", "subheader" -> SpanStyle(fontWeight = FontWeight.Bold)
            "emphasized", "note", "blockquote" -> SpanStyle(fontStyle = FontStyle.Italic)
            "alert" -> SpanStyle(color = Color(0xFFD32F2F))
            "input" -> SpanStyle(color = inputColor)
            "preformatted", "user1", "user2" -> SpanStyle(fontFamily = FontFamily.Monospace)
            else -> SpanStyle()
        }
        val withLink = if (s.hyperlink != null)
            style.copy(color = Color(0xFF1565C0), textDecoration = TextDecoration.Underline) else style
        withStyle(withLink) { append(s.text) }
    }
}
