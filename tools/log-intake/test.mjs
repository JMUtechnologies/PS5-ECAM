// SPDX-License-Identifier: GPL-3.0-or-later
// The intake's checks with node alone (tools/tests/test-reports.sh runs them): what is accepted and
// turned away, the second scrub, and the table, which must hold no install ID and no log.
import { scrub, validate, compatibility } from './src/report.js';

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
const published = JSON.stringify(table);
check(!published.includes('a'.repeat(32)) && !published.includes('"log"') && !published.includes('install'), 'the table has no install ID and no log');

console.log(failures ? 'intake: FAILED' : 'intake: all passed');
process.exit(failures ? 1 : 0);
