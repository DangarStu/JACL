package au.com.dangarmarine.jacl

import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import kotlinx.coroutines.delay
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.rememberTextMeasurer

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Handle an "Open in JACL" VIEW intent (a .jaclgame / .j2 tapped
        // elsewhere) before listing the shelf.
        intent?.data?.let { uri -> runCatching { GameLibrary.importGame(this, uri) } }

        setContent {
            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) { JaclApp() }
            }
        }
    }
}

@Composable
fun JaclApp() {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    var games by remember { mutableStateOf<List<Game>>(emptyList()) }
    var current by remember { mutableStateOf<Game?>(null) }

    LaunchedEffect(Unit) {
        GameLibrary.installBundledStarters(ctx)
        games = GameLibrary.games(ctx)
    }

    val game = current
    if (game == null) {
        ShelfScreen(games) { current = it }
    } else {
        GameScreen(game) { current = null }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ShelfScreen(games: List<Game>, onOpen: (Game) -> Unit) {
    Scaffold(
        topBar = { TopAppBar(title = { Text("JACL v${GlkBridge.version}") }) }
    ) { pad ->
        if (games.isEmpty()) {
            Box(Modifier.fillMaxSize().padding(pad), contentAlignment = Alignment.Center) {
                Text("No games yet — import a .jaclgame to begin.")
            }
        } else {
            LazyColumn(Modifier.fillMaxSize().padding(pad)) {
                items(games) { g ->
                    ListItem(
                        headlineContent = { Text(g.title) },
                        modifier = Modifier.fillMaxWidth().clickable { onOpen(g) }
                    )
                    HorizontalDivider()
                }
            }
        }
    }
}

@Composable
fun GameScreen(game: Game, onBack: () -> Unit) {
    val bridge = remember(game.file.path) { GlkBridge() }
    val density = LocalDensity.current
    val measurer = rememberTextMeasurer()
    var started by remember { mutableStateOf(false) }
    var containerW by remember { mutableStateOf(0) }
    // The per-game header colour the web/iPad use behind the banner image.
    val headerColor = remember(game.file.path) {
        GameLibrary.stringConstant(game.file, "header_colour")?.let { parseHexColor(it) }
    }

    DisposableEffect(game.file.path) {
        onDispose { bridge.stop() }
    }

    Column(
        Modifier
            .fillMaxSize()
            .onSizeChanged { sz ->
                if (sz.width > 0 && !started) {
                    containerW = sz.width
                    // Measure the monospaced "0" cell at the body size so the
                    // status grid columns line up (mirrors GlkBridge.metrics).
                    val m = measurer.measure(
                        AnnotatedString("0"),
                        style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 17.sp)
                    )
                    bridge.start(
                        gamePath = game.file.path,
                        widthPx = sz.width - with(density) { 16.dp.toPx() }.toInt(),
                        heightPx = sz.height,
                        cellWidthPx = m.size.width.toDouble(),
                        cellHeightPx = m.size.height.toDouble(),
                    )
                    started = true
                } else if (sz.width > 0 && started && sz.width != containerW) {
                    containerW = sz.width
                    val m = measurer.measure(
                        AnnotatedString("0"),
                        style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 17.sp)
                    )
                    bridge.resize(
                        sz.width - with(density) { 16.dp.toPx() }.toInt(), sz.height,
                        m.size.width.toDouble(), m.size.height.toDouble())
                }
            }
    ) {
        // Status (grid) windows on top.
        for (w in bridge.windows.filter { it.type == "grid" }) {
            GridView(bridge.grids[w.id] ?: emptyList())
        }
        // Transcript (buffer) windows.
        for (w in bridge.windows.filter { it.type == "buffer" }) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                TranscriptView(bridge, bridge.buffers[w.id] ?: emptyList(), headerColor)
            }
        }
        InputBar(bridge)
    }
}

