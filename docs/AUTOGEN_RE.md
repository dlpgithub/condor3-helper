# How Condor 3 loads, draws and frees autogen (reverse engineered, Condor.exe base 0x400000)

Status 2026-11-20. Found with Ghidra (`re/g.sh` + `re/ghidra_scripts/*.java` on the saved project `~/ghidra_proj/condor`).
All game functions use the Delphi register convention: EAX, EDX, ECX, then stack arguments pushed left to right, callee pops.

## Objects
- Landscape `L = [[0x792fd0]+0x44]`: tiles x/y as u16 at +0x1a4/+0x1a6, cell size (90) at +0x1b8, `.oha` table at **+0x44**
  (`row = [L+0x44][tx]`, `entry = row[ty]`, a Delphi string with the MD5 or 0 = no autogen), autogen integrity flag +0x48.
- Ground objects `G = [[0x792fd0]+0x48]`; **autogen manager `AG = [G+0x20]`**:
  `+4` TList of resident models (drawn), `+8` TList built by the cache thread, `+0xc/+0x10` centre tile of the window.
- Autogen model (class ref `[0x6dedac]`, VMT 0x6dedf8; a TList of parts, count at +8):
  `+0x11` "no D3D yet" flag, `+0x24` vertex buffer, `+0x28` index buffer, `+0x2c` input layout, `+0x50` effect,
  `+0x54` path, `+0x58` dyn array of groups (first part, count), `+0x5c` 4x4 matrix (identity), `+0x9c` = 1 for autogen,
  `+0xa4..+0xb8` AABB min/max (+0xb8 = max z), `+0xd0` = 0.2f, `+0xe4` vertex dyn array (32 bytes: pos, normal, uv),
  `+0xe8` index dyn array (u32, absolute), `+0xec/+0xf0` tile x/y, `+0xf4` position = tile centre
  `((tx+0.5)*64*cell, (ty+0.5)*64*cell, 0)`, `+0x100` world matrix (from `FUN_00761014(L, &m+0x100, &m+0xf4)`),
  `+0x8c/+0x90` copy of +0x100/+0x104.
- Part (class ref `[0x76fb40]`, created by `FUN_007708a8(EAX=classref, DL=1)`): `+0x10` name ShortString, `+0xa4` texture ShortString,
  `+0x125` colour (4 f32, unaligned), `+0x138/+0x13c/+0x140` spec/shiny/env, `+0x144` transparent flag, `+0x14c` texture SRV
  (interface, AddRef'd copy from the texture manager), `+0x150` AO, `+0x154` BM, `+0x1e0` first index, `+0x1e4` index count,
  `+0x1e8` first vertex, `+0x1ec` vertex count.

## Flow
1. `FUN_006def04` = AutogenCacheThread.Execute (worker thread, at landscape load and whenever the camera enters a new tile):
   for every tile of the 4x4 window `cx-2..cx+1, cy-2..cy+1` that is new and has an `.oha` entry:
   `m = FUN_007709bc([0x6dedac], DL=1, CL=0, push 1, push 0)` (no D3D), `TList.Add(AG+8, m)`, `m+0x9c = 1`,
   `FUN_00771b98(m, "Landscapes\<L>\Autogen\O%03d%03d"|"%02d%02d", CL=0, push 1,0,0,1)` (load C3D), tile + position fields, MD5 check.
   Then `AG+0xc/+0x10 = centre` and the done flag `[0x792fd0]+0x28 = 1`.
2. `FUN_00771b98` (model load): reads the C3D (`FUN_00771db4`), computes bounds (`FUN_00770a74`); because `+0x11 = 1` it does NOT
   create GPU buffers and does NOT free the CPU arrays.
3. When all cache threads are done, the main thread runs "Landscape refresh" -> `FUN_006de798` (GroundObjects.AfterTesselate)
   -> **`FUN_006df568`** (autogen): for every new model `+0x50 = FUN_00775938(**0x793978)`, **`FUN_00771134(m, DL=1)`** = textures
   (texture manager `FUN_005c4900`, cached by name, kept for the whole flight) + CreateInputLayout + CreateBuffer(VB, IB), move into
   `AG+4`; free every model outside the window; recompute every model's matrix with `FUN_00761014`.
   **The CPU arrays are never freed**: for every resident tile the game keeps the mesh twice (CPU copy + GPU buffer).
4. Every frame: `FUN_006df690` (AutoGen.Update): collision for the model whose tile equals the glider's tile and whose max z is above
   the glider: `FUN_00752c10` tests TRIANGLES from `+0xe4/+0xe8`. `FUN_006df898` (DrawAutogen): for each model in `AG+4`, frustum test
   of the transformed AABB, then `FUN_00772e98(EAX=m, push 1.0f)` (sets matrices on `+0x50`, binds VB/IB/layout, DrawIndexed per part
   of group 0).
- Useful others: TList.Add 0x42309c (EAX list, EDX item), TList.Get 0x4231fc, TList.Delete 0x4230e8, TObject.Free 0x404d50,
  DynArraySetLength 0x407608 (EAX &arr, EDX typeinfo, ECX dims, push len; typeinfos: vertices `[0x4ac838]`, indices `[0x4ac808]`,
  groups `[0x76fc70]`), free model CPU arrays 0x771458, identity matrix source `[0x792bd4]`.
- The main loop logs "Memory usage: %.2f / %d MB" after every landscape refresh (Logs/Logfile.txt).

## Measured (Mac, CrossOver/DXVK, OrlyDemo 12 autogen tiles = 547 MB C3D + 336 MB DDS, flight at Orly-06)
- private committed memory in flight: 2285 MB with autogen, 688 MB with the same landscape without autogen.
