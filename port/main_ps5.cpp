// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the entry point (Cemu's src/main.cpp on the desktop).
//
//  1. out of the sandbox: /data, and JIT memory for the recompilers (ps5/privilege.h);
//  2. the boot log, the DualSense and Cemu's core (settings, MLC, graphic packs, the game scan);
//  3. the launcher, its start screen and Cemu's and Azahar's sides, until a game is chosen;
//  4. the game, on Cemu's or Azahar's Vulkan renderer, until the in-game menu (touchpad + Options)
//     asks for the library, which starts PS5CEMU-HAR over (app/emulator.h, RestartToLibrary) on
//     that emulator's side.

#include "app/emulator.h"
#include "app/pack_updates.h"
#include "app/paths.h"
#include "app/updates.h"
#include "azahar/azahar.h"
#include "azahar/library.h"
#include "frontend/launcher.h"
#include "frontend/settings.h"
#include "ps5/crash.h"
#include "ps5/display.h"
#include "ps5/driver_clock.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"
#include "ps5/profiler.h"
#include "ps5/threads.h"
#include "ps5/window.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

// Cemu's recompiler: the exact stfs conversion, or cvtsd2ss (Cemu patch 0021)
extern bool g_ps5AccurateSingleStores;
// patches/cemu: the GPU's time on Cemu's command buffers (VulkanRenderer.cpp), for profiling runs
extern bool g_ps5GpuTimestamps;
// patches/cemu: Cemu's samplers without custom border colours (VulkanRenderer.cpp)
extern bool g_ps5StandardBorderColors;
// patches/cemu: Cemu's millisecond clock on the TSC (Common/unix/platform.cpp)
void PS5Cemu_UseTscTickCount(bool enabled);
// patches/cemu: an idle emulated core's spin before it sleeps, in PAUSEs (coreinit_Thread.cpp)
namespace coreinit
{
	extern uint32_t g_ps5IdleSpinIterations;
}

// The PS4 SDK's version record: its size is set by the caller (libkernel)
struct SceKernelSwVersion
{
	size_t size;
	char text[0x1c];
	uint32_t version;
};
extern "C" int32_t sceKernelGetSystemSwVersion(SceKernelSwVersion* version);

namespace
{
	// The console's firmware, as its settings show it ("11.60"), for the boot log and Diagnostics:
	// most reports hinge on it
	std::string Firmware()
	{
		SceKernelSwVersion version{};
		version.size = sizeof(version);
		if (sceKernelGetSystemSwVersion(&version) != 0)
			return "unknown";
		version.text[sizeof(version.text) - 1] = 0;
		return fmt::format("{} ({:#010x})", version.text, version.version);
	}

	std::vector<std::string> Diagnostics(const ps5privilege::Result& privileges)
	{
		return {
			fmt::format("PS5CEMU-HAR {}: Cemu at {}, Azahar at {}", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT, PS5CEMU_AZAHAR_COMMIT),
			fmt::format("Firmware {}", Firmware()),
			privileges.summary,
			ps5log::Path()[0] ? fmt::format("Logs: {}, {}/log.txt", ps5log::Path(), ps5paths::kRoot) : "Boot log not written (/data is unreachable)",
		};
	}

	ps5emu::Options Options(const ps5settings::Launcher& settings)
	{
		return {settings.gamesFolder, settings.overlay, settings.volume, settings.upscaleFilter, settings.asyncShaders, settings.gamePadSpeaker};
	}

	// The side the launcher opens on when the app starts over (and only then), and why the game did
	// not start, when there is a reason.
	void RememberSide(const char* side, const std::string& launchError = {})
	{
		// the game's menu may have saved settings since the launcher's were read
		ps5settings::Launcher settings = ps5settings::Load();
		settings.side = side;
		settings.launchError = launchError;
		ps5settings::Save(settings);
	}
}

// What Cemu's src/main.cpp defines for the rest of Cemu.
std::atomic_bool g_isGPUInitFinished = false;
// Cemu's command line asks for a console window with some options; there is none on the PS5.
void requireConsole() {}

