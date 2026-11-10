// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: reports to the project, for players who said yes (docs/REPORTS.md says exactly what is
// sent). As the launcher starts a game it notes the session (reports/current.json); the next time the
// launcher is up, in this process or after the app started over, that becomes one finished report in
// reports/outbox/ with how the game ended, and the sender (report_send.cpp) posts the outbox while the
// launcher is idle. Nothing here runs beside a game, and nothing is written at all while reports are off.
// Everything a report holds is a fixed field or text that went through Scrub.
//
// This file and reports.cpp are the part with no network and no console call in it, which
// tools/tests/test-reports.sh runs on a PC.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The intake's address (tools/log-intake), given to the build as PLAYTENDOH_REPORT_URL. None: the app
// asks nothing, keeps nothing and sends nothing.
#ifndef PLAYTENDOH_REPORT_URL
#define PLAYTENDOH_REPORT_URL ""
#endif

namespace ps5reports
{
	constexpr const char* kUrl = PLAYTENDOH_REPORT_URL;
	constexpr bool Available() { return kUrl[0] != 0; }
	// docs/REPORTS.md, field by field what a report holds: the question and Settings point to it
	constexpr const char* kExplained = "https://github.com/premohq/PlayTendoh/blob/HEAD/docs/REPORTS.md";
	constexpr int kSchema = 1;
	// the outbox: at most this many reports, this many bytes and this old; the oldest go first
	constexpr int kOutboxFiles = 20;
	constexpr uint64_t kOutboxBytes = 512 * 1024;
	constexpr int64_t kOutboxSeconds = 14 * 24 * 3600;
	// a report's log: the boot log's last lines
	constexpr size_t kLogLines = 120, kLogBytes = 16 * 1024;
	constexpr size_t kNameLength = 80, kErrorLength = 300;

	// What the launcher knows as a game starts
	struct Session
	{
		std::string side;	   // "switch", as ps5cemu.json names it
		std::string emulator;  // "eden"
		uint64_t titleId = 0;
		std::string name;	   // the catalogue's
		int64_t started = 0;   // seconds since 1970
		std::vector<std::pair<std::string, int>> settings; // the game's own, by their ps5cemu.json names
	};
	// The app and the console it runs on
	struct Console
	{
		std::string app, commit; // "6.0.0", "76f7fd0"
		std::string firmware;	 // "11.60"
		std::string model;		 // ModelClass's
		bool jit = false, hen = false; // JIT memory and /data granted
	};
	// What the launcher finds when it is up again
	struct Outcome
	{
		std::string crash;	 // the session's first [crash] line, or empty
		std::string error;	 // why the game did not start or stopped, or empty
		std::string logPath; // that session's boot log
	};

	// text with everything personal taken out: `secrets` (the values the app knows: user names, tokens,
	// share logins) wherever they are, whatever their case; IPv4, IPv6 and MAC addresses; e-mail
	// addresses; URLs; every path cut to "<path>/file.ext"; what follows user=, token:, password=, ssid,
	// serial, nickname, room and the like; hex runs of 32 or more. It errs on the side of removing.
	std::string Scrub(std::string_view text, const std::vector<std::string>& secrets = {});
	// "base", "slim", "pro" or "unknown", from the console's model name ("CFI-7016B")
	std::string ModelClass(std::string_view model);
	// "11.60" from "11.60 (0x11600000)" or " 11.600.000"; "unknown" when there is no number in it
	std::string FirmwareShort(std::string_view firmware);

	// Where the reports are kept (the data folder's reports/), and the values Scrub removes by name
	void SetFolder(const std::string& folder);
	void SetSecrets(std::vector<std::string> secrets);
	// The player's answer. On: an install ID is made when there is none (32 random hex digits, nothing of
	// the console's). Off: the ID, the session noted and every report waiting are deleted, now.
	void SetEnabled(bool on);
	bool Enabled();
	std::string InstallId(); // empty while off

	// The app and the console, once a process (main_ps5.cpp)
	void SetConsole(const Console& console);
	// A game starts: noted, when reports are on
	void Begin(const Session& session);
	// It ended by its own menu (when: seconds since 1970)
	void Ended(int64_t when);
	// The launcher is up again: the session noted, if any, becomes a report in the outbox ("ended",
	// "failed" with outcome.error, "crashed" with outcome.crash, or "closed" when nothing says how it
	// ended). True when one was written.
	bool Close(const Outcome& outcome, int64_t now);

	// The outbox, oldest first, after dropping what is over its limits
	std::vector<std::string> Pending(int64_t now);
	std::string Read(const std::string& path);
	void Remove(const std::string& path);

	// The sender (report_send.cpp): the outbox posted to kUrl a few seconds after the launcher is up, on a
	// thread of its own; Stop before a game, as ps5update::Stop. A report the intake turns away (400, 413)
	// is deleted; one it cannot take now stays for the next time.
	void StartSending();
	void StopSending();
}
