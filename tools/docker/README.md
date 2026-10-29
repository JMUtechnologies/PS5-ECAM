# ECAM Docker workstation

Ubuntu 24.04 / clang-18, based on the earlier Store workstation recipe and
ECAM's docs/BUILDING.md. Uses linux/amd64 for the existing x86-64 toolchain inputs
on Apple Silicon, with Meson 1.9.2 and CMake 3.31.10. The source checkout is mounted
at /src; build outputs persist
in that checkout. No privileged mode or Docker socket mount is needed inside it.

RADV's extraction and compilation work directory uses the persistent Docker
volume `ps5-ecam_radv-work` to avoid GNU tar directory identity errors on macOS
bind mounts. Existing host work files are hidden by the mount, not deleted.
Installed RADV archives remain in the source checkout. RADV work logs can be
read with `bash tools/docker/workstation.sh run bash` inside the container.
Do not remove the volume while preserving incremental RADV builds.
The Ninja wrapper also applies JOBS to RADV's direct Ninja invocations.

From the repository root:

```sh
bash tools/docker/workstation.sh image
bash tools/docker/workstation.sh doctor
bash tools/docker/workstation.sh run make deps
bash tools/docker/workstation.sh run make test-retro
bash tools/docker/workstation.sh run make build
```

Additional runtimes require their documented donor toolchain preparation before
`make retro`, `make retro-upgrades`, or `make gameboy` (Game Boy branch only).
See docs/RETRO-INTEGRATION.md and docs/GAMEBOY-INTEGRATION.md where present.
Packaging Eden also requires its pinned OpenGL SDK archive; see tools/build-eden.sh.
The workflows build on native x86-64 Linux. Mac x86 emulation failed the
executable-alias diagnostic, so it cannot qualify Eden's JIT checks. Native
compilation does not establish PS5 hardware compatibility.

Live status (requires the authenticated GitHub CLI):

```sh
python3 tools/docker/status.py
python3 tools/docker/status.py --watch
```

The viewer refreshes every 30 seconds; Ctrl+C stops only the viewer. It does not
cancel builds. It reports completed jobs, not an estimated percentage of time.

`verify-staged-runtimes.py` checks pinned source receipts, bridge hashes, notices,
and executable headers. `package-qualified.sh` refuses incomplete staging and
creates a test ZIP with checksum and an explicit hardware-unvalidated receipt.
It requires the base native build and its prepared SDK/link inputs as well as
all seven runtime folders. It never installs the test app.

The host must allow the Docker CLI to access Docker Desktop's daemon. Creating
this project cannot grant host session socket permission. This setup never
uploads to the PS5, installs packages on it, or publishes a release.
