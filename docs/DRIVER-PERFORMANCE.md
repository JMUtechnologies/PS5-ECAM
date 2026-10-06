# The Vulkan driver and Breath of the Wild at 4K60 and 8K30

This is a read-through of the Vulkan driver PS5CEMU-HAR links (Mihawk-99's RADV port: the
[PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) fork at `7b59ef27`, built with
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s recipe at `fde9e37`), of how Cemu drives it,
and of what could make Breath of the Wild run at 4K and 60 fps, and at 8K and 30. It ends with the
settings for both, changes ranked, and how to test them.

Nothing here was measured on a console. The driver patches in `patches/mesa` were built and run on
the driver's PC model (the PS5 winsys's host build, which runs everything but the GPU), and their
console code was compiled against the platform headers. Every number for the console below comes
from PS5_Vulkan's own measurements, cited by their finding names (R34, R68 and so on in its
`docs/HARDWARE_FINDINGS.md`), or is marked as an estimate.

## The driver: 7b59ef2, with 120 Hz that works and frame pacing

The app pinned PS5_Mesa `0b2d6d1a` until 2026-10-06. Upstream has since rewritten its commit ids
(that tree is now `504adad`, and the SDK fork's `95c08f27` is now `b83202b`, the same tree), so the old
pins may stop being fetchable. `tools/deps.json` now pins upstream's current driver, `7b59ef27`
(PS5_Vulkan `fde9e37` builds the same), which is `504adad` and four changes to the VideoOut swapchain:

| Change | What it does | What PS5CEMU-HAR does with it |
| --- | --- | --- |
| `f46af72` `PS5_VIDEOOUT_PARAM_JSON` | The driver offers 119.88 Hz only to a title whose `param.json` declares it, and read only `/app0/sce_sys/param.json`. A process the HEN jailbreaks has no `/app0` (`port/app/paths.h`), so **119.88 Hz was never offered there, and "120 Hz output" did nothing** (the boot log said `VideoOut surface 3840x2160 at 59.94 Hz`). The variable names another file | With "120 Hz output" on, the app names its own `param.json` before a Wii U game starts. With it off, it names a file that does not exist: in a sandboxed process the driver used to find `/app0`'s and switch the display to 119.88 Hz whatever the setting said, since the 59.94 Hz mode Cemu then took was only a mode handle |
| `12d0391` attribute3 bit 0x40 alone | 0x80000 (a 120 Hz mode that requires VRR) turned the system's VRR off; retail titles with VRR declare 0x40 with 0x40000 | `sce_sys/param.json` declares 0x40040 (was 0x80040). The driver reads the file either way; the home screen may keep the old value until the app is registered again |
| `aa44fa4` `wsi_videoout_set_flip_rate` | Each flip shows for at least 1, 2 or 3 vblanks (`sceVideoOutSetFlipRate`) | **Frame pacing**, a new setting (below) |
| `7b59ef2` a mode for each size | Modes at 3840x2160, 2560x1440 and 1920x1080, which VideoOut scales to the screen | The surface takes the 3840x2160 mode by size (`port/ps5/vulkan_display.cpp`), as before |

The winsys, where `patches/mesa` apply, is the same in both, and the patches apply unchanged.

### Frame pacing

**Settings > Video > Frame pacing** (and the in-game menu's Graphics, which changes it at once) keeps
each frame on screen for at least two or three refreshes, through the driver's flip rate
(`port/ps5/display.cpp`). A game that cannot hold the display's rate then runs at an even lower one,
instead of mixing frames shown for one refresh with frames shown for two:

| Output | Frame pacing | Each frame shows for | When the next is late, a frame shows for | Suits |
| --- | --- | --- | --- | --- |
| 119.88 Hz | Off | 8.3 ms or more | 16.7 ms | a game that holds 120 |
| 119.88 Hz | 60 fps | 16.7 ms or more | 25 ms | **BotW at 4K**, FPS++ at 60 |
| 119.88 Hz | 40 fps | 25 ms or more | 33 ms | BotW at 5K or 8K, if it holds 40 |
| 59.94 Hz | Off | 16.7 ms or more | 33 ms | as before |
| 59.94 Hz | 30 fps | 33.3 ms or more | 50 ms | **BotW at 8K**, FPS++ at 30 |

The boot log says what was asked of VideoOut (`[vulkan] frame pacing: each frame shown for at least
2 refreshes`) and what the surface got (`[vulkan] VideoOut surface 3840x2160 at 119.88 Hz`, or a line
saying 120 Hz output is on but VideoOut offers 59.94 Hz only). The pacing is the flip rate of the
whole output, so the app puts it back to every refresh before the launcher and the 3DS side.

## Breath of the Wild at 4K60 and 8K30: the settings

VideoOut's largest buffer is 3840x2160, so 8K is drawn at 7680x4320 and scaled to the 4K screen by
Cemu (four samples a pixel). Both use the bundled community graphic packs (the in-game menu, or
Triangle on the game in the library):

| | 4K at 60 | 8K at 30 |
| --- | --- | --- |
| Graphics > Resolution | 3840x2160 (4K) | 7680x4320 (8K) |
| Graphics > Anti-Aliasing | Normal FXAA | **None**: the 8K picture scaled to 4K is already smooth, and FXAA would be one more full pass at 8K |
| Graphics > Shadows | Medium (100%) | Medium (100%) |
| Mods > FPS++ | On, 60FPS Limit | On, 30FPS Limit (or off: the game's own 30) |
| Settings > Video > 120 Hz output | On, where the TV has it | **Off** (frame pacing cannot make 30 at 120 Hz) |
| Settings > Video > Frame pacing | 60 fps (with 120 Hz output; Off at 60 Hz) | 30 fps |
| In-game > Graphics > Accurate barriers | Off, if the game shows no glitches with it off | the same |

If 8K does not hold 30, 5120x2880 (5K) with 120 Hz output and frame pacing at 40 fps is the next
step. 8K's render targets are four times 4K's, and nothing has measured how much of the console's
memory BotW at 8K takes: the boot log's memory lines (`[perf] ... heap` and Cemu's start) are where
to look first if 8K stops or stutters where 4K does not.

## Where things stand

- **BotW already holds about 60 fps at 1440p** ([COMPATIBILITY.md](COMPATIBILITY.md), and the
  timer regression patch 0011 found while testing it). 4K is 2.25 times the pixels. Most of Cemu's
  CPU work does not grow with resolution, so going from 1440p to 4K is mostly a question of GPU
  time, and of anything that leaves the GPU idle while there is work for it.
- **The GPU has room on paper.** PS5_Vulkan reports Eden's Vulkan renderer on this same driver
  holding 4K60 with the GPU about 19% busy, and mostly 60 at 8K at about 55%. BotW through Cemu is
  a heavier GPU load (a deferred renderer, Cemu's translated shaders, its surface copies), so that
  is a ceiling, not a forecast.
- **Nothing measures whether BotW is GPU-bound on the console.** That decides which of the changes
  below matter, which is why the first two are measurements.

## How the driver works

It is upstream RADV and ACO (Mesa 26.2) with a winsys of its own in place of amdgpu
(`src/amd/vulkan/winsys/ps5`), a VideoOut swapchain in place of DRM's (`src/vulkan/wsi/wsi_common_videoout.c`),
and a few `RADV_PS5` changes in RADV itself. The GPU is described as GFX10.3 (Navi 21's register
programming and shader ISA) with GC 10.1.3's fixed-function traits, each found on the console first.

### Memory

- Every buffer and image is one direct-memory allocation of **type 12**, mapped for CPU and GPU read
  and write at one address, for its whole life. The GPU address is the CPU address, so nothing is
  ever "made resident" and mapping is free.
- Measured (2026-09-27): that memory is **CPU-cached and coherent both ways** for shader and copy
  access, so host-visible memory needs no flushes. Buffers zeroed at creation (the GTT domain) pay a
  memset and an eviction of the whole buffer once.
- Every Vulkan memory type RADV reports (device-local, host-visible, cached; the 2/3 "VRAM" and 1/3
  "GTT" split of the pool) is that same memory. The distinctions are bookkeeping.
- Descriptors, push constants and shader code sit in a 4 GiB window at `0x2_0000_0000`, because the
  shaders' 32-bit pointers carry high word 2. Everything else goes in a 256 GiB region at
  `0x40_0000_0000`, handed out in 2 MiB granules.
- Type 12 is the type the platform header calls the title's CPU-only memory. No other type was ever
  tried, and the GPU's bandwidth on it was never measured; Eden's numbers suggest it is fine.

### Command streams and submission

This is where the port differs most from Linux.

- The console faults the GPU on an `INDIRECT_BUFFER` into title memory (PS5_Vulkan B8), so **nothing
  is chained**: a command buffer's words live in ordinary `malloc` chunks, and every submission
  **copies all of them** into a 16 MiB ring of GPU memory (or a buffer of its own when larger than
  half the ring), then **evicts every line** it wrote with `CLFLUSHOPT`, then calls
  `sceAgcDriverSubmitDcb`.
- Each AGC submission **starts from reset GPU state** and is limited to 2^20 words (2026-09-26), so
  every one starts with RADV's full preamble, and a stream is split at points where RADV re-emits
  its state (every 512K words, at a draw or dispatch).
- Every submission ends with a `RELEASE_MEM` that flushes the colour and depth caches and **writes
  back and invalidates the GPU's L2**, GL1 and the vector caches (GCR `0x30c`: GL2_WB, GL2_INV,
  GL1_INV, GLV_INV), then writes the submission's sequence number to a marker.
- Without `sceAgcSuspendPoint` after it, a submission starts up to a refresh late (R68). A kick
  thread makes that call, so the submitting thread no longer waits the ~0.13-0.3 ms it takes. On a
  tester's base PS5 the late start happened *with* the suspend point too, which is unexplained.
- The submission path times itself for a statistics line every 10 s (`radv/ps5 submissions:
  window count= kwords= claim_ms= copy_ms= flush_ms= agc_ms= suspend_ms=`), with **seven reads of
  the monotonic clock per submission**. `clock_gettime` is a system call on the console: 20 us in
  R34, about 800 ns in R68.

### Completion and waits

- One queue for everything; submissions run in order. A sync object is a sequence number: a wait
  for a semaphore needs only its signal submitted, never a GPU wait.
- The CPU learns of completion by evicting the marker's line and reading it. A wait spins for
  1.5 ms, then **sleeps 1 ms between looks**. Each look, and each `vkGetFenceStatus` that finds its
  fence unsignalled, reads the clock.

### Presenting

- VideoOut takes five 32 MiB 3840x2160 framebuffers, registered once, as 64 KiB R_X tiles (no DCC on
  the swapchain); a 2560x1440 or 1920x1080 swapchain gets five of its own. FIFO is the only present
  mode. The driver configures 119.88 Hz when it first opens VideoOut, where `param.json` declares it
  (`PS5_VIDEOOUT_PARAM_JSON`, above) and the display runs at it, and a flip shows for at least as
  many vblanks as the flip rate says.
- A present goes to a **flip thread**, which waits for the frame's fence (the spin-then-1 ms-sleep
  wait above) and then calls `sceVideoOutSubmitFlip` for the next vblank. A frame that finishes
  just before a vblank can miss it while the thread sleeps. A 1 ms sleep is an eighth of a refresh
  at 119.88 Hz; patch 0003 makes it 200 us.
- `sceAgcDcbSetFlip`, a flip packet the GPU runs in the stream itself, works once VideoOut is open
  (2026-09-17) and was how PS5_Vulkan's first driver (ps5vk) flipped. RADV's swapchain does not use
  it.

### What the GPU cannot do here, and what it costs

| Missing | What the driver does instead | Matters for BotW? |
| --- | --- | --- |
| Legacy geometry shaders (they hang) | Every GS is NGG; a GS with streamout runs as compute (poly) | Only if a GS with transform feedback is used |
| `v_dot4_u32_u8` | NGG culling's workgroup repack uses a slower fallback | Possibly: NGG culling is on by default (test `nonggc`) |
| Flat scratch | Scratch through buffer instructions | Only for shaders that spill |
| Indirect buffers | No device-generated commands; no IB chaining (the copy above) | Yes: every submission is copied |
| Perf counters, VRS, RB+ | Not reported | No |

### Threaded recording

The fork has a layer of its own (`RADV_THREADED_RECORDING=1`): the application's `vkCmd*` calls
are queued with their arguments copied, and a worker thread records them while the application
goes on; `vkEndCommandBuffer` waits for the worker. It passed a 474,440-case CTS gate with every
case unchanged, and stays off until it measures faster in a game. **PS5CEMU-HAR never sets it.**

## How Cemu drives it

From Cemu's Vulkan renderer at the pinned commit:

- **Submissions:** every 300 draws, within 10 draws of any occlusion query ending, on texture
  readbacks, and at each swap. Each waits on the previous one's semaphore and has a fence.
- **Polling:** `vkGetFenceStatus` on the oldest unfinished command buffers after every submit.
- **At swap:** a wait for the *previous* frame's last command buffer, so one frame is in flight.
- **All on one thread.** Cemu's GPU thread decodes the Wii U's command stream, records Vulkan,
  and submits. RADV submits at once (no wait comes before its signal), so the copy, the flush and
  `sceAgcDriverSubmitDcb` all land on that thread.
- **Guest memory as a GPU buffer** (`m_useHostMemoryForCache`, through
  `VK_EXT_external_memory_host`, which this driver supports) is compiled out upstream. Cemu
  uploads vertex and uniform data instead. The Metal backend uses the same idea on Apple's
  unified memory.

## Where a BotW frame's time can go

| Cost | Where | Size per frame | Source |
| --- | --- | --- | --- |
| Clock reads while submitting | Cemu's GPU thread | 7 per submission: 6-140 us each submission at R68/R34 costs, times 10-30 submissions | Estimate |
| Copy and eviction of every word | Cemu's GPU thread | Two passes over everything recorded | Code; host benchmark below |
| `sceAgcDriverSubmitDcb` | Cemu's GPU thread | `agc_ms` in the statistics line | Unmeasured for Cemu |
| RADV recording (state, descriptors, draws) | Cemu's GPU thread | Thousands of draws | Unmeasured; threaded recording moves it |
| Each AGC submission's state reset, preamble and L2 write-back and invalidate | GPU | A bubble at each submission boundary | Unmeasured |
| Waits waking up to 1 ms late | Swap wait; flip thread | Up to a missed vblank | Code |
| Base PS5 late start (R68) | GPU idle | Up to a refresh per idle start | Measured on one tester's console |

## Changes, ranked

### 1. Measure first (in this repo, no driver rebuild)

These are in the working tree alongside this document.

- **The driver's messages in `boot.log`** (`port/ps5/log.cpp`, `ps5log::ForwardDriverMessages`).
  RADV writes its statistics, the VideoOut mode it took (`wsi/videoout: 119.88 Hz refused ...`) and
  its errors to stderr, which only the klog sees. Lines starting `radv`, `wsi/`, `MESA` or `ACO`
  now also go to the boot log as `[driver] ...`; the rest of stderr goes where it went. A full pipe
  drops a line rather than stalling a writer.
- **`radvEnvironment` in `ps5cemu.json`** (`port/frontend/settings.*`, `port/main_ps5.cpp`): driver
  variables set before either emulator starts, for A/B tests without a rebuild. Only `RADV_`,
  `MESA_` and `ACO_` names are taken, and `RADV_DEBUG` stays `radvDebug`. For example:

  ```json
  "radvEnvironment": {"RADV_THREADED_RECORDING": "1", "RADV_PS5_GPU_TIME": "1"}
  ```

### 2. Driver patches (`patches/mesa`, against PS5_Mesa `7b59ef27`)

Built and run on the PC model: about 110,000 submissions in 11 s, including 3.1M-word command
buffers split into five AGC submissions in a buffer of their own, with RADV's assertions on (its
check that the words copied equal the words planned held throughout). The console branch compiles
against PS5_PayloadSDK's headers. None of them has run on a console. They were written against
`0b2d6d1a` and apply to `7b59ef27` unchanged (the four commits between touch only the swapchain);
RADV builds with them there.

