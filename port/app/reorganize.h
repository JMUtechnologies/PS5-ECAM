// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: an install from before given a folder for each system, and put back (the prompt at start,
// Settings > Game files > A folder for each system).
//
// Each system's files are renamed into its own folder beside the launcher's (/data/cemu, /data/azahar,
// /data/melonds, /data/eden: ps5paths, app/system_layout.h). On the same drive a rename moves a folder
// whatever its size, so nothing is copied and no room is needed. The launcher only asks (Ask): the moves
// are made as the app next starts (Run, from main, before a system's folder is decided or a file of
// theirs opened), and the start after a power cut finishes them, as reorganize.txt still says what was
// under way. A system whose move fails is put back as it was, and stays where it was. Each system moved
// gets ecam_layout's marker, which makes the app and Eden's program take its new folder; put back, it
// loses it. Nothing is ever deleted.

#pragma once

#include <string>
#include <vector>

namespace ps5reorganize
{
	enum class Request
	{
		None,
		Separate, // into a folder each
		PutBack,  // back where versions before kept them (Go back needs them there)
	};

	// What can be done now: the systems still in the old layout with files in it, those in their own
	// folders, and, when the ones to separate cannot all move, the path in the way
	struct Layout
	{
		std::vector<std::string> separate, separated; // "cemu", "azahar", "melonds", "eden"
		std::string blocked;	 // a file already where one would go, or on another drive than its folder
		bool otherDrive = false; // blocked for that second reason
	};
	// root: the launcher's data folder (ps5paths::Root()); console: /data/ps5cemu, where the Switch's
	// files were whatever root is (Eden's program keeps them in /data)
	Layout Look(const std::string& root, const std::string& console);

	// The request, written down for the next start (false when it could not be)
	bool Ask(const std::string& root, Request request);
	Request Asked(const std::string& root);

	// What a start did: the systems moved, and those that could not be (put back as they were)
	struct Outcome
	{
		Request request = Request::None;
		std::vector<std::string> moved, failed;
	};
	// Does what was asked, and what a power cut left unfinished: the boot log's lines in log
	Outcome Run(const std::string& root, const std::string& console, std::vector<std::string>& log);
}
