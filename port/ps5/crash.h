// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the app's own crash report, for a session Cemu does not run in (the start screen,
// and Azahar's side). It writes [crash] lines to the boot log as Cemu's does (patches/cemu): the
// signal, where it struck, the machine context and the code addresses on the crashed thread's
// stack, which give the functions with the ELF's symbols (address - load address). When Cemu
// starts, its own handler (Common/ExceptionHandler) takes over: it knows the emulated CPU's state.

#pragma once

namespace ps5crash
{
	// At start, once the boot log is open.
	void Install();
}
