#!/bin/sh
# gen-landing.sh -- generate the JACL landing page (index.html) on stdout.
#
#   gen-landing.sh [PROJECTS_DIR] [GAMES_DIR]
#
# PROJECTS_DIR: where the .jacl sources live (for titles / languages).
# GAMES_DIR:    where built .jaclgame packages live; the "Get it for iPad"
#               tab lists only games that have one.
#
# Two CSS-only tabs: "Play Online" (fcgijacl links) and "Get it for iPad"
# (.jaclgame downloads). Tab switching is pure CSS; a small script at the end
# adds one enhancement -- opening the page as .../#get selects the iPad tab.

projects="${1:-../projects}"
games="${2:-$projects/jaclgames}"

# Resolve $title and $lang for a game source file.
game_meta() {
    g="$1"
    name=$(basename "$g" .jacl)
    title_line=$(grep -E '^constant[[:space:]]+game_title[[:space:]]' "$g" 2>/dev/null | head -1)
    title=$(echo "$title_line" | sed -E 's/.*"([^"]+)".*/\1/')
    if [ -z "$title" ] || [ "$title" = "$title_line" ]; then
        title="$name"
    fi
    if grep -q '^#include "indonesian_verbs.library"' "$g"; then lang="Indonesian"
    elif grep -q '^#include "spanish_verbs.library"' "$g"; then lang="Spanish"
    elif grep -q '^#include "french_verbs.library"\|^#include "french_webinterface.library"' "$g"; then lang="French"
    elif grep -q '^#include "german_verbs.library"' "$g"; then lang="German"
    else lang="English"; fi
}

