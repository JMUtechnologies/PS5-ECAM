# The report intake

Where Playtendoh's reports go (`docs/REPORTS.md` says what a report holds). A Cloudflare Worker with a
D1 database:

- `POST /v1/report`: one report from the app. Checked against schema 1 (`src/report.js`), limited to
  30 an hour per install ID and 60 per network address, scrubbed again, and kept in D1. The address
  is only the rate limiter's key; nothing about the sender is stored.
- `GET /v1/compatibility.json`: one row a game, no install ID, no log. GitHub Pages fetches it every
  day (`.github/workflows/pages.yml`) and shows it at `compatibility.html`.
- Once a day, reports older than 180 days are deleted.

The app holds no secret: only the intake's address, given at build time. The Worker holds none
either. Nothing here writes to GitHub.

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

1. **The app:** build with that address: `make release PLAYTENDOH_REPORT_URL=https://playtendoh-reports.<account>.workers.dev/v1/report`
   (or the same as an environment variable). Without it the app asks nothing and sends nothing.
2. **The site:** on GitHub, Settings > Secrets and variables > Actions > Variables, add `REPORTS_URL`
   = `https://playtendoh-reports.<account>.workers.dev` (no path). The Pages workflow then fetches the
   table every day, or at once from Actions > Pages > Run workflow.

## Reading the reports

The table on the site covers most questions. For the rest, SQL on the private database:

```bash
npx wrangler d1 execute playtendoh-reports --remote --command \
  "SELECT name, result, firmware, model, error FROM reports WHERE result != 'ended' ORDER BY received DESC LIMIT 50"
npx wrangler d1 execute playtendoh-reports --remote --command \
  "SELECT log FROM reports WHERE title_id = '0100f2c0115b6000' AND result = 'crashed' ORDER BY received DESC LIMIT 1"
```

## Cost

Free up to Cloudflare's free plan's limits: 100,000 requests a day, 5 GB of D1 (a report is about
2 KB, so some 2.5 million reports) and 5 million D1 rows read a day. The daily table reads every
row once and is cached for an hour. Past those limits, Workers Paid is 5 USD a month.

## Tests

`node test.mjs` (also run by `tools/tests/test-reports.sh`): what is taken and turned away, the
second scrub, and that the published table has no install ID and no log.