| Patch | What it changes | Expected effect | Risk |
| --- | --- | --- | --- |
| 0001 TSC timing | The submission path times with the TSC (about 12 ns); the statistics window reads the clock once per 64 submissions and calibrates its tick rate over the window itself (no TSC frequency assumed) | Removes 7 system calls per submission from Cemu's GPU thread | Low: only the statistics use it |
| 0002 Streamed copy | Words go into the ring with non-temporal stores; the eviction pass becomes one `SFENCE` | About half the copy cost (on a host Xeon, a 256 KiB submission: 72 us to 33-43 us) | Low: correct whether or not the CP's fetch sees CPU caches, which was never measured |
| 0003 Waits | `vkGetFenceStatus` (a zero timeout) reads no clock; waits past their spin sleep 200 us, not 1 ms | Fewer system calls; the flip thread and the swap wait wake within 0.2 ms of the GPU | Low: a few more wake-ups per long wait |
| 0004 GPU busy time | With `RADV_PS5_GPU_TIME=1`, each submission carries a start timestamp (`COPY_DATA` of the GPU clock, RADV's top-of-pipe timestamp) and an end one (the bottom-of-pipe `RELEASE_MEM` the driver's GPU clock read already uses), and the statistics line gains `gpu_busy_pct` | Says whether BotW at 4K is GPU-bound | Low: off by default, and then nothing changes |

