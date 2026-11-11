// Playtendoh's site: the table players' reports make, read live from the report intake (tools/log-intake), which
// rebuilds it at most once an hour; compatibility.json beside this page (site/build.py) when the intake can't be
// reached. Every value goes into the page as text, never as HTML.
'use strict';

const SIDES = { wiiu: 'Wii U', '3ds': '3DS', ds: 'DS', switch: 'Switch', switch2: 'Switch 2', gamecube: 'GameCube', wii: 'Wii', n64: 'N64', nes: 'NES',
	gb: 'Game Boy', gbc: 'Game Boy Color', gba: 'Game Boy Advance', snes: 'SNES', vb: 'Virtual Boy', pokemini: 'Pokémon mini', fds: 'FDS', sgb: 'Super Game Boy',
	gw: 'Game & Watch', satellaview: 'Satellaview', vs: 'VS. System', ique: 'iQue' };
const STATUS = { works: 'Works', mixed: 'Mixed', fails: 'Fails' };
const MODELS = { base: 'PS5', slim: 'Slim', pro: 'Pro' };

const side = (key) => SIDES[key] || key;
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
		if (query && !g.name.toLowerCase().includes(query) && !g.titleId.includes(query)) continue;
		shown++;
		const row = document.createElement('tr');
		const name = cell(row, g.name || g.titleId);
		const id = document.createElement('small');
		id.textContent = g.titleId;
		name.appendChild(document.createElement('br'));
		name.appendChild(id);
		if (g.commonError) name.title = 'Most often: ' + g.commonError;
		cell(row, side(g.side));
		cell(row, STATUS[g.status] || g.status, 'status-' + g.status);
		cell(row, String(g.consoles));
		cell(row, String(g.sessions));
		cell(row, `${g.ended} / ${g.failed} / ${g.crashed}`);
		cell(row, g.medianMinutes == null ? '–' : `${g.medianMinutes} min`);
		cell(row, (g.models || []).map((m) => MODELS[m] || m).join(', ') || '–');
		cell(row, (g.firmware || []).join(', ') || '–');
		cell(row, g.lastSeen);
		body.appendChild(row);
	}
	const generated = document.getElementById('summary').dataset.generated;
	document.getElementById('summary').textContent = games.length
		? `${shown} of ${games.length} games shown.${generated ? ' Updated ' + generated.slice(0, 10) + '.' : ''}`
		: 'No reports yet.';
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
	let table = intake.startsWith('https://') ? await fetchTable(intake + '/v1/compatibility.json', 8000) : null;
	if (!table) table = await fetchTable('compatibility.json', 15000);
	games = table ? table.games.filter((g) => g && ROW.every((k) => typeof g[k] === 'string')) : [];
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
