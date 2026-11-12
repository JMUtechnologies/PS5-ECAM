// SPDX-License-Identifier: GPL-3.0-or-later
// The intake's checks with node alone (tools/tests/test-reports.sh runs them): what is accepted and
// turned away, the second scrub, and the table, which must hold no install ID and no log; then the Worker
// itself (src/index.js) over a stand-in for D1, given a report the app wrote when there is one (argv).
import { readFileSync } from 'node:fs';
import { scrub, validate, compatibility } from './src/report.js';
import worker, { rebuildSite } from './src/index.js';

let failures = 0;
const check = (ok, what) => {
	console.log(`${ok ? 'PASS' : 'FAIL'}  intake: ${what}`);
	failures += ok ? 0 : 1;
};

const id = 'a'.repeat(32);
const good = {
	schema: 1, install: id, app: '6.0.0', commit: '76f7fd0', side: 'wiiu', emulator: 'cemu', titleId: '000500001010ec00', name: 'Mario Kart 8',
	started: 1762700000, firmware: '11.60', model: 'pro', jit: true, hen: true, settings: { frameGeneration: 1, 'bad name': 2, volume: 'x' },
	result: 'ended', seconds: 2520, log: '[net] 192.168.1.37 a4:5e:60:c1:9b:7d jordan@example.com /mnt/usb0/Jordan/Mario Kart 8.wua',
};

const { row } = validate(good);
check(row && row.result === 'ended' && row.seconds === 2520 && row.model === 'pro' && row.jit === 1, 'a good report is taken');
check(row && row.settings === '{"frameGeneration":1}', 'only number settings with plain names');
for (const item of ['192.168.1.37', 'a4:5e:60:c1:9b:7d', 'jordan@example.com', 'Jordan', '/mnt/usb0'])
	check(row && !row.log.includes(item), `the log has no "${item}"`);
check(row && row.log.includes('<path>/Mario Kart 8.wua'), "a path is cut to its file's name");
check(scrub('fe80::1c2d:3eff:fe4f:5a6b and 2a02:8108:1234:5678:9abc:def0:1234:5678') === '<ip> and <ip>', 'IPv6 addresses');
check(scrub('12:34:56 ps5log::Line 0100f2c0115b6000 30/60 fps') === '12:34:56 ps5log::Line 0100f2c0115b6000 30/60 fps', 'times, names, title IDs and 30/60 stay');
check(scrub('[driver] radv/ps5 submissions: 120 in 10 s, 30/60 fps') === '[driver] radv/ps5 submissions: 120 in 10 s, 30/60 fps', 'the driver line stays whole');
check(scrub('games in /mnt/usb0/Family Games/Okafor/Wii U') === 'games in <path>', 'a path with spaces goes whole');
check(scrub('copy of "C:\\Users\\Jordan\\Zelda.wua" made') === 'copy of "<path>/Zelda.wua" made', 'a Windows path is cut to its file');
check(scrub('[saves] /data/ps5cemu/user/80000001/userdata.dat written') === '[saves] <path>/userdata.dat written', "a save's path is cut to its file");

const withLogs = validate({ ...good, result: 'crashed', logs: [
	{ file: 'crash-20261110.txt', text: '[crash] SIGSEGV in the JIT, player Jordan at 10.0.0.9 /data/porpoise/debug/x.log' },
	{ file: '../../etc/passwd', text: 'x'.repeat(20000) },
	{ file: 'core.log', text: 'one' }, { file: 'fourth.log', text: 'never kept' }, 'not a log',
] }).row;
const kept = withLogs && JSON.parse(withLogs.emulator_logs);
check(kept && kept.length === 3 && kept[0].file === 'crash-20261110.txt' && !JSON.stringify(kept).includes('fourth'), "the emulator's logs: three at most");
check(kept && !kept[0].text.includes('10.0.0.9') && !kept[0].text.includes('/data/porpoise') && kept[0].text.includes('<path>/x.log'), 'and scrubbed again');
check(kept && !kept[1].file.includes('/') && kept[1].text.length === 8 * 1024, 'a file name without folders, a log cut to 8 KB');
check(validate(good).row.emulator_logs === null && validate({ ...good, logs: 'x' }).row.emulator_logs === null, 'none, or not a list: nothing kept');

for (const [what, bad] of [
	['another schema', { ...good, schema: 2 }],
	['a bad install ID', { ...good, install: 'Jordan' }],
	['a bad title ID', { ...good, titleId: '../etc' }],
	['an unknown result', { ...good, result: 'great' }],
	['a bad side', { ...good, side: 'Wii U<script>' }],
	['not an object', [1, 2]],
])
	check(!!validate(bad).error, `turned away: ${what}`);
check(validate({ ...good, firmware: 'Jordan', model: 'CFI-7016', seconds: 999999 }).row.firmware === 'unknown', 'odd fields become unknown');

