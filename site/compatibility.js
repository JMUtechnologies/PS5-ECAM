// Playtendoh's site: how games run. The owner's games (list.json beside this page, from
// tools/compat-list.json: site/build.py) keep the owner's status; the reports' table, read live from the report
// intake (tools/log-intake), which rebuilds it at most once an hour, gives every game its counts, and the games the list
// does not have a status of their own, as the console reads them (tools/compat_feed.py). compatibility.json beside this
// page when the intake can't be reached. Every value goes into the page as text, never as HTML.
'use strict';

const SIDES = { wiiu: 'Wii U', '3ds': '3DS', ds: 'DS', switch: 'Switch', switch2: 'Switch 2', gamecube: 'GameCube', wii: 'Wii', n64: 'N64', nes: 'NES',
	gb: 'Game Boy', gbc: 'Game Boy Color', gba: 'Game Boy Advance', snes: 'SNES', vb: 'Virtual Boy', pokemini: 'Pokémon mini', fds: 'FDS', sgb: 'Super Game Boy',
	gw: 'Game & Watch', satellaview: 'Satellaview', vs: 'VS. System', ique: 'iQue' };
const STATUS = { playable: 'Playable', issues: 'Issues', broken: 'Not playable', few: 'Too few reports' };
const MODELS = { base: 'PS5', slim: 'Slim', pro: 'Pro' };

const side = (key) => SIDES[key] || key;
// a name as the console compares it: lower case letters and digits only (compat_feed.py's key)
const key = (name) => [...String(name).toLowerCase()].filter((c) => /[a-z0-9]/.test(c)).join('');
// a game only the reports have: a status once two of its sessions said something, as compat_feed.py's from_reports
function reported(g) {
	const told = g.ended + g.failed + g.crashed;
	if (told < 2) return 'few';
	return g.ended >= told * 0.8 ? 'playable' : g.ended === 0 ? 'broken' : 'issues';
}
let games = [];

function cell(row, text, cls) {
	const td = document.createElement('td');
	td.textContent = text;
	if (cls) td.className = cls;
	row.appendChild(td);
	return td;
}

function draw() {
	const query = document.getElementById('search').value.trim().toLowerCase();
	const wanted = document.getElementById('side').value;
	const status = document.getElementById('status').value;
	const body = document.querySelector('#games tbody');
	body.textContent = '';
	let shown = 0;
	for (const g of games) {
		if (wanted && g.side !== wanted) continue;
		if (status && g.status !== status) continue;
		if (query && !g.name.toLowerCase().includes(query) && !(g.titleId || '').includes(query)) continue;
		shown++;
		const row = document.createElement('tr');
		const name = cell(row, g.name || g.titleId);
		const under = [g.region, g.titleId].filter(Boolean).join(' · ');
		if (under) {
			const id = document.createElement('small');
			id.textContent = under;
			name.appendChild(document.createElement('br'));
			name.appendChild(id);
		}
		if (g.commonError) name.title = 'Most often: ' + g.commonError;
		cell(row, side(g.side));
		cell(row, STATUS[g.status] || g.status, 'status-' + g.status);
		const counted = g.sessions != null;
		cell(row, counted ? String(g.consoles) : '–');
		cell(row, counted ? String(g.sessions) : '–');
		cell(row, counted ? `${g.ended} / ${g.failed} / ${g.crashed}` : '–');
		cell(row, g.medianMinutes == null ? '–' : `${g.medianMinutes} min`);
		cell(row, (g.models || []).map((m) => MODELS[m] || m).join(', ') || '–');
		cell(row, (g.firmware || []).join(', ') || '–');
		cell(row, g.lastSeen || '–');
		body.appendChild(row);
	}
	const generated = document.getElementById('summary').dataset.generated;
	document.getElementById('summary').textContent = games.length
		? `${shown} of ${games.length} games shown.${generated ? ' Updated ' + generated.slice(0, 10) + '.' : ''}`
		: 'No games yet.';
}

const ROW = ['side', 'titleId', 'name', 'status'];

async function fetchTable(url, ms) {
	const controller = new AbortController();
	const timer = setTimeout(() => controller.abort(), ms);
	try {
		const response = await fetch(url, { signal: controller.signal });
		if (!response.ok) return null;
		const table = await response.json();
		return table && table.schema === 1 && Array.isArray(table.games) ? table : null;
	} catch {
		return null;
	} finally {
		clearTimeout(timer);
	}
}

async function load() {
	const intake = (document.querySelector('meta[name="reports-url"]') || {}).content || '';
	const [live, list] = await Promise.all([
		intake.startsWith('https://') ? fetchTable(intake + '/v1/compatibility.json', 8000) : null,
		fetchTable('list.json', 15000),
	]);
	const table = live || (await fetchTable('compatibility.json', 15000));
	// the list's games first, by side and name; a report of one gives it its counts (the list's one row adds up a game's
	// regions, and keeps the first title ID), the others are rows of their own
	games = list ? list.games.filter((g) => g && ['side', 'name', 'status'].every((k) => typeof g[k] === 'string')).map((g) => ({ ...g })) : [];
	const index = new Map(games.map((g) => [g.side + ' ' + key(g.name), g]));
	for (const r of table ? table.games.filter((g) => g && ROW.every((k) => typeof g[k] === 'string')) : []) {
		const mine = index.get(r.side + ' ' + key(r.name));
		if (!mine) {
			games.push({ ...r, status: reported(r) });
			continue;
		}
		if (mine.sessions == null) {
			Object.assign(mine, { ...r, name: mine.name, status: mine.status, firmware: [...r.firmware], models: [...r.models] });
			continue;
		}
		for (const k of ['consoles', 'sessions', 'ended', 'failed', 'crashed']) mine[k] += r[k];
		mine.firmware = [...new Set([...mine.firmware, ...r.firmware])];
		mine.models = [...new Set([...mine.models, ...r.models])];
		if (r.lastSeen > mine.lastSeen) mine.lastSeen = r.lastSeen;
	}
	games.sort((a, b) => side(a.side).localeCompare(side(b.side)) || a.name.localeCompare(b.name));
	if (table && table.generated) document.getElementById('summary').dataset.generated = table.generated;
	const select = document.getElementById('side');
	for (const key of [...new Set(games.map((g) => g.side))].sort((a, b) => side(a).localeCompare(side(b)))) {
		const option = document.createElement('option');
		option.value = key;
		option.textContent = side(key);
		select.appendChild(option);
	}
	for (const id of ['search', 'side', 'status']) document.getElementById(id).addEventListener('input', draw);
	draw();
}

load();
