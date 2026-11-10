# Switch 2 on Lichii's newest core: the port in progress

Not used by the build yet: `tools/build-eglantine.sh` still applies `patches/eglantine` to EglantinePS5
at the pin in `tools/deps.json` (Lichii from 1 November, the proven core).

These patches go on heydemoura/EglantinePS5 at `c8a5c2a` (`git am`), and move its `Eglantine` submodule to
zortos293/Lichii `nx2` at `d2eaae4` (9 November 2026). Where they stand:

- `tools/prepare-vulkan-port.py`: all 69 Vulkan anchors fit Lichii; where Lichii now does the same itself,
  Lichii's is kept (its compute read barrier, draw submission policy with the PS5's 512-draw cap,
  scratch-buffer release, 16 MiB VMA blocks, pipeline cache writes).
- `headless/*.cmake` (the configure-time edits): all 25 that had moved are handled, and the core configures
  on Lichii. Where Lichii does the job itself, its own is kept: hardened network sockets, the idle core spin,
  mod-root guards, the GPU fence wait and shutdown, the graphics pipeline without a build lock. Fitted: the
  tracked-write manager, the in-place code decoding, the front macro callback. Left out for now (speed-ups
  only): the command-word batching.
- The core fixes: 4 of the port's 59 apply on Lichii; one is in Lichii already; the other 54 are in
  `tools/eglantine-patches-pending/` to port one by one, the PS5 memory work first.
- Then: compile, the port's own checks, and the pin in `tools/deps.json` with `patches/eglantine` remade.
