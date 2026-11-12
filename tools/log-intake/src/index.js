// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh's report intake, a Cloudflare Worker (README.md):
//   POST /v1/report               one report from the app (application/json, 64 KB at most): checked,
//                                 rate-limited, scrubbed again and kept in D1. 201, or 400 (dropped by the
//                                 app), 413, 415 or 429 (kept by the app and sent later).
//   GET  /v1/compatibility.json   the table GitHub Pages shows: one row a game, no install ID, no log.
// Nothing about the sender is kept: its network address is only the rate limiter's key.

import { MAX_BYTES, validate, compatibility } from './report.js';

const json = (body, status = 200, headers = {}) =>
	new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json', ...headers } });

async function take(request, env) {
	if (!(request.headers.get('content-type') || '').toLowerCase().startsWith('application/json')) return json({ error: 'json only' }, 415);
	const declared = Number(request.headers.get('content-length'));
	if (declared > MAX_BYTES) return json({ error: 'size' }, 413);
	const address = request.headers.get('cf-connecting-ip') || 'unknown';
	if (env.ADDRESS_LIMITER && !(await env.ADDRESS_LIMITER.limit({ key: address })).success) return json({ error: 'later' }, 429);
	const body = await request.text();
	if (!body || body.length > MAX_BYTES) return json({ error: 'size' }, 413);
	let report;
	try {
		report = JSON.parse(body);
	} catch {
		return json({ error: 'not json' }, 400);
	}
	const { row, error } = validate(report);
	if (error) return json({ error }, 400);
	if (env.INSTALL_LIMITER && !(await env.INSTALL_LIMITER.limit({ key: row.install })).success) return json({ error: 'later' }, 429);
	const columns = Object.keys(row);
	await env.DB.prepare(`INSERT INTO reports (received, ${columns.join(', ')}) VALUES (?, ${columns.map(() => '?').join(', ')})`)
		.bind(Math.floor(Date.now() / 1000), ...columns.map((c) => row[c]))
		.run();
	return json({ ok: true }, 201);
}

async function table(env, ctx, request) {
	const cache = caches.default;
	const hit = await cache.match(request);
	if (hit) return hit;
	const { results } = await env.DB.prepare(
		'SELECT received, install, app, side, title_id, name, result, error, seconds, firmware, model FROM reports'
	).all();
	const response = json(compatibility(results, Math.floor(Date.now() / 1000)), 200, {
		'cache-control': 'public, max-age=3600',
		'access-control-allow-origin': '*',
	});
	ctx.waitUntil(cache.put(request, response.clone()));
	return response;
}

const DAILY = '17 3 * * *';

// The Pages workflow started on the default branch, with PAGES_TOKEN (a fine-grained token for this repository
// alone, Actions: read and write; `npx wrangler secret put PAGES_TOKEN`). None: nothing is started.
export async function rebuildSite(env, fetcher = fetch) {
	if (!env.PAGES_TOKEN) return false;
	const repository = env.PAGES_REPOSITORY || 'premohq/PlayTendoh';
	const response = await fetcher(`https://api.github.com/repos/${repository}/actions/workflows/pages.yml/dispatches`, {
		method: 'POST',
		headers: {
			authorization: `Bearer ${env.PAGES_TOKEN}`,
			accept: 'application/vnd.github+json',
			'user-agent': 'playtendoh-reports',
			'x-github-api-version': '2022-11-28',
		},
		body: JSON.stringify({ ref: env.PAGES_BRANCH || 'Playtendoh' }),
	});
	if (response.status !== 204) console.log('pages dispatch', response.status);
	return response.status === 204;
}

export default {
	async fetch(request, env, ctx) {
		const { pathname } = new URL(request.url);
		try {
			if (pathname === '/v1/report' && request.method === 'POST') return await take(request, env);
			if (pathname === '/v1/compatibility.json' && request.method === 'GET') return await table(env, ctx, request);
			return json({ error: 'not found' }, 404);
		} catch (e) {
			console.log('intake error', String((e && e.message) || e).slice(0, 200));
			return json({ error: 'intake error' }, 500);
		}
	},
	// once a day (wrangler.toml's first cron): reports over 180 days old go. Every hour (its second): the site
	// built again, as GitHub runs the Pages workflow's own hourly schedule only every few hours
	async scheduled(event, env, ctx) {
		if (event.cron === DAILY) {
			await env.DB.prepare('DELETE FROM reports WHERE received < ?').bind(Math.floor(Date.now() / 1000) - 180 * 86400).run();
			return;
		}
		await rebuildSite(env);
	},
};
