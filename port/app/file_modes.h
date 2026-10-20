// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: /data/ps5cemu open to every user. The app runs as root when the HEN jailbreaks it and
// as its own user when it needs no HEN (ps5/privilege.h), and the console's FTP service and the file
// managers run as other users still. A folder one of them made 0755, or a file 0644, was out of the
// others' reach: a game whose saves and shader cache could not be written started to a black screen
// (#55), and installed games could not be deleted over FTP (#58). Folders are 0777 and files 0666,
// as the homebrew a file manager puts in /data/homebrew has them, and as RADV keeps its cache:
//  - Start gives every file and folder there those modes, where the app may change them, at each
//    start: what the app, Cemu, Azahar and melonDS made since (the process's umask cannot be set: on
//    the PS5 umask is a raw system call in the app's own code, which ends it), and what an earlier
//    build or a session as another user made. Only entries whose mode differs are changed. Azahar's
//    folders are made so already (patches/azahar 0028). The sides' game folders are the player's
//    files, which the app only reads: left as they are.
//  - Such entries are root's, mostly: a session the elevation helper or the HEN made root (a refused
//    game folder, /data out of reach) made them 0755 and 0644, and a session that needed neither
//    could not write them (#55, #56). A fresh start then asks the elevation helper for root
//    (ps5privilege::ReachClosedFiles), so its walk opens them.

#pragma once

namespace ps5filemodes
{
	// Whether the last walk left files or folders another user made, which the app as root may open
	bool ClosedLeft();
	// On a thread of its own; nothing when no walk is due
	void Start();
	// Before a game: a walk part-way through stops, and the next start goes on with it
	void Stop();
}