const rows = [];
const add = (install, result, received, extra = {}) => rows.push({ ...validate({ ...good, install: install.repeat(32), result, ...extra }).row, received });
add('a', 'ended', 100);
add('b', 'ended', 200, { model: 'base', firmware: '9.00' });
add('c', 'ended', 300);
add('c', 'crashed', 400, { error: 'SIGSEGV in ps5emu::Run' });
add('a', 'failed', 500, { titleId: '0100f2c0115b6000', side: 'switch', name: 'Zelda', error: 'keys missing' });
const table = compatibility(rows, 1000);
const mk8 = table.games.find((g) => g.titleId === '000500001010ec00');
const zelda = table.games.find((g) => g.titleId === '0100f2c0115b6000');
check(mk8 && mk8.consoles === 3 && mk8.sessions === 4 && mk8.ended === 3 && mk8.crashed === 1 && mk8.status === 'mixed', 'the table counts consoles and endings');
check(mk8 && mk8.medianMinutes === 42 && mk8.models.join() === 'base,pro' && mk8.firmware.join() === '11.60,9.00', 'play time, models and firmware');
check(zelda && zelda.status === 'fails' && zelda.commonError === 'keys missing', 'a game that never ended: fails, with why');
const once = compatibility([{ ...validate({ ...good, install: 'e'.repeat(32), result: 'ended' }).row, received: 700 }], 1000).games[0];
check(once && once.status === 'works' && once.consoles === 1, 'a game that ended well on one console: works');
add('d', 'crashed', 600, { logs: [{ file: 'core.log', text: 'the emulator log' }] });
const published = JSON.stringify(compatibility(rows, 1000));
check(!published.includes('a'.repeat(32)) && !published.includes('"log"') && !published.includes('install') &&
	!published.includes('emulator log') && !published.includes('core.log'), "the table has no install ID and no log, the emulator's neither");

// -- the Worker, over a stand-in for D1 that keeps the rows INSERT gives it ------------------------------
const stored = [];
const env = {
	DB: {
		prepare(sql) {
			return {
				bind(...values) {
					return { async run() {
						const columns = sql.match(/\(([^)]*)\) VALUES/)[1].split(',').map((c) => c.trim());
						stored.push(Object.fromEntries(columns.map((c, i) => [c, values[i]])));
					} };
				},
				async all() { return { results: stored.map((r) => ({ ...r })) }; },
			};
		},
	},
	INSTALL_LIMITER: { async limit() { return { success: true }; } },
	ADDRESS_LIMITER: { async limit({ key }) { return { success: key !== '203.0.113.9' }; } },
};
globalThis.caches = { default: { async match() {}, async put() {} } };
const ctx = { waitUntil() {} };
const post = (body, headers = {}) => worker.fetch(new Request('https://intake.test/v1/report', {
	method: 'POST', body, headers: { 'content-type': 'application/json', ...headers } }), env, ctx);
const app = process.argv[2] ? readFileSync(process.argv[2], 'utf8') : JSON.stringify({ ...good, result: 'crashed', logs: [{ file: 'core.log', text: 'x' }] });
check((await post(app)).status === 201 && stored.length === 1, process.argv[2] ? 'Worker: the report the app wrote is taken' : 'Worker: a report is taken');
const saved = stored[0] || {};
check(typeof saved.received === 'number' && saved.title_id && !('logPlaces' in saved) && !('ip' in saved), 'Worker: kept with its time and nothing of the sender');
if (process.argv[2]) {
	const logs = JSON.parse(saved.emulator_logs || '[]');
	check(saved.result === 'crashed' && saved.emulator === 'porpoise' && logs.length === 3 && logs[0].file.startsWith('crash-'),
		"Worker: the app's crash and its emulator's three logs, crash first");
}
check((await post('{', {})).status === 400, 'Worker: not JSON: 400, the app drops it');
check((await post(JSON.stringify({ ...good, schema: 9 }))).status === 400, 'Worker: another schema: 400');
check((await post(app, { 'content-type': 'text/plain' })).status === 415, 'Worker: not JSON by its type: 415');
check((await post('x'.repeat(70 * 1024))).status === 413, 'Worker: over 64 KB: 413');
check((await post(app, { 'cf-connecting-ip': '203.0.113.9' })).status === 429, 'Worker: over the limit: 429, the app sends it later');
const served = await (await worker.fetch(new Request('https://intake.test/v1/compatibility.json'), env, ctx)).json();
const servedText = JSON.stringify(served);
check(served.games.length === 1 && served.games[0].crashed === 1 && !servedText.includes(saved.install) && !servedText.includes('number 1999') && !servedText.includes('<path>'),
	'Worker: the table, with no install ID and no log');
check((await worker.fetch(new Request('https://intake.test/v1/reports'), env, ctx)).status === 404, 'Worker: nothing else is served');


// -- every hour: the site built again, only with a token -------------------------------------------------
const asked = [];
const github = async (url, init) => { asked.push({ url, init }); return new Response(null, { status: 204 }); };
check((await rebuildSite({}, github)) === false && asked.length === 0, 'Worker: no token, no rebuild asked for');
check((await rebuildSite({ PAGES_TOKEN: 't' }, github)) === true && asked.length === 1
	&& asked[0].url === 'https://api.github.com/repos/premohq/PlayTendoh/actions/workflows/pages.yml/dispatches'
	&& JSON.parse(asked[0].init.body).ref === 'Playtendoh' && asked[0].init.headers.authorization === 'Bearer t',
	'Worker: with a token, the Pages workflow started on Playtendoh');
let deleted = 0;
const daily = { DB: { prepare(sql) { return { bind() { return { async run() { if (sql.startsWith('DELETE')) deleted++; } }; } }; } } };
await worker.scheduled({ cron: '17 3 * * *' }, daily, ctx);
await worker.scheduled({ cron: '41 * * * *' }, daily, ctx);
check(deleted === 1, 'Worker: old reports go once a day, not every hour');

console.log(failures ? 'intake: FAILED' : 'intake: all passed');
process.exit(failures ? 1 : 0);
