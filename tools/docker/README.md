# ECAM Docker workstation

Ubuntu 24.04 / clang-18, based on the earlier Store workstation recipe and
ECAM's docs/BUILDING.md. Uses linux/amd64 for the existing x86-64 toolchain inputs
and pins Meson 1.7.2 because Mesa requires at least 1.4.0.
on Apple Silicon. The source checkout is mounted at /src; build outputs persist
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
This image recipe is prepared but has not yet been built or qualified on PS5.

The host must allow the Docker CLI to access Docker Desktop's daemon. Creating
this project cannot grant host session socket permission. This setup never
uploads to the PS5, installs packages on it, or publishes a release.