### 3. Settings to test (no rebuild)

| Test | Where | Why |
| --- | --- | --- |
| `RADV_THREADED_RECORDING=1` | `radvEnvironment` | Takes RADV's recording off Cemu's GPU thread, the thread that also decodes the Wii U's commands |
| `nonggc` | `radvDebug` | NGG culling pays the slow repack fallback here; Wii U geometry is light and 4K is pixel-bound |
| Accurate barriers off | In-game menu, Graphics | Fewer full pipeline barriers between Cemu's passes, which matter more at 4K |
| 120 Hz output and frame pacing at 60 fps | Settings > Video | A late frame costs 8.3 ms instead of 16.7 ms, so "mostly 60" looks closer to 60; with the driver before `7b59ef2`, 120 Hz never reached a jailbroken process |
| `RADV_PERFTEST=pswave32` | `radvEnvironment` | RADV recommends wave64 for pixel shaders; a quick check, low expectations |

### 4. Driver changes proposed, not written

1. **Flip on the GPU.** Submit `sceAgcDcbSetFlip` (mode 1) after the frame's work on the same
   in-order queue, as ps5vk did, instead of a CPU thread waiting for the fence and then flipping.
   The flip then cannot miss a vblank because a thread slept, and a core stops spinning on every
   frame. Needs a hook from the generic WSI into the winsys and the flip bookkeeping kept for
   acquires. Medium effort; the packet's behaviour was measured in PS5_Vulkan M5 C1.