@Composable
fun GridView(rows: List<List<RenderedSpan>>) {
    Surface(color = Color(0xFF202020), contentColor = Color.White) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp)) {
            for (row in rows) {
                Text(
                    buildSpans(row, Color(0xFF82B1FF)),   // light accent on the dark bar
                    fontFamily = FontFamily.Monospace,
                    fontSize = 17.sp,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
fun TranscriptView(bridge: GlkBridge, paras: List<RenderedParagraph>, headerColor: Color?) {
    val scroll = rememberScrollState()
    LaunchedEffect(paras.size, paras.lastOrNull()?.spans?.sumOf { it.text.length }) {
        scroll.scrollTo(scroll.maxValue)
    }
    // Banner (header-colour) treatment applies only to the first image, like iOS.
    var bannerShown = false
    Column(Modifier.fillMaxSize().verticalScroll(scroll).padding(vertical = 12.dp)) {
        for (p in paras) {
            val img = p.spans.firstOrNull { it.image != null }?.image
            if (img != null) {
                if (!bannerShown) { BannerImage(bridge, img, headerColor); bannerShown = true }
                else BlorbImage(bridge, img)
            }
            val text = buildSpans(p.spans, MaterialTheme.colorScheme.primary)
            if (text.isNotEmpty()) {
                // Body text inset 12dp; the banner above runs full-bleed.
                Text(text, fontFamily = FontFamily.Serif, fontSize = 17.sp,
                    modifier = Modifier.padding(horizontal = 12.dp))
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}

/** The opening banner: the game's header_colour painted behind it, full-bleed
 *  to the right edge with the art flush-left -- matching the web/iPad header. */
@Composable
fun BannerImage(bridge: GlkBridge, num: Int, headerColor: Color?) {
    val bmp: ImageBitmap = remember(num) {
        bridge.image(num)?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
    } ?: return
    Box(
        Modifier.fillMaxWidth()
            .then(if (headerColor != null) Modifier.background(headerColor) else Modifier),
        contentAlignment = Alignment.CenterStart
    ) {
        Image(bmp, contentDescription = null)   // natural size, left-aligned
    }
    Spacer(Modifier.height(8.dp))
}

@Composable
fun BlorbImage(bridge: GlkBridge, num: Int) {
    val bmp: ImageBitmap? = remember(num) {
        bridge.image(num)?.let { bytes ->
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
        }
    }
    if (bmp != null) {
        Image(bmp, contentDescription = null, modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp))
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
fun InputBar(bridge: GlkBridge) {
    val input = bridge.pendingInput
    when (input?.type) {
        "line" -> {
            var text by remember(bridge.buffers) { mutableStateOf("") }
            val focus = remember { FocusRequester() }
            // Auto-focus (and raise the keyboard) whenever a line prompt appears,
            // so you can just type -- as the iPad does.
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
                    textStyle = TextStyle(fontSize = 17.sp, color = MaterialTheme.colorScheme.onSurface),
                    cursorBrush = androidx.compose.ui.graphics.SolidColor(MaterialTheme.colorScheme.onSurface),
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(
                        autoCorrect = false,
                        capitalization = KeyboardCapitalization.None,
                        imeAction = ImeAction.Send,
                    ),
                    keyboardActions = KeyboardActions(onSend = { submit() }),
                    modifier = Modifier.weight(1f).focusRequester(focus),
                )
                TextButton(onClick = { submit() }) { Text("Enter") }
            }
        }
        "char" -> {
            Button(
                onClick = { bridge.submitChar(" ") },
                modifier = Modifier.fillMaxWidth().padding(8.dp)
            ) { Text("Tap to continue") }
        }
        else -> if (bridge.finished) {
            Text("The game has ended.", Modifier.padding(8.dp),
                color = MaterialTheme.colorScheme.onSurfaceVariant)
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

/** Build an AnnotatedString from spans, mapping Glk styles to text styling.
 *  `inputColor` tints the player's echoed command (the Glk "input" style),
 *  matching the iOS accent-coloured echo. */
fun buildSpans(spans: List<RenderedSpan>, inputColor: Color): AnnotatedString = buildAnnotatedString {
    for (s in spans) {
        if (s.text.isEmpty()) continue
        val style = when (s.style) {
            "header", "subheader" -> SpanStyle(fontWeight = FontWeight.Bold)
            "emphasized", "note" -> SpanStyle(fontStyle = FontStyle.Italic)
            "alert" -> SpanStyle(color = Color(0xFFD32F2F))
            "input" -> SpanStyle(color = inputColor)            // the player's typed command
            "preformatted", "user1", "user2" -> SpanStyle(fontFamily = FontFamily.Monospace)
            else -> SpanStyle()
        }
        val withLink = if (s.hyperlink != null)
            style.copy(color = Color(0xFF1565C0), textDecoration = TextDecoration.Underline) else style
        withStyle(withLink) { append(s.text) }
    }
}
