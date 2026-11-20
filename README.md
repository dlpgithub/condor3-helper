# condor3-helper: 64-bit data server for Condor 3 (beats the 32-bit memory limit)

Condor 3 is a 32-bit program: everything it holds must fit into 4 GB of address space. Dense landscapes (photogrammetry
buildings as autogen) fill that quickly, because the game keeps every autogen tile of a 4 x 4 tile window as one model and keeps
each mesh twice (CPU copy + GPU buffer). Measured with `OrlyDemo` (12 autogen tiles, 883 MB on disk): **2.3 GB** in flight, of which
**1.6 GB** is autogen.

This project moves the heavy data out of the game:

- `c3helper.exe` (this repo, **64-bit** Windows program) loads a landscape's complete autogen (`Autogen/oXXYY.c3d` + textures),
  cuts every tile into 4 x 4 chunks and builds 3 levels of detail (full; vertex clustering at 8 m; at 24 m), all in 64-bit memory.
- the mod (`d3dx.dll` of `condor3-mod`, module `src/agstream.c`) switches the game's own autogen loading off, asks the helper for
  the chunks around the glider (full detail up to 2 km, LOD 1 to 5 km, LOD 2 to 12 km), and turns each answer into a real game model
  that the game draws with its own shaders. Far chunks use textures with fewer mip levels. Chunks that move out of range are freed.
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
| `r0`, `r1`, `r2` | 2000, 5000, 12000 | distance (m, glider to chunk box) up to which LOD 0 / 1 / 2 is used; beyond: no autogen |
| `jobs` | 1 | chunks turned into game models per frame |

Logs: `<Condor>\postfx\agstream.log` (mod side), `<Condor>\postfx\c3helper.log` (helper). Remote command `ag` (mod's `postfx\cmd.txt`)
prints the streaming state, `vm` the 32-bit address-space use.

## Test without the game
    wine c3htest.exe 'Y:\Condor3\c3helper.exe' 'Y:\Condor3\Landscapes\OrlyDemo'
starts the helper, opens the landscape and fetches every chunk at every LOD and every texture once (checks sizes and indices).

## Files
- `include/c3h_proto.h` protocol (shared with the mod; copy in condor3-mod/src), `src/helper.c` the helper,
  `src/c3h_client.c/.h` 32-bit client (copy in condor3-mod/src), `tools/c3htest.c` test client,
  `tools/make_dense.py` stress-test landscape (autogen density x N).
- `docs/AUTOGEN_RE.md` how the game loads/draws/frees autogen (addresses, object layouts), `docs/RESULTS.md` measurements.
- `re/` Ghidra headless scripts used for the reverse engineering (`re/g.sh <Script.java> <out> args`).
