-- Playtendoh's reports (README.md): npx wrangler d1 execute playtendoh-reports --remote --file schema.sql
CREATE TABLE IF NOT EXISTS reports (
	id INTEGER PRIMARY KEY,
	received INTEGER NOT NULL,   -- seconds since 1970, the intake's clock
	install TEXT NOT NULL,       -- the app's random install ID: counts consoles, names none
	app TEXT NOT NULL,
	commit_id TEXT NOT NULL,
	side TEXT NOT NULL,
	emulator TEXT NOT NULL,
	title_id TEXT NOT NULL,
	name TEXT NOT NULL,
	result TEXT NOT NULL,        -- ended, failed, crashed, closed
	error TEXT,
	seconds INTEGER,
	firmware TEXT NOT NULL,
	model TEXT NOT NULL,         -- base, slim, pro, unknown
	jit INTEGER NOT NULL,
	hen INTEGER NOT NULL,
	settings TEXT NOT NULL,      -- JSON: {"name": number}
	log TEXT,                    -- the boot log's last lines
	emulator_logs TEXT           -- JSON: [{"file": "core.log", "text": its last lines}], the emulator's own
);
CREATE INDEX IF NOT EXISTS reports_game ON reports (side, title_id);
CREATE INDEX IF NOT EXISTS reports_received ON reports (received);