int main(int argc, char* argv[])
{
	ps5log::Line("[main] PS5CEMU-HAR {} starting (Cemu at {}, Azahar at {})", PS5CEMU_VERSION, PS5CEMU_CEMU_COMMIT, PS5CEMU_AZAHAR_COMMIT);
	// before any thread starts: the HEN jailbreaks the process as it is
	const ps5privilege::Result privileges = ps5privilege::Acquire();
	if (privileges.filesystem)
	{
		ps5log::Open(ps5paths::kLogs);
		// what the libraries print on stderr, the Vulkan driver's own reports among it (its submissions'
		// timing every ten seconds, a stalled GPU), kept as boot.log is, the session before alongside
		const std::string driverLog = std::string(ps5paths::kLogs) + "/driver.log";
		std::rename(driverLog.c_str(), (std::string(ps5paths::kLogs) + "/driver.prev.log").c_str());
		if (std::freopen(driverLog.c_str(), "w", stderr))
			ps5log::BufferStderr();
	}
	ps5log::Line("[main] firmware {}", Firmware());
	ps5log::Line("[main] {}", privileges.summary);
	{
		bool image = false;
		const std::string source = ps5paths::MountSource(&image);
		ps5log::Line("[main] app folder {}{}", ps5paths::AppDir(),
			!source.empty() ? fmt::format(" (mounted from {})", source) : image ? " (mounted from an image)" : "");
	}
	ps5crash::Install(); // Cemu's own replaces it, in a session Cemu runs in
	{
		// how the console starts the CPU's floating point: desktop systems keep denormals (0x1f80);
		// flush-to-zero (bit 15) or denormals-are-zero (bit 6) would make Cemu's and Azahar's float
		// results differ from a desktop's
		uint32_t mxcsr = 0;
		asm volatile("stmxcsr %0" : "=m"(mxcsr));
		ps5log::Line("[main] MXCSR {:#06x}: flush-to-zero {}, denormals-are-zero {}", mxcsr, mxcsr & 0x8000 ? "on" : "off",
			mxcsr & 0x40 ? "on" : "off");
	}
	ps5threads::Initialize(); // before any thread starts: they inherit the main thread's CPUs

	ps5settings::Launcher settings = ps5settings::Load();
	// the update notice, on a fresh start only (not each time a game hands back to the library)
	const bool freshStart = settings.side.empty();
	// still before any thread: the game folders and drives the HEN may have left out (#16)
	if (privileges.filesystem)
		ps5privilege::ReachFolders({settings.gamesFolder, settings.n3ds.gamesFolder});
	// the first thread but main: the boot log's writer, after the elevation helper is done with the
	// process (it must find it single-threaded)
	ps5log::StartWriter();
	ps5threads::SetPinning(settings.pinCpuThreads);
	ps5threads::SetCompileBelowNormal(settings.compileBelowNormal);
	// before a game starts: about 20 ns a PAUSE on the console's CPU
	coreinit::g_ps5IdleSpinIterations = (uint32_t)settings.idleSpinUs * 50;
	if (settings.idleSpinUs)
		ps5log::Line("[threads] an idle emulated core spins {} us before it sleeps (idleSpinUs in ps5cemu.json)", settings.idleSpinUs);
	if (settings.profile && privileges.filesystem)
		ps5profile::Start();
	// before Cemu's renderer starts: with the profiler, the [perf] lines say how busy the GPU is too
	g_ps5GpuTimestamps = settings.profile;
	g_ps5StandardBorderColors = settings.standardBorderColors;
	if (settings.standardBorderColors)
		ps5log::Line("[vulkan] Cemu's samplers: standard border colours only (standardBorderColors in ps5cemu.json)");
	// before Cemu starts: its millisecond clock, taken at every draw call
	PS5Cemu_UseTscTickCount(settings.tscTickCount);
	ps5log::Line("[clock] Cemu's millisecond clock: {} (tscTickCount in ps5cemu.json)", settings.tscTickCount ? "the TSC" : "clock_gettime");
	// before either emulator's Vulkan driver starts: the driver's clock without a system call a read
	// (driverClockTsc in ps5cemu.json, an experiment)
	if (settings.driverClockTsc)
		ps5clock::UseTscForDriver();
	else
		ps5log::Line("[vulkan] the driver's clock is clock_gettime (driverClockTsc in ps5cemu.json)");
	// before Cemu recompiles any of a game's code
	g_ps5AccurateSingleStores = settings.accurateSingleStores;
	if (settings.accurateSingleStores)
		ps5log::Line("[emu] single-precision stores: Cemu's exact conversion (accurateSingleStores in ps5cemu.json)");
	// before either emulator's Vulkan driver starts, which reads them once. RADV keeps its shader
	// cache in /app0 unless told otherwise, and a jailbroken process has none: told here, so the 3DS's
	// side has it too (it was set as Cemu started, which a 3DS game's process never does)
	setenv("MESA_SHADER_CACHE_DIR", ps5paths::kRadvCache, 1);
	// The driver keeps the shaders it compiles for the next start, as on a PC (shaderDiskCache in
	// ps5cemu.json, on by default). Mesa keeps no disk cache in a process whose effective user or
	// group is not its real one, and the HEN makes only the effective ones root: the real ones are
	// made the effective ones.
	if (!settings.shaderDiskCache)
		setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
	else if (geteuid() != getuid() || getegid() != getgid())
	{
		const int user = setreuid(geteuid(), geteuid()), group = setregid(getegid(), getegid());
		ps5log::Line("[vulkan] RADV's shader cache: real user {} (euid {}){}, real group {} (egid {}){}", getuid(), geteuid(),
			user ? " (refused)" : "", getgid(), getegid(), group ? " (refused)" : "");
	}
	ps5log::Line("[vulkan] RADV's shader cache in {}: {}", ps5paths::kRadvCache,
		!settings.shaderDiskCache ? "off (shaderDiskCache in ps5cemu.json)" :
		geteuid() == getuid() && getegid() == getgid() ? "on" : "off (the console kept the real user)");
	if (!settings.radvDebug.empty())
	{
		setenv("RADV_DEBUG", settings.radvDebug.c_str(), 1);
		ps5log::Line("[vulkan] RADV_DEBUG={} (radvDebug in ps5cemu.json)", settings.radvDebug);
	}
	ps5pad::Init();
	ps5pad::SetVibrationEnabled(settings.rumble);
	ps5window::Initialize();

	ps5launcher::Status status;
	status.diagnostics = Diagnostics(ps5privilege::Current());
	std::string error;
	if (!privileges.filesystem)
	{
		status.notice = status.notice3ds =
			"PS5CEMU-HAR cannot reach /data. Load a HEN with PPSA99360 in its app jailbreak list, or elfldr, then restart PS5CEMU-HAR.";
		ps5log::Line("[main] {}", status.notice);
		ps5notify::Send(status.notice);
	}
	else if (!settings.launchError.empty())
	{
		// the last game failed after its renderer started, and the process was started over to show it
		(settings.side == "3ds" ? status.notice3ds : status.notice) = "The game could not start: " + settings.launchError;
		settings.launchError.clear();
		ps5settings::Save(settings);
	}

	// One emulator per session: neither starts until the start screen's choice (or the side the last
	// game was on), and leaving that side starts the app over. Cemu's core (its guest memory, system
	// threads, crash handler, graphic packs and game scan) runs only for the Wii U; Azahar (its game
	// scan, and its core once a game starts) only for the 3DS.
	// Each time the launcher moves to a side (a game that did not start brings the launcher back).
	// Either side gives way to the other in the same process: Azahar's core and Cemu's emulated
	// Wii U both start only for a game, so until then only their game lists and settings are loaded.
	std::optional<ps5launcher::System> started;
	const size_t sessionLine = status.diagnostics.size();
	auto prepare = [&](ps5launcher::System system) {
		if (started == system)
			return;
		started = system;
		status.diagnostics.resize(sessionLine);
		if (system == ps5launcher::System::N3ds)
		{
			ps5log::Line("[main] the 3DS side: Cemu's emulated Wii U is not started");
			status.diagnostics.push_back("This session: Azahar (3DS); Cemu's emulated Wii U is not started");
			ps5azahar::StartScan(settings.n3ds.gamesFolder);
			ps5emu::LogMemory(); // the 3DS side's start, against Cemu's
			return;
		}
		ps5log::Line("[main] the Wii U side: Azahar's core is not started");
		status.diagnostics.push_back("This session: Cemu (Wii U); Azahar's core is not started");
		if (!privileges.filesystem)
			return;
		std::string coreError;
		if (!ps5emu::InitializeCore(coreError))
		{
			status.notice = "Cemu did not start: " + coreError;
			ps5log::Line("[main] {}", status.notice);
			ps5notify::Send(status.notice);
			return;
		}
		status.coreReady = true;
		ps5emu::ApplyOptions(Options(settings));
		if (!privileges.jit)
			ps5notify::Send("No JIT memory: Wii U games run on the interpreter, much slower. Is PPSA99360 in your HEN's app jailbreak list?");
		ps5emu::LogMemory(); // Cemu's start, against the 3DS side's
	};

	if (freshStart && privileges.filesystem)
		ps5update::Start();
	for (;;)
	{
		ps5display::SetHighFrameRate(false); // the launcher at 59.94 Hz
		const auto choice = ps5launcher::Run(settings, status, prepare);
		ps5update::Stop(); // nothing of the launcher's runs beside a game
		ps5packs::Stop();
		if (!choice)
		{
			// nothing to show it on: wait for the player to close the app from the PS5's menu
			for (;;)
				sceKernelUsleep(1000000);
		}
		const ps5emu::Game& game = choice->game;

		if (choice->system == ps5launcher::System::N3ds)
		{
			if (ps5azahar::LaunchGame(game, settings.n3ds, error))
			{
				ps5azahar::RunGame();
				RememberSide("3ds");
				ps5emu::RestartToLibrary();
				return 0;
			}
			ps5log::Line("[main] {} did not start: {}", game.name, error);
			if (ps5azahar::CoreTouched())
			{
				// Azahar's core is half started: show why from a fresh process
				RememberSide("3ds", error);
				ps5emu::RestartToLibrary();
				return 1;
			}
			status.notice3ds = "The game could not start: " + error;
			settings.side = "3ds"; // the launcher shows why on Azahar's side
			continue;
		}

		ps5emu::ApplyOptions(Options(settings));
		ps5display::SetHighFrameRate(settings.highFrameRate);
		// a Wii U game only (its threads' names): before its emulated CPU cores and GPU thread start
		ps5threads::SetIsolation(settings.isolateCores);
		ps5window::Initialize();
		if (ps5emu::LaunchGame(game, error))
		{
			ps5emu::RunGame();
			RememberSide("wiiu");
			ps5emu::RestartToLibrary();
			return 0;
		}
		ps5log::Line("[main] {} did not start: {}", game.name, error);
		if (!ps5emu::RendererStarted())
		{
			status.notice = "The game could not start: " + error;
			settings.side = "wiiu";
			continue;
		}
		// Cemu's renderer holds VideoOut: show the reason from a fresh process
		RememberSide("wiiu", error);
		ps5emu::RestartToLibrary();
		return 1;
	}
}
