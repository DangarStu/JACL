/*
 * OFFSIDE: import teams and players from a CSV file (the Teams tab's
 * "Import teams and players from a spreadsheet" box, +import_box).
 *
 * JACL doesn't accept file uploads, so the file is read and checked here in
 * the browser and its rows are sent to the game as ordinary parameters, a
 * batch at a time, with the command "import rows". Each row goes as
 *   team|player|position|shirt|gender|hidden
 * with "-" for anything blank; the game re-checks everything it is sent.
 */
(function () {
  var box = document.getElementById('importbox');
  var preview = document.getElementById('importpreview');
  var input = document.getElementById('importfile');
  if (!box || !preview || !input) return;

  var POSITIONS = (preview.getAttribute('data-positions') || '').split('|');
  var MAX_TEAMS = parseInt(preview.getAttribute('data-maxteams'), 10) || 40;
  var BATCH = parseInt(preview.getAttribute('data-batch'), 10) || 10;
  var DONE_KEY = 'offside-import-done';
  var rows = [];

  function el(tag, text, cls) {
    var e = document.createElement(tag);
    if (text != null) e.textContent = text;
    if (cls) e.className = cls;
    return e;
  }

  function plural(n, word) {
    return n + ' ' + word + (n == 1 ? '' : 's');
  }

  // A SUMMARY LEFT BY AN IMPORT THAT JUST FINISHED AND RELOADED THE PAGE
  try {
    var done = sessionStorage.getItem(DONE_KEY);
    if (done) {
      sessionStorage.removeItem(DONE_KEY);
      box.open = true;
      preview.appendChild(el('p', done, 'notice'));
    }
  } catch (e) { /* NO STORAGE: NOTHING TO SHOW */ }

  // RFC 4180 CSV: QUOTED FIELDS MAY HOLD THE DELIMITER, "" AND LINE BREAKS.
  // THE DELIMITER IS WHICHEVER OF , ; OR TAB THE FIRST LINE USES MOST, SINCE
  // SPREADSHEETS IN SOME LOCALES SAVE "CSV" WITH SEMICOLONS.
  function parseCSV(text) {
    if (text.charCodeAt(0) == 0xFEFF) text = text.slice(1);
    var first = text.split(/\r?\n/)[0] || '';
    var delim = ',', best = -1;
    [',', ';', '\t'].forEach(function (d) {
      var n = first.split(d).length - 1;
      if (n > best) { best = n; delim = d; }
    });
    var out = [], row = [], field = '', quoted = false, i, c;
    for (i = 0; i < text.length; i++) {
      c = text[i];
      if (quoted) {
        if (c == '"') {
          if (text[i + 1] == '"') { field += '"'; i++; } else quoted = false;
        } else field += c;
      } else if (c == '"') quoted = true;
      else if (c == delim) { row.push(field); field = ''; }
      else if (c == '\n' || c == '\r') {
        if (c == '\r' && text[i + 1] == '\n') i++;
        row.push(field); out.push(row); row = []; field = '';
      } else field += c;
    }
    if (field !== '' || row.length) { row.push(field); out.push(row); }
    return out;
  }

  // WHICH COLUMN HOLDS WHAT: FROM A HEADER ROW IF THE FIRST ROW HAS ONE
  // (ANY CELL STARTING "TEAM"), OTHERWISE THE DOCUMENTED ORDER
  var HEADS = {
    team: ['team', 'club', 'side'],
    player: ['player', 'name', 'player name'],
    position: ['position', 'pos'],
    shirt: ['number', 'shirt', 'shirt number', 'no', 'no.', '#'],
    gender: ['gender', 'sex'],
    hidden: ['hidden', 'hide', 'hide name']
  };

  function columns(head) {
    var cols = { team: 0, player: 1, position: 2, shirt: 3, gender: 4, hidden: 5 };
    var lower = head.map(function (h) { return h.trim().toLowerCase(); });
    if (!lower.some(function (h) { return h.indexOf('team') === 0; })) return { cols: cols, header: false };
    cols = {};
    Object.keys(HEADS).forEach(function (k) {
      cols[k] = -1;
      lower.forEach(function (h, i) {
        if (cols[k] == -1 && (HEADS[k].indexOf(h) != -1 || (k == 'team' && h.indexOf('team') === 0))) cols[k] = i;
      });
    });
    return { cols: cols, header: true };
  }

  // ONE LINE OF TEXT, WITH NO | (THE FIELD SEPARATOR ON THE WAY TO THE GAME)
  function clean(s) {
    return (s == null ? '' : String(s)).replace(/[\r\n\t]+/g, ' ').replace(/\|/g, '/').trim();
  }

  function position(p, warn) {
    if (!p) return '-';
    var l = p.toLowerCase();
    for (var i = 0; i < POSITIONS.length; i++) {
      if (POSITIONS[i].toLowerCase() == l) return POSITIONS[i];
    }
    var short = { gk: 'Goalkeeper', goalie: 'Goalkeeper', keeper: 'Goalkeeper', def: 'Defender',
                  mid: 'Midfielder', fwd: 'Forward', striker: 'Forward', attacker: 'Forward' };
    if (short[l] && POSITIONS.indexOf(short[l]) != -1) return short[l];
    warn('position "' + p + '" isn\'t one OFFSIDE knows, so it will be left blank');
    return '-';
  }

  function gender(g, warn) {
    var l = g.toLowerCase();
    if (!l || l == '-' || l == 'not stated') return '-';
    if (['f', 'female', 'w', 'woman', 'girl'].indexOf(l) != -1) return 'Female';
    if (['m', 'male', 'man', 'boy'].indexOf(l) != -1) return 'Male';
    warn('gender "' + g + '" will be left as not stated');
    return '-';
  }

  function hidden(h) {
    return ['yes', 'y', 'true', '1', 'x', 'hidden', 'hide'].indexOf(h.toLowerCase()) != -1 ? 'Hidden' : '-';
  }

  function check(table) {
    var warnings = [], teams = {}, order = [], players = 0, out = [];
    if (!table.length) return { rows: out, warnings: ['The file is empty.'], teams: order, players: 0 };
    var c = columns(table[0]);
    if (c.header && c.cols.team == -1) warnings.push('No Team column was found.');
    table.forEach(function (r, n) {
      if (c.header && n == 0) return;
      var line = n + 1;
      function get(k) { return c.cols[k] >= 0 ? clean(r[c.cols[k]]) : ''; }
      function warn(msg) { warnings.push('Row ' + line + ': ' + msg + '.'); }
      if (r.every(function (f) { return clean(f) === ''; })) return;
      var team = get('team'), player = get('player');
      if (!team) { warn('no team, so it was left out'); return; }
      if (team.length > 100 || player.length > 100) { warn('a name over 100 characters, so it was left out'); return; }
      // TEAMS MATCH WHATEVER THE CAPITALS, AS THEY DO IN THE GAME
      var key = team.toLowerCase();
      if (!teams[key]) { teams[key] = { name: team, players: 0 }; order.push(key); }
      team = teams[key].name;
      var shirt = get('shirt');
      if (shirt.length > 4) { warn('shirt number "' + shirt + '" is too long, so it will be left blank'); shirt = ''; }
      var row = [team, player || '-', position(get('position'), warn), shirt || '-',
                 gender(get('gender'), warn), hidden(get('hidden'))];
      if (player) { teams[key].players++; players++; }
      out.push(row.join('|'));
    });
    if (order.length > MAX_TEAMS) {
      warnings.push('A competition can have at most ' + MAX_TEAMS + ' teams; the file names ' + order.length +
                    ', so teams beyond that won\'t be added.');
    }
    return { rows: out, warnings: warnings, teams: order.map(function (k) { return teams[k]; }), players: players };
  }

  function show(result) {
    preview.innerHTML = '';
    rows = result.rows;
    if (!rows.length) {
      preview.appendChild(el('p', result.warnings[0] || 'No rows with a team were found in that file.', 'formerror'));
      return;
    }
    preview.appendChild(el('p', 'The file has ' + plural(result.teams.length, 'team') + ' and ' +
                                plural(result.players, 'player') + ':'));
    var ul = el('ul', null, 'importteams');
    result.teams.forEach(function (t) { ul.appendChild(el('li', t.name + ' (' + plural(t.players, 'player') + ')')); });
    preview.appendChild(ul);
    if (result.warnings.length) {
      var d = el('details', null, 'importwarn');
      d.appendChild(el('summary', plural(result.warnings.length, 'thing') + ' to check'));
      var wl = el('ul');
      result.warnings.forEach(function (w) { wl.appendChild(el('li', w)); });
      d.appendChild(wl);
      preview.appendChild(d);
    }
    var go = el('input');
    go.type = 'button';
    go.value = 'Import';
    go.onclick = function () { send(go); };
    preview.appendChild(go);
    preview.appendChild(el('span', ' Teams and players already here are left as they are.', 'key'));
  }

  // SENDS THE ROWS IN ORDER, ONE BATCH PER REQUEST, ADDING UP WHAT EACH
  // BATCH REPORTS, THEN RELOADS THE TAB TO SHOW THE NEW TEAMS
  function send(button) {
    button.disabled = true;
    var status = el('p', null, 'key');
    preview.appendChild(status);
    var total = { teams: 0, players: 0, skipped: 0, full: 0, bad: 0 };
    var form = box.querySelector('form.importform');
    var uid = form && form.querySelector('input[name=user_id]');
    var at = 0;

    function finish(message) {
      try { sessionStorage.setItem(DONE_KEY, message); } catch (e) { /* SHOWN BELOW INSTEAD */ }
      status.textContent = message;
      location.reload();
    }

    function summary() {
      var s = 'Imported ' + plural(total.teams, 'new team') + ' and ' + plural(total.players, 'new player') + '.';
      if (total.skipped) s += ' ' + plural(total.skipped, 'player') + (total.skipped == 1 ? ' was' : ' were') + ' already in their team.';
      if (total.full) s += ' ' + total.full + ' row(s) didn\'t fit: a competition holds at most ' + MAX_TEAMS + ' teams.';
      if (total.bad) s += ' ' + total.bad + ' row(s) couldn\'t be read.';
      return s;
    }

    function next() {
      if (at >= rows.length) { finish(summary()); return; }
      status.textContent = 'Importing… ' + at + ' of ' + rows.length + ' rows';
      var q = new URLSearchParams();
      q.set('ajax', 'true');
      q.set('command', 'import rows');
      if (uid && uid.value) q.set('user_id', uid.value);
      var dev = new URLSearchParams(location.search).get('device');
      if (dev) q.set('device', dev);
      q.set('ftoken', String(1 + Math.floor(Math.random() * 1999999999)));
      for (var i = 0; i < BATCH && at < rows.length; i++, at++) q.set('imp' + i, rows[at]);
      fetch(location.pathname + '?' + q.toString(), { credentials: 'same-origin' })
        .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.text(); })
        .then(function (html) {
          var doc = new DOMParser().parseFromString(html, 'text/html');
          var res = doc.getElementById('importresult');
          if (!res) {
            // NO RESULT MEANS THE GAME REFUSED (SIGNED OUT, OR NOT AN ORGANISER):
            // SHOW ITS NOTICE AND STOP
            var n = doc.querySelector('p.notice');
            throw new Error(n ? n.textContent : 'the server didn\'t accept the import');
          }
          Object.keys(total).forEach(function (k) { total[k] += parseInt(res.getAttribute('data-' + k), 10) || 0; });
          next();
        })
        .catch(function (err) {
          var msg = 'The import stopped: ' + err.message + '. ' + summary();
          if (total.teams || total.players) {
            finish(msg + ' Importing the file again will add the rest.');
          } else {
            status.textContent = msg;
            status.className = 'formerror';
            button.disabled = false;
          }
        });
    }
    next();
  }

  input.addEventListener('change', function () {
    var file = input.files && input.files[0];
    preview.innerHTML = '';
    if (!file) return;
    if (file.size > 1024 * 1024) {
      preview.appendChild(el('p', 'That file is over 1MB, which is far more than a competition needs. Is it the right one?', 'formerror'));
      return;
    }
    var reader = new FileReader();
    reader.onload = function () { show(check(parseCSV(String(reader.result)))); };
    reader.onerror = function () { preview.appendChild(el('p', 'That file couldn\'t be read.', 'formerror')); };
    reader.readAsText(file);
  });
})();