2. **Fewer AGC submissions per frame.** Each one resets state, replays the preamble and empties
   the L2. Cemu's 300-draw threshold (`VulkanRenderer::SubmitCommandBuffer`) could be a setting
   (a patch in `patches/cemu`), tested at 600-1500 against `count=` and `gpu_busy_pct`. Too few
   submissions delay the GPU's start on a frame, so it is a measurement, not a default to flip.
3. **Investigate the base PS5's late start (R68).** If each idle start of the GPU waits for a
   refresh on a base console, Cemu's many submissions are the worst case for it. A probe like R67's
   stamps, run on a base PS5 with several submissions per frame, would settle it.
4. **No copy at all.** Record straight into GPU memory, reserving room for the preamble before a
   stream and the completion after it, so a one-time-submit command buffer is submitted where it
   was written. Larger change; patch 0002 takes most of the cost away first.
5. **Waits that block on the GPU's interrupt.** On the PS4, `RELEASE_MEM` can raise an
   end-of-pipe interrupt that wakes a kernel event queue. Nothing in PS5_PayloadSDK's AGC header
   exposes one; finding whether AGC exports it would end the spinning and the sleep latency both.
6. **Build at `-O3` with LTO.** The "release" archive is `-Dbuildtype=debugoptimized` (`-O2 -g`)
   with assertions off. A small CPU gain on recording, to be measured.
