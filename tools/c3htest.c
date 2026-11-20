// c3htest.exe (32-bit) - end-to-end test of the helper outside the game:  c3htest <c3helper.exe> <landscape folder> [log]
// Starts the helper, opens the landscape, fetches every chunk at every LOD and every texture once, checks the answers.
#include <windows.h>
#include <stdio.h>
#include "c3h_client.h"

int main(int argc, char** argv) {
    C3hClient c; char err[256]; int st, i, l; C3hLandscape L; C3hChunkInfo* info; DWORD t0;
    unsigned long long bytes[C3H_MAX_LOD] = {0}; unsigned bad = 0, texOk = 0;
    if (argc < 3) { printf("usage: c3htest c3helper.exe landscape_dir [log]\n"); return 2; }
    if (!C3h_Start(&c, argv[1], argc > 3 ? argv[3] : NULL, err, sizeof err)) { printf("start failed: %s\n", err); return 1; }
    printf("helper ready\n");
    t0 = GetTickCount();
    st = C3h_Request(&c, C3H_REQ_OPEN, 0, 0, argv[2], 600000);
    printf("OPEN status %d in %lu ms: %s\n", st, GetTickCount() - t0, c.hd->resp_msg);
    if (st != 0) { C3h_Stop(&c); return 1; }
    memcpy(&L, c.arena, sizeof L);
    info = (C3hChunkInfo*)malloc(L.n_chunks * sizeof *info); memcpy(info, c.arena + sizeof L, L.n_chunks * sizeof *info);
    printf("tiles %u x %u, tile %.0f m, %u chunks/side, %u chunks, %u textures, %u lods\n", L.tiles_x, L.tiles_y, L.tile_size, L.chunks_per_tile, L.n_chunks, L.n_textures, L.n_lods);
    t0 = GetTickCount();
    for (i = 0; i < (int)L.n_chunks; i++) for (l = 0; l < (int)L.n_lods; l++) {
        st = C3h_Request(&c, C3H_REQ_CHUNK, i, l, NULL, 10000);
        if (st == 0) {
            const C3hChunk* h = (const C3hChunk*)c.arena; const C3hPart* p = (const C3hPart*)(h + 1); unsigned k;
            const unsigned* idx = (const unsigned*)((const char*)(p + h->n_parts) + h->n_verts * sizeof(C3hVertex));
            size_t expect = sizeof *h + h->n_parts * sizeof *p + h->n_verts * sizeof(C3hVertex) + h->n_indices * 4;
            if (expect != c.hd->resp_size || c.hd->resp_size != info[i].bytes[l]) bad++;
            for (k = 0; k < h->n_indices; k++) if (idx[k] >= h->n_verts) { bad++; break; }
            bytes[l] += c.hd->resp_size;
        } else if (st != 1) bad++;
    }
    printf("chunks: %lu ms, MB per lod %llu %llu %llu, bad %u\n", GetTickCount() - t0, bytes[0] >> 20, bytes[1] >> 20, bytes[2] >> 20, bad);
    t0 = GetTickCount();
    for (i = 0; i < (int)L.n_textures; i++) for (l = 0; l < (int)L.n_lods; l++)
        if (C3h_Request(&c, C3H_REQ_TEXTURE, i, l, NULL, 10000) == 0 && !memcmp(c.arena, "DDS ", 4)) texOk++;
    printf("textures: %u of %u ok in %lu ms\n", texOk, L.n_textures * L.n_lods, GetTickCount() - t0);
    C3h_Stop(&c);
    printf("done\n");
    return bad ? 1 : 0;
}
