package au.com.dangarmarine.jacl

/** A room on the map. (x,y) is the node's top-left in the web map's pixel
 *  space (120 px/cell, 80 px nodes); [here] marks the current room. */
data class MapNode(val x: Int, val y: Int, val here: Boolean, val label: String)

/** An exit line between two room centres; [updown] draws an up/down arrow. */
data class MapEdge(val x1: Int, val y1: Int, val x2: Int, val y2: Int, val updown: Boolean)

/** The explored map: canvas size [w]x[h] (px) plus nodes and edges. */
data class GameMap(val w: Int, val h: Int, val nodes: List<MapNode>, val edges: List<MapEdge>)

/**
 * Parse a `<jacl-map>` data block (the lines between the markers) emitted by
 * mapping.library's +map_native. Records: "M w h", "E x1 y1 x2 y2 ud",
 * "N x y here label...". Returns null if there's nothing drawable.
 */
fun parseGameMap(lines: List<String>): GameMap? {
    var w = 0; var h = 0
    val nodes = ArrayList<MapNode>()
    val edges = ArrayList<MapEdge>()
    for (line in lines) {
        val s = line.trim()
        when {
            s.startsWith("M ") -> {
                val p = s.split(' ')
                w = p.getOrNull(1)?.toIntOrNull() ?: 0
                h = p.getOrNull(2)?.toIntOrNull() ?: 0
            }
            s.startsWith("E ") -> {
                val p = s.split(' ')
                if (p.size >= 6) edges.add(MapEdge(
                    p[1].toInt(), p[2].toInt(), p[3].toInt(), p[4].toInt(), p[5] == "1"))
            }
            s.startsWith("N ") -> {
                // "N x y here label..." -- label is the rest (may contain spaces).
                val p = s.split(' ', limit = 5)
                if (p.size >= 5) nodes.add(MapNode(
                    p[1].toInt(), p[2].toInt(), p[3] == "1", p[4]))
            }
        }
    }
    if (nodes.isEmpty()) return null
    return GameMap(if (w > 0) w else 1, if (h > 0) h else 1, nodes, edges)
}