7. **Measure the GPU's bandwidth on type-12 memory**, and try the other types, with a streaming
   compute shader. Low priority given Eden's numbers.

Bigger, outside the driver: **Cemu's guest memory as a GPU buffer** (the compiled-out path above).
BotW's vertex and uniform uploads go away when the GPU reads the Wii U's RAM directly, and this
driver can import it (the port maps guest RAM as type-12 direct memory, which
`sceKernelMprotect` can give GPU access). Cemu's Vulkan path is unfinished upstream; its Metal
path is the model.

## Building with the patches

PS5_Vulkan's recipe exports exactly the pinned revision (`git archive 7b59ef27`), so the patches
need a build of the fork's own tree, which `tools/link.sh` then takes through `RADV_ARCHIVE`:

```sh
make radv                                   # the recipe once: SDK, host tools, cross files
cd .deps/PS5_Mesa
git checkout -b ps5cemu-perf 7b59ef27c1b09b9671bc4153c41940c3155c3af2
git am ../../patches/mesa/*.patch
work=../PS5_Vulkan/.deps/work
PATH=$work/radv-clc-bin:$PATH meson setup build-ps5 \
    --cross-file $work/radv-cross-constants.ini --cross-file ../PS5_Vulkan/tooling/radv/ps5-cross.ini \
    $(cat $work/radv-build-ps5-release/.radv-options) \
    -Dradv-build-id=7b59ef27c1b09b9671bc4153c41940c3155c3af2
ninja -C build-ps5 src/amd/vulkan/libvulkan_radeon.a
cd ../..
make package RADV_ARCHIVE=$PWD/.deps/PS5_Mesa/build-ps5/src/amd/vulkan/libvulkan_radeon.a \
    RADV_SDK=$PWD/.deps/PS5_Vulkan/.deps/native/ps5-payload-sdk
```

