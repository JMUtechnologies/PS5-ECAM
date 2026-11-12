# The report intake

Where Playtendoh's reports go (`docs/REPORTS.md` says what a report holds). A Cloudflare Worker with a
D1 database:

- `POST /v1/report`: one report from the app (64 KB at most). Checked against schema 1 (`src/report.js`), limited to
  20 a minute per install ID and 30 per network address, scrubbed again, and kept in D1. The address
  is only the rate limiter's key; nothing about the sender is stored.
- `GET /v1/compatibility.json`: one row a game, no install ID, no log, rebuilt at most once an hour (Cloudflare's
  cache). `compatibility.html` reads it live; GitHub Pages also bakes a copy in every hour (`.github/workflows/pages.yml`)
  for when the intake can't be reached.
- Once a day, reports older than 180 days are deleted.
- Every hour, the site is built again (the Pages workflow started on `Playtendoh`), so the console's
  `status.json` and the table baked into the site are never more than an hour old. GitHub's own hourly
  schedule in `pages.yml` stays, but GitHub runs it only every few hours.

The app holds no secret: only the intake's address, given at build time. The Worker holds one, if you
give it: `PAGES_TOKEN`, which can do nothing but start this repository's workflows.

## Setting it up (about ten minutes, free)

Needs Node.js 18 or newer and a free Cloudflare account.

```bash
cd tools/log-intake
npx wrangler login
npx wrangler d1 create playtendoh-reports
```

Put the `database_id` it prints into `wrangler.toml`, in place of `REPLACE_WITH_THE_ID_WRANGLER_D1_CREATE_PRINTS`
(that file has no secret in it, so it can be committed), then:

```bash
npx wrangler d1 execute playtendoh-reports --remote --file schema.sql
npx wrangler deploy
```

`deploy` prints the Worker's address, for example `https://playtendoh-reports.<account>.workers.dev`.
Then:

1. **The app:** `tools/build-cemu.sh` builds this intake's address in
   (`https://playtendoh-reports.pixelnet321.workers.dev/v1/report`). Another intake: `make release
   PLAYTENDOH_REPORT_URL=https://.../v1/report`; none: `PLAYTENDOH_REPORT_URL=` (empty), and the app
   asks nothing and sends nothing.
2. **The site:** on GitHub, Settings > Secrets and variables > Actions > Variables, add `REPORTS_URL`
   = `https://playtendoh-reports.<account>.workers.dev` (no path). The Pages workflow then fetches the
   table every hour, or at once from Actions > Pages > Run workflow.
3. **Every hour, for sure:** on GitHub, Settings > Developer settings > Fine-grained tokens, make one for
   `premohq/PlayTendoh` alone with Actions: Read and write, then `npx wrangler secret put PAGES_TOKEN`
   (paste it) and `npx wrangler deploy`. Without it the intake starts nothing, and the site waits for
   GitHub's schedule.

## Reading the reports

The table on the site covers most questions. For the rest, SQL on the private database (from
`tools/log-intake`). What fails, grouped by game and by why, the most frequent first:

```bash
npx wrangler d1 execute playtendoh-reports --remote --command \
  "SELECT side, title_id, name, emulator, result, substr(error, 1, 100) AS why, COUNT(*) AS times, COUNT(DISTINCT install) AS consoles,
   datetime(MAX(received), 'unixepoch') AS last FROM reports WHERE result != 'ended' GROUP BY side, title_id, result, why ORDER BY times DESC LIMIT 50"
```

A game's latest crash, with the boot log and the emulator's own logs (`emulator_logs`: Cemu's,
Azahar's, Eden's, Lichii's, Porpoise's or a system program's, as JSON):

```bash
npx wrangler d1 execute playtendoh-reports --remote --json --command \
  "SELECT received, app, commit_id, emulator, firmware, model, error, log, emulator_logs FROM reports
   WHERE title_id = '0100f2c0115b6000' AND result = 'crashed' ORDER BY received DESC LIMIT 1"
```

Everything from one release, to see what it broke: `... WHERE app = '6.0.0' ...`.

## Cost

Free up to Cloudflare's free plan's limits: 100,000 requests a day, 5 GB of D1 (a report is 2 to 40 KB with
its logs, so some 200,000 reports at the most) and 5 million D1 rows read a day. The table, made every hour, reads
every row once: 24 reads of every row a day, under the limit up to about 200,000 reports. Past those limits, Workers Paid is 5 USD a month.

## Tests

`node test.mjs` (also run by `tools/tests/test-reports.sh`, which gives it a report the app's own code
wrote): what is taken and turned away, the second scrub, that the published table has no install ID
and no log, and the Worker itself answering as the app expects (201, 400, 413, 415, 429) over a
stand-in for D1.
