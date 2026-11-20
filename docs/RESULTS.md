# Measurements (Mac, CrossOver 64-bit bottle, DXVK, RX 5700 XT; 2026-11-20)

"private" = committed private memory of the 32-bit Condor.exe (the `vm` remote command of the mod), i.e. what fills the 4 GB address space.
Flight: Free flight at Orly-06, glider at the runway start unless noted.

## In flight, at the start position
| landscape | autogen on disk | game alone (vanilla) | with streaming (helper) |
|---|---|---|---|
| OrlyNoAG (OrlyDemo without autogen) | 0 | 688 MB | - |
| OrlyDemo (12 autogen tiles) | 547 MB C3D + 336 MB DDS | 2253..2291 MB | 885 MB |
| OrlyDense (same, autogen density x3, `tools/make_dense.py`) | 1642 MB C3D + 336 MB DDS | **crashes** right after "ENTERED SIMULATION" (2.7 GB committed during load, largest free block 489 MB) | 1280..1295 MB |

Streaming at the start position: 176 of 192 chunks resident (12-13 at LOD 0 up to 2 km, ~41 at LOD 1 up to 5 km, ~122 at LOD 2 up to 12 km).
The helper (64-bit) holds OrlyDemo in about 1.5 GB (all LODs), OrlyDense in about 4 GB; opening takes 12 s / 36 s.

## Jump test (scripts/viewshots.sh with 6 positions ~20 km apart, 4 s each; one round = 6 jumps, ~420 chunk loads)
Extreme on purpose: every jump replaces nearly all autogen chunks and the game's own terrain texture LODs.

| run | start | after round 1 | round 2 | round 3 |
|---|---|---|---|---|
| vanilla OrlyDemo | 2253 MB | 2963 MB | 3898 MB | 3907 MB (only 110 MB address space left) |
| vanilla OrlyNoAG (no autogen at all) | 688 MB | 820 MB | 965 MB | |
| streaming OrlyDemo (final build) | 933 MB | 1069 MB | 1195 MB | |
| streaming OrlyDense (final build) | 1311 MB | 1517 MB (pools fill) | 1645 MB (largest free block 1.8 GB) | |
| streaming OrlyDense (before the upload/pool fixes) | 1295 MB | 1604 MB | 1852 MB | 2101 MB (still running) |

Not verified yet: the building collision of streamed chunks (the full-detail chunk under the glider keeps its CPU copy and gets the real
tile number, exactly what the game's collision test reads; a teleport test only produced a G-force crash). Not run on native Windows yet.

The growth that remains with streaming is the same as without any autogen: it comes from the game's own terrain texture streaming under
CrossOver (the new 16 MB blocks contain bytes of `Textures/tXXYY.dds`), not from the autogen path. The autogen path itself had three
leaks, all fixed and verified one by one:
1. chunk answers and textures copied into `malloc` memory: Wine's C heap grows in 16 MB segments and never returns them -> `VirtualAlloc`.
2. textures created/destroyed per chunk fragment DXVK's memory -> texture pool by shape (all atlas pages of one LOD have the same shape).
3. vertex/index buffers of every size -> buffer pool in size classes; uploads through our own staging ring (Map + Copy) instead of
   UpdateSubresource / initial data.
Checks: the game's destructor releases VB/IB/input layout (refcount 0 after free), our texture references are the only ones left.

## Look
Screenshots with the game's own autogen and with streaming are indistinguishable at the start, 2 km behind at 400 m and 5 km behind at
700 m (`postfx/shots/off_*.png` / `on_*.png` in the game folder). Close views show the 3D buildings and trees, lit like before.