The options come from the recipe's own release build, so the archive is configured the same way.
Keeping the pinned revision as the build id keeps existing shader caches valid; the patches touch
only the winsys, never the compiler. A change that touches the compiler needs a new id. Moving from
`0b2d6d1a` to `7b59ef27` is a new id: each game's pipelines are compiled again once, on its first
start with the new driver.

The winsys's PC model (`-Dradv-winsys=ps5` without the cross files) builds on Ubuntu 24.04 with
`libdrm-dev` installed and `-Dc_args="-include host-types.h"` (and `cpp_args`), where `host-types.h`
includes `<linux/types.h>` outside assembly: `ac_surface.c` uses the kernel's `__u64`, which no
header includes when DRM is left out.

## Test plan

Same save, same spot, same graphic packs; 60 s per run, then **Settings > Diagnostics > Copy logs to
USB**. Busy places are the useful ones: Kakariko Village, Hateno Village, the view from the Great
Plateau tower.

1. Baseline at 1440p and at 4K, with `"radvEnvironment": {"RADV_PS5_GPU_TIME": "1"}` and the
   patched driver. Record FPS and the `[driver] radv/ps5 submissions` lines. With 120 Hz output on,
   check the boot log says `VideoOut surface 3840x2160 at 119.88 Hz`.
2. The two columns of "Breath of the Wild at 4K60 and 8K30" as they stand, then with frame pacing
   off, for what pacing changes.
3. Each row of "Settings to test", one at a time, at 4K.
4. The unpatched driver at 4K, for the patches' own effect (`copy_ms`, `flush_ms` and FPS).

Reading the lines (per 10 s window):

- `count` divided by 10 s and by the FPS is **submissions per frame**.
- `copy_ms + flush_ms + agc_ms + claim_ms` is the **driver's submission cost on Cemu's GPU thread**;
  divided by the frames in the window, it is milliseconds per frame out of 16.7.
- `gpu_busy_pct` near 90-100 with FPS under 60 at 4K: **GPU-bound**. The levers are fewer
  submissions, `nonggc`, Accurate barriers off, graphic-pack costs (shadow resolution, AA), and if
  it still falls short, a lower internal resolution (1800p) scaled to the 4K output.
- `gpu_busy_pct` well under 80 with FPS under 60: **CPU- or latency-bound**. The levers are
  threaded recording, the patches, the flip on the GPU, and Cemu's own CPU settings.