cat <<'HEAD'
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>JACL Interactive Fiction</title>
  <style>
    :root { --accent: #42596d; --accent-dark: #2c3e50; --bg: #f5f1ea; --card: #fff; --muted: #6b6b6b; }
    * { box-sizing: border-box; }
    body { font-family: Georgia, Palatino, Times, serif; margin: 0; padding: 0; color: #2c2c2c; line-height: 1.6; background: var(--bg); }
    header.hero { background: linear-gradient(135deg, var(--accent-dark), var(--accent)); color: #fff; padding: 3em 1em 2em; text-align: center; }
    header.hero h1 { margin: 0 0 0.3em; font-size: 2.4em; letter-spacing: 0.02em; }
    header.hero p { margin: 0; opacity: 0.9; font-style: italic; font-size: 1.05em; }
    nav.topnav { background: #2c3e50; text-align: center; padding: 0.5em; }
    nav.topnav a { color: #fff; text-decoration: none; margin: 0 1em; font-size: 0.95em; }
    nav.topnav a:hover { text-decoration: underline; }
    main { max-width: 760px; margin: 2em auto; padding: 0 1em; }
    section.intro { background: var(--card); padding: 1.5em 1.8em; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); margin-bottom: 2em; }
    section.intro p { margin: 0 0 0.8em; }
    section.intro p:last-child { margin-bottom: 0; }
    h2 { color: var(--accent-dark); border-bottom: 2px solid var(--accent); padding-bottom: 0.3em; margin-top: 0; }
    ul.games { list-style: none; padding: 0; margin: 0; display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 0.9em; }
    ul.games li { background: var(--card); border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); transition: transform 0.1s, box-shadow 0.1s; }
    ul.games li:hover { transform: translateY(-2px); box-shadow: 0 3px 8px rgba(0,0,0,0.12); }
    ul.games a { display: block; padding: 0.9em 1.1em; text-decoration: none; color: var(--accent); font-weight: bold; font-size: 1.05em; }
    ul.games .lang { color: var(--muted); font-weight: normal; font-size: 0.88em; margin-left: 0.3em; }
    ul.apps { list-style: none; padding: 0; margin: 0.4em 0 1.2em; display: grid; grid-template-columns: repeat(auto-fill, minmax(170px, 1fr)); gap: 0.8em; }
    ul.apps li { background: var(--card); border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); transition: transform 0.1s, box-shadow 0.1s; }
    ul.apps li:hover { transform: translateY(-2px); box-shadow: 0 3px 8px rgba(0,0,0,0.12); }
    ul.apps a, ul.apps li.soon span { display: block; padding: 0.85em 1.05em; text-decoration: none; }
    ul.apps a { color: var(--accent); }
    ul.apps strong { display: block; font-size: 1.05em; }
    ul.apps em { display: block; color: var(--muted); font-size: 0.86em; font-style: normal; }
    ul.apps li.soon { opacity: 0.55; }
    pre.apt { background: var(--accent-dark); color: var(--bg); font-family: ui-monospace, Menlo, Consolas, monospace; padding: 1em; border-radius: 6px; overflow-x: auto; font-size: 0.78em; line-height: 1.5; }
    nav.tabs { display: flex; gap: 0.4em; border-bottom: 2px solid var(--accent); margin-bottom: 1.4em; }
    nav.tabs label { padding: 0.55em 1.2em; cursor: pointer; color: var(--muted); font-weight: bold; border-radius: 6px 6px 0 0; }
    input.tabsel { position: absolute; left: -9999px; }
    .tabpane { display: none; }
    #tab-play:checked ~ .tabpane.play { display: block; }
    #tab-get:checked ~ .tabpane.get { display: block; }
    #tab-play:checked ~ nav.tabs label[for=tab-play],
    #tab-get:checked ~ nav.tabs label[for=tab-get] { color: var(--accent-dark); background: var(--card); box-shadow: 0 -1px 3px rgba(0,0,0,0.06); }
    p.tabintro { color: var(--muted); margin: 0 0 1.2em; }
    footer { margin: 3em 1em 2em; color: var(--muted); font-size: 0.85em; text-align: center; }
    footer a { color: var(--accent); }
  </style>
</head>
<body>
  <header class="hero">
    <h1>JACL Interactive Fiction</h1>
    <p>Text adventures by Stuart Allen, written in JACL</p>
  </header>
  <nav class="topnav">
    <a href="/guide/">Guide</a>
    <a href="https://github.com/DangarStu/JACL">Source</a>
  </nav>
  <main>
    <section class="intro">
      <p><strong>Interactive fiction</strong> is a genre of text-based computer games where you read a description of a scene and type simple commands (for example <em>go north</em>, <em>take lamp</em>, or <em>examine desk</em>) to move through a story and solve its puzzles.</p>
      <p>It is a direct descendant of the text adventures of the 1970s and 80s &mdash; titles like <em>Zork</em>, <em>Adventure</em>, and <em>The Hitchhiker&rsquo;s Guide to the Galaxy</em> &mdash; and has a thriving modern community of authors and players.</p>
      <p>Want to write your own? The <a href="/guide/">JACL Guide</a> walks you through the language &mdash; and you can buy it in print as <a href="https://www.lulu.com/shop/stuart-allen/jacl-authors-guide/paperback/product-e7nkeqd.html"><em>The JACL Author&rsquo;s Guide</em></a> paperback from Lulu.</p>
      <p>The interpreter source code &mdash; for running JACL locally or contributing &mdash; is on <a href="https://github.com/DangarStu/JACL">GitHub</a>.</p>
    </section>
    <section class="intro">
      <h2>Get the app</h2>
      <p>Play online below, or install a native app &mdash; offline play with graphics, sound, and a live map window.</p>
      <ul class="apps">
        <li><a href="https://apps.apple.com/app/id6780354110"><strong>iPhone &amp; iPad</strong><em>App Store</em></a></li>
        <li><a href="https://github.com/DangarStu/JACL/releases/latest"><strong>macOS</strong><em>Download .dmg</em></a></li>
        <li><a href="https://github.com/DangarStu/JACL/releases/latest"><strong>Linux</strong><em>.deb / AppImage</em></a></li>
        <li class="soon"><span><strong>Android</strong><em>Coming to Google Play</em></span></li>
      </ul>
      <p class="tabintro">Debian &amp; Ubuntu &mdash; install and auto-update with apt:</p>
      <pre class="apt">curl -fsSL https://apt.dangarmarine.com.au/jacl.gpg | sudo gpg --dearmor -o /usr/share/keyrings/jacl.gpg
echo "deb [signed-by=/usr/share/keyrings/jacl.gpg] https://apt.dangarmarine.com.au stable main" | sudo tee /etc/apt/sources.list.d/jacl.list
sudo apt update &amp;&amp; sudo apt install jacl-desktop</pre>
    </section>
    <input class="tabsel" type="radio" name="tab" id="tab-play" checked>
    <input class="tabsel" type="radio" name="tab" id="tab-get">
    <nav class="tabs">
      <label for="tab-play">Play Online</label>
      <label for="tab-get">Get it for iPad</label>
    </nav>
    <div class="tabpane play">
      <h2>Play a game</h2>
      <ul class="games">
HEAD

# --- Play Online list ---
for game in "$projects"/*.jacl; do
    [ -e "$game" ] || continue
    grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$game" || continue
    game_meta "$game"
    printf '        <li><a href="/jacl/%s.fcgi">%s <span class="lang">(%s)</span></a></li>\n' "$name" "$title" "$lang"
done

cat <<'MID'
      </ul>
    </div>
    <div class="tabpane get">
      <h2>Get it for iPad</h2>
      <p class="tabintro">Download a game on your iPad and choose <strong>Open in JACL</strong> to play it in the app &mdash; offline, with graphics. Each download is a single <code>.jaclgame</code> file.</p>
      <ul class="games">
MID

# --- Get it for iPad list (only games with a built package) ---
for game in "$projects"/*.jacl; do
    [ -e "$game" ] || continue
    grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$game" || continue
    game_meta "$game"
    [ -f "$games/$name.jaclgame" ] || continue
    printf '        <li><a href="/games/%s.jaclgame" download>%s <span class="lang">(%s)</span></a></li>\n' "$name" "$title" "$lang"
done

cat <<'TAIL'
      </ul>
    </div>
  </main>
  <footer>Served by JACL. Online games run via the fcgijacl interpreter. <a href="/guide/">Read the Guide</a> to write your own. &middot; <a href="/privacy.html">Privacy</a></footer>
  <script>
    /* Deep-link: opening the page as .../#get (e.g. the JACL iPad app's
       "Get more games" link) selects the "Get it for iPad" tab directly.
       Pure enhancement -- without JS the page just shows the default tab. */
    if (location.hash === '#get') {
      var g = document.getElementById('tab-get');
      if (g) g.checked = true;
    }
  </script>
</body></html>
TAIL
