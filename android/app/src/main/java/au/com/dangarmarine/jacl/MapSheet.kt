package au.com.dangarmarine.jacl

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.hypot
import kotlin.math.min

/** A swipe-to-dismiss sheet showing the explored map, drawn on a canvas. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MapSheet(map: GameMap?, onDismiss: () -> Unit) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = sheetState) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(0.92f)) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Map", style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.weight(1f))
                TextButton(onClick = onDismiss) { Text("Done") }
            }
            if (map == null) {
                Text("No map yet.", Modifier.padding(16.dp),
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            } else {
                MapCanvas(map, Modifier.fillMaxSize())
            }
        }
    }
}

@Composable
private fun MapCanvas(map: GameMap, modifier: Modifier) {
    val measurer = rememberTextMeasurer()
    val here = MaterialTheme.colorScheme.primaryContainer
    val other = MaterialTheme.colorScheme.surfaceVariant
    val edge = MaterialTheme.colorScheme.onSurfaceVariant
    val border = MaterialTheme.colorScheme.outline
    val onNode = MaterialTheme.colorScheme.onSurface

    BoxWithConstraints(modifier) {
        // Fit the whole map (w x h px) into the viewport, then let pinch/drag
        // adjust from there.
        val vw = constraints.maxWidth.toFloat()
        val vh = constraints.maxHeight.toFloat()
        val fit = remember(map, vw, vh) {
            if (map.w <= 0 || map.h <= 0) 1f else min(vw / map.w, vh / map.h) * 0.95f
        }
        var scale by remember(map) { mutableFloatStateOf(fit) }
        var offset by remember(map) {
            mutableStateOf(Offset((vw - map.w * fit) / 2f, (vh - map.h * fit) / 2f))
        }

        Canvas(
            Modifier.fillMaxSize().pointerInput(map) {
                detectTransformGestures { centroid, pan, zoom, _ ->
                    // Zoom about the gesture centroid, and pan.
                    val newScale = (scale * zoom).coerceIn(0.2f, 6f)
                    offset = centroid - (centroid - offset) * (newScale / scale) + pan
                    scale = newScale
                }
            }
        ) {
            withTransform({
                translate(offset.x, offset.y)
                scale(scale, scale, pivot = Offset.Zero)
            }) {
                for (e in map.edges) {
                    val a = Offset(e.x1.toFloat(), e.y1.toFloat())
                    val b = Offset(e.x2.toFloat(), e.y2.toFloat())
                    drawLine(edge, a, b, strokeWidth = 2f)
                    if (e.updown) drawArrowhead(a, b, edge)
                }
                for (n in map.nodes) {
                    val tl = Offset(n.x.toFloat(), n.y.toFloat())
                    val sz = Size(80f, 80f)
                    drawRoundRect(if (n.here) here else other, tl, sz, CornerRadius(12f))
                    drawRoundRect(border, tl, sz, CornerRadius(12f), style = Stroke(width = 2f))
                    val layout = measurer.measure(
                        n.label,
                        style = TextStyle(fontSize = 11.sp, color = onNode, textAlign = TextAlign.Center),
                        constraints = Constraints(maxWidth = 76),
                    )
                    drawText(layout, topLeft = Offset(
                        tl.x + (80 - layout.size.width) / 2f,
                        tl.y + (80 - layout.size.height) / 2f))
                }
            }
        }
    }
}

/** A small arrowhead at the midpoint of a->b, pointing toward b. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawArrowhead(
    a: Offset, b: Offset, color: Color,
) {
    val mid = Offset((a.x + b.x) / 2f, (a.y + b.y) / 2f)
    val dx = b.x - a.x; val dy = b.y - a.y
    val len = hypot(dx, dy)
    if (len < 1f) return
    val ux = dx / len; val uy = dy / len            // unit along the line
    val back = 9f; val spread = 6f
    val baseX = mid.x - ux * back; val baseY = mid.y - uy * back
    // perpendicular
    val px = -uy * spread; val py = ux * spread
    drawLine(color, mid, Offset(baseX + px, baseY + py), strokeWidth = 2f)
    drawLine(color, mid, Offset(baseX - px, baseY - py), strokeWidth = 2f)
}
