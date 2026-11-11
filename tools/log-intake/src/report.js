// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh's report intake: what a report may hold (schema 1, docs/REPORTS.md), the second scrub of
// its text, and the compatibility table made from many reports. No Worker API in here, so test.mjs runs
// it with node alone.

// the boot log's 16 KB, the emulator's own three logs at 8 KB each, and the rest
export const MAX_BYTES = 64 * 1024;
const RESULTS = ['ended', 'failed', 'crashed', 'closed'];
const MODELS = ['base', 'slim', 'pro', 'unknown'];

// The app scrubs before it sends (port/app/reports.cpp); these are the patterns again, so a report from
// an older or altered app is no worse.
export function scrub(text) {
	return String(text)
		.replace(/[A-Za-z0-9._+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, '<email>')
		.replace(/\b[0-9A-Fa-f]{2}([:-])(?:[0-9A-Fa-f]{2}\1){4}[0-9A-Fa-f]{2}\b/g, '<mac>')
		.replace(/(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w]|\.\d)/g, '<ip>')
		.replace(/(?<![\w:])(?=[0-9A-Fa-f:]*::|(?:[0-9A-Fa-f]{0,4}:){6})[0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{0,4}){2,7}(?:%\w+)?(?![\w:])/g, '<ip>')
		.replace(/\b[0-9A-Fa-f]{32,}\b/g, '<hex>')
		.replace(/[A-Za-z][A-Za-z0-9+.-]*:\/\/[^\s]*/g, '<path>')
		// a path: from a slash after a space, a quote, = or : (or a drive's letter) to the line's end, as the
		// app cuts it; what follows its last slash stays when it is a file's name
		.replace(/(^|[\s"'(=:])((?:[A-Za-z]:)?[\\/](?=[^\s\\/])[^\n]*)/gm, (all, lead, path) => {
			const last = path.split(/[\\/]/).pop();
			return lead + (/\.[A-Za-z0-9]{1,5}\b/.test(last) ? '<path>/' + last : '<path>');
		})
}

function text(value, most) {
	return typeof value === 'string' ? value.replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g, '').slice(0, most) : null;
}

// The emulator's own logs of the session (Cemu's, Porpoise's, Eden's...): [{file, text}], three at most,
// a file's name and its last 8 KB, scrubbed again; JSON for the database, or null
function emulatorLogs(logs) {
	if (!Array.isArray(logs)) return null;
	const kept = [];
	for (const log of logs.slice(0, 3)) {
		if (!log || typeof log !== 'object') continue;
		const file = (text(log.file, 64) || '').replace(/[^\w .+-]/g, '');
		const body = text(log.text, 8 * 1024);
		if (file && body) kept.push({ file: scrub(file), text: scrub(body) });
	}
	return kept.length ? JSON.stringify(kept) : null;
}

// The report as the database keeps it, or a reason it is turned away
export function validate(report) {
	if (!report || typeof report !== 'object' || Array.isArray(report)) return { error: 'not an object' };
	if (report.schema !== 1) return { error: 'unknown schema' };
	const install = text(report.install, 32);
	if (!install || !/^[0-9a-f]{32}$/.test(install)) return { error: 'bad install id' };
	const titleId = text(report.titleId, 16);
	if (!titleId || !/^[0-9a-f]{16}$/.test(titleId)) return { error: 'bad title id' };
	if (!RESULTS.includes(report.result)) return { error: 'bad result' };
	const side = text(report.side, 24);
	if (!side || !/^[a-z0-9]+$/.test(side)) return { error: 'bad side' };
	const settings = {};
	if (report.settings && typeof report.settings === 'object')
		for (const [name, value] of Object.entries(report.settings).slice(0, 40))
			if (/^[A-Za-z0-9]{1,40}$/.test(name) && Number.isInteger(value)) settings[name] = value;
	const seconds = Number.isInteger(report.seconds) && report.seconds >= 0 && report.seconds < 86400 ? report.seconds : null;
	return {
		row: {
			install,
			app: text(report.app, 24) || 'unknown',
			commit_id: (text(report.commit, 16) || '').replace(/[^0-9a-f]/g, '') || 'unknown',
			side,
			emulator: (text(report.emulator, 32) || 'unknown').replace(/[^a-z0-9-]/g, ''),
			title_id: titleId,
			name: scrub(text(report.name, 80) || ''),
			result: report.result,
			error: report.error ? scrub(text(report.error, 300)) : null,
			seconds,
			firmware: /^\d{1,2}\.\d{2}$/.test(report.firmware) ? report.firmware : 'unknown',
			model: MODELS.includes(report.model) ? report.model : 'unknown',
			jit: report.jit === true ? 1 : 0,
			hen: report.hen === true ? 1 : 0,
			settings: JSON.stringify(settings),
			log: report.log ? scrub(text(report.log, 16 * 1024)) : null,
			emulator_logs: emulatorLogs(report.logs),
		},
	};
}

function median(values) {
	if (!values.length) return null;
	const sorted = [...values].sort((a, b) => a - b);
	return sorted[Math.floor(sorted.length / 2)];
}

// What works: one row a game, from the reports' rows (as the database gives them). It names no install
// ID and holds no log: it is what GitHub Pages shows (site/compatibility.html).
export function compatibility(rows, now) {
	const games = new Map();
	for (const row of rows) {
		const key = row.side + ':' + row.title_id;
		let game = games.get(key);
		if (!game) {
			game = { side: row.side, titleId: row.title_id, name: row.name, consoles: new Set(), sessions: 0, ended: 0, failed: 0, crashed: 0, closed: 0,
				minutes: [], firmware: new Set(), models: new Set(), errors: new Map(), apps: new Set(), lastSeen: 0 };
			games.set(key, game);
		}
		game.consoles.add(row.install);
		game.sessions++;
		game[row.result]++;
		if (row.result === 'ended' && row.seconds != null) game.minutes.push(Math.round(row.seconds / 60));
		if (row.firmware !== 'unknown') game.firmware.add(row.firmware);
		if (row.model !== 'unknown') game.models.add(row.model);
		if (row.error) game.errors.set(row.error, (game.errors.get(row.error) || 0) + 1);
		game.apps.add(row.app);
		if (row.received >= game.lastSeen) {
			game.lastSeen = row.received;
			if (row.name) game.name = row.name;
		}
	}
	const list = [...games.values()].map((g) => {
		const worked = g.ended / g.sessions;
		// works: four in five times or more, on however many consoles (the table says how many)
		const status = g.ended === 0 ? 'fails' : worked >= 0.8 ? 'works' : 'mixed';
		const error = [...g.errors.entries()].sort((a, b) => b[1] - a[1])[0];
		return {
			side: g.side, titleId: g.titleId, name: g.name, status, consoles: g.consoles.size, sessions: g.sessions,
			ended: g.ended, failed: g.failed, crashed: g.crashed, closed: g.closed, medianMinutes: median(g.minutes),
			firmware: [...g.firmware].sort(), models: [...g.models].sort(), apps: [...g.apps].sort(),
			commonError: error ? error[0] : null, lastSeen: new Date(g.lastSeen * 1000).toISOString().slice(0, 10),
		};
	});
	list.sort((a, b) => a.side.localeCompare(b.side) || a.name.localeCompare(b.name));
	return { schema: 1, generated: new Date(now * 1000).toISOString(), games: list };
}
