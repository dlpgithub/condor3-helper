# condor3-helper: 64-bit data server for Condor 3 (beats the 32-bit memory limit)

Condor 3 is a 32-bit program: everything it holds must fit into 4 GB of address space. Dense landscapes (photogrammetry
buildings as autogen) fill that quickly, because the game keeps every autogen tile of a 4 x 4 tile window as one model and keeps
each mesh twice (CPU copy + GPU buffer). Measured with `OrlyDemo` (12 autogen tiles, 883 MB on disk): **2.3 GB** in flight, of which
**1.6 GB** is autogen.

This project moves the heavy data out of the game:

- `c3helper.exe` (this repo, **64-bit** Windows program) loads a landscape's complete autogen (`Autogen/oXXYY.c3d` + textures),
  cuts every tile into 4 x 4 chunks (dense ones split further, max 20 MB each) and builds 4 levels of detail, all in 64-bit memory
  (4 tiles in parallel). The levels come from the detail sets of `Autogen/lods.txt` when the landscape has them (tiles2condor_v2 writes
  them from the export's own coarser levels, e.g. L20 / L19 / L17), the remaining ones are vertex clustering (8 m, 24 m, 72 m for
  landscapes without sets). Each level carries its geometric error in metres.
- the mod (`d3dx.dll` of `condor3-mod`, module `src/agstream.c`) switches the game's own autogen loading off, asks the helper for
  the chunks in view range and picks each chunk's level by its screen-space error: the coarsest level whose error looks smaller than
  2 pixels from the camera (3D distance, altitude counts). It turns each answer into a real game model that the game draws with its
  own shaders. Far chunks use textures with fewer mip levels. Chunks that move out of range are freed.
- the two talk through shared memory in the same CrossOver bottle / Windows session (`include/c3h_proto.h`).

Result with `OrlyDemo`: **~0.9 GB instead of 2.3 GB** in flight, same look (see docs/RESULTS.md).

## Build
    scripts/build.sh            # build/c3helper.exe (64-bit) and build/c3htest.exe (32-bit test client); needs the c3dev Docker image
                                # of condor3-mod (MinGW-w64 has both compilers)
The mod part is built with condor3-mod (`scripts/build_docker.sh`, option `ENABLE_AGSTREAM`, on by default).

## Install
Copy `build/c3helper.exe` next to `Condor.exe` and install the mod's `d3dx.dll` as usual (`scripts/postfx.sh install` /
`scripts\postfx.bat install`). Without `c3helper.exe` the mod leaves autogen to the game.
Optional `<Condor>\postfx\agstream.txt` (one `key value` per line):

| key | default | meaning |
|---|---|---|
| `enable` | 1 | 0 = let the game load autogen itself |
| `tol` | 2 | screen-space error tolerance in pixels: smaller = finer levels farther out (more memory and streaming) |
| `range` | 15000 | no autogen beyond this distance (m, camera to chunk box) |
| `budget` | 3 | main-thread streaming work per frame in ms (textures, 4 MB buffer pieces; at least one step per frame) |
| `lodmode` | 1 | 0 = the old fixed ranges `r0`, `r1`, `r2` (2000, 5000, 12000 m, glider to chunk box, 2D) |

Logs: `<Condor>\postfx\agstream.log` (mod side), `<Condor>\postfx\c3helper.log` (helper). Remote command `ag` (mod's `postfx\cmd.txt`)
prints the streaming state and the frame-time statistics, `vm` the 32-bit address-space use (the automatic `vm` every 10 s is off;
`vm auto on` switches it on). Frames over 30 ms are logged in agstream.log with what streaming did in the tick before
(`slow frame ...`), so stutters can be told apart from the game's own.

## Test without the game
    wine c3htest.exe 'Y:\Condor3\c3helper.exe' 'Y:\Condor3\Landscapes\OrlyDemo'
starts the helper, opens the landscape and fetches every chunk at every LOD and every texture once (checks sizes and indices).

## Files
- `include/c3h_proto.h` protocol (shared with the mod; copy in condor3-mod/src), `src/helper.c` the helper,
  `src/c3h_client.c/.h` 32-bit client (copy in condor3-mod/src), `tools/c3htest.c` test client,
  `tools/make_dense.py` stress-test landscape (autogen density x N).
- `docs/AUTOGEN_RE.md` how the game loads/draws/frees autogen (addresses, object layouts), `docs/RESULTS.md` measurements.
- `re/` Ghidra headless scripts used for the reverse engineering (`re/g.sh <Script.java> <out> args`).
