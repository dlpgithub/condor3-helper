// c3helper.exe - 64-bit data server for the Condor 3 mod (see include/c3h_proto.h, docs/DESIGN.md).
//
// Holds a landscape's complete autogen (Autogen/oXXYY.c3d + textures) in 64-bit memory, cut into chunks with several levels
// of detail, and hands single chunks / textures to the 32-bit game through shared memory. Started by the mod:
//     c3helper.exe --pid <Condor.exe pid> [--log <file>]
// The mod creates the shared objects (Local\c3h_<pid>_*) before starting us; we exit when the game exits or on C3H_REQ_QUIT.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "c3h_proto.h"

static FILE* g_log;
static void Log(const char* fmt, ...) {
    va_list ap; SYSTEMTIME t;
    if (!g_log) return;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

// ---- small helpers -------------------------------------------------------------------------------------------
typedef struct { uint8_t* p; size_t n; } Buf;
static Buf ReadFileAll(const char* path) {
    Buf b = {0, 0}; FILE* f = fopen(path, "rb"); long long n;
    if (!f) return b;
    _fseeki64(f, 0, SEEK_END); n = _ftelli64(f); _fseeki64(f, 0, SEEK_SET);
    b.p = (uint8_t*)malloc((size_t)n + 1);
    if (b.p && fread(b.p, 1, (size_t)n, f) == (size_t)n) b.n = (size_t)n; else { free(b.p); b.p = NULL; }
    fclose(f);
    return b;
}
typedef struct { uint8_t* p; size_t n, cap; } Out;
static void* OutAdd(Out* o, const void* src, size_t n) {
    void* dst;
    if (o->n + n > o->cap) { size_t c = o->cap ? o->cap * 2 : 65536; while (c < o->n + n) c *= 2; o->p = (uint8_t*)realloc(o->p, c); o->cap = c; }
    dst = o->p + o->n;
    if (src) memcpy(dst, src, n); else memset(dst, 0, n);
    o->n += n;
    return dst;
}

// ---- C3D -----------------------------------------------------------------------------------------------------
typedef struct { char name[32], tex[260]; uint32_t fv, nv, fi, ni; float color[4], params[3]; } C3dObj;
typedef struct { int nobj; C3dObj* obj; uint32_t nverts, nidx; C3hVertex* v; uint32_t* idx; Buf file; } C3d;

static int ParseC3d(C3d* m, Buf b) {
    size_t o = 3; uint32_t ng, i; const uint8_t* d = b.p;
    memset(m, 0, sizeof *m); m->file = b;
#define NEED(k) do { if (o + (k) > b.n) return 0; } while (0)
    if (b.n < 7 || memcmp(d, "C3D", 3)) return 0;
    NEED(4); memcpy(&ng, d + o, 4); o += 4 + 8 * (size_t)ng;
    NEED(4); memcpy(&i, d + o, 4); o += 4; m->nobj = (int)i;
    m->obj = (C3dObj*)calloc(m->nobj ? m->nobj : 1, sizeof(C3dObj));
    for (i = 0; i < (uint32_t)m->nobj; i++) {
        C3dObj* ob = &m->obj[i]; uint8_t l;
        NEED(1); l = d[o++]; NEED(l); memcpy(ob->name, d + o, l < 31 ? l : 31); o += l;
        NEED(16); memcpy(&ob->fv, d + o, 4); memcpy(&ob->nv, d + o + 4, 4); memcpy(&ob->fi, d + o + 8, 4); memcpy(&ob->ni, d + o + 12, 4); o += 16;
        NEED(1); l = d[o++]; NEED(l); memcpy(ob->tex, d + o, l); o += l;
        NEED(28); memcpy(ob->color, d + o, 16); memcpy(ob->params, d + o + 16, 12); o += 28;
    }
    NEED(4); memcpy(&m->nverts, d + o, 4); o += 4; NEED((size_t)m->nverts * 32); m->v = (C3hVertex*)(d + o); o += (size_t)m->nverts * 32;
    NEED(4); memcpy(&m->nidx, d + o, 4); o += 4; NEED((size_t)m->nidx * 4); m->idx = (uint32_t*)(d + o);
#undef NEED
    for (i = 0; i < (uint32_t)m->nobj; i++) {
        C3dObj* ob = &m->obj[i];
        if (ob->fi + ob->ni > m->nidx) return 0;
    }
    return 1;
}

// ---- landscape state -----------------------------------------------------------------------------------------
#define LODS 3
static const float kLodCell[LODS] = { 0.0f, 8.0f, 24.0f };      // vertex clustering cell per LOD (metres)
static const int   kTexLod[LODS]  = { 0, 1, 3 };                     // mip levels dropped per LOD (2048 -> 1024 -> 256)
static const float kUvQuant[LODS] = { 0.0f, 16.0f, 6.0f };                 // texture-coordinate grouping per LOD (atlas fraction)

typedef struct { Buf blob[LODS]; } ChunkData;                    // ready-to-send C3hChunk answers
static char       g_dir[MAX_PATH];                                // landscape folder
static uint32_t   g_tx, g_ty, g_cpt = 4;                          // tiles, chunks per tile side
static float      g_tile = 5760.0f;
static C3hChunkInfo* g_info; static ChunkData* g_chunks; static uint32_t g_nchunks;
static char     (*g_tex)[260]; static uint32_t g_ntex;           // texture paths relative to Autogen/
static Buf*       g_texfile;                                      // loaded on demand

static int TexId(const char* t) {
    uint32_t i;
    if (!t[0]) return -1;
    for (i = 0; i < g_ntex; i++) if (!_stricmp(g_tex[i], t)) return (int)i;
    g_tex = realloc(g_tex, (g_ntex + 1) * sizeof *g_tex); strcpy(g_tex[g_ntex], t);
    g_texfile = realloc(g_texfile, (g_ntex + 1) * sizeof *g_texfile); memset(&g_texfile[g_ntex], 0, sizeof(Buf));
    return (int)g_ntex++;
}

// open-addressing map uint64 key -> int32 value (vertex clustering)
typedef struct { uint64_t* k; int32_t* v; size_t cap; } Map;
static void MapInit(Map* m, size_t n) { m->cap = 64; while (m->cap < n * 2) m->cap <<= 1; m->k = malloc(m->cap * 8); m->v = malloc(m->cap * 4); memset(m->k, 0xff, m->cap * 8); }
static void MapFree(Map* m) { free(m->k); free(m->v); }
static int32_t* MapSlot(Map* m, uint64_t key, int* isNew) {
    size_t h = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 20) & (m->cap - 1);
    while (m->k[h] != ~0ull && m->k[h] != key) h = (h + 1) & (m->cap - 1);
    *isNew = m->k[h] == ~0ull; m->k[h] = key;
    return &m->v[h];
}

// Builds the answer for one chunk at one LOD from triangle lists per object (tri = index of the first index of a triangle).
typedef struct { uint32_t* t; uint32_t n, cap; } TriList;
static uint32_t BuildChunk(Buf* out, const C3d* m, int tx, int ty, int lod, TriList* perObj, float bmin[3], float bmax[3]) {
    Out o = {0}; C3hChunk* h; uint32_t nparts = 0, nv = 0, ni = 0, tris = 0; int ob;
    C3hPart* parts; Out verts = {0}, idx = {0};
    float cell = kLodCell[lod];
    parts = calloc(m->nobj ? m->nobj : 1, sizeof(C3hPart));
    for (ob = 0; ob < m->nobj; ob++) {
        TriList* tl = &perObj[ob]; Map map; uint32_t k, firstV = nv, firstI = ni;
        float* acc = NULL; uint32_t nacc = 0, cap = 0;
        if (!tl->n) continue;
        MapInit(&map, (size_t)tl->n * 3);
        for (k = 0; k < tl->n; k++) {
            uint32_t c, out3[3]; int j;
            for (j = 0; j < 3; j++) {
                uint32_t vi = m->idx[tl->t[k] + j]; const C3hVertex* v = &m->v[vi]; uint64_t key; int isNew; int32_t* s;
                if (lod == 0) key = vi;
                else {
                    int64_t qx = (int64_t)floorf(v->pos[0] / cell) + 4096, qy = (int64_t)floorf(v->pos[1] / cell) + 4096;
                    int64_t qz = (int64_t)floorf(v->pos[2] / cell) + 1024;
                    int64_t qu = (int64_t)floorf(v->uv[0] * kUvQuant[lod]) & 255, qv = (int64_t)floorf(v->uv[1] * kUvQuant[lod]) & 255;
                    key = ((uint64_t)(qx & 0x3fff) << 42) | ((uint64_t)(qy & 0x3fff) << 28) | ((uint64_t)(qz & 0xfff) << 16) | ((uint64_t)qu << 8) | (uint64_t)qv;
                }
                s = MapSlot(&map, key, &isNew);
                if (isNew) {
                    *s = (int32_t)nacc;
                    if (nacc == cap) { cap = cap ? cap * 2 : 1024; acc = realloc(acc, (size_t)cap * 9 * sizeof(float)); }
                    memset(acc + (size_t)nacc * 9, 0, 9 * sizeof(float)); nacc++;
                }
                {   float* a = acc + (size_t)*s * 9;
                    a[0] += v->pos[0]; a[1] += v->pos[1]; a[2] += v->pos[2]; a[3] += v->normal[0]; a[4] += v->normal[1]; a[5] += v->normal[2];
                    a[6] += v->uv[0]; a[7] += v->uv[1]; a[8] += 1.0f; }
                out3[j] = (uint32_t)*s;
            }
            if (out3[0] == out3[1] || out3[1] == out3[2] || out3[0] == out3[2]) continue;    // collapsed by clustering
            for (c = 0; c < 3; c++) { uint32_t g = firstV + out3[c]; OutAdd(&idx, &g, 4); }
            ni += 3; tris++;
        }
        for (k = 0; k < nacc; k++) {   // cluster averages -> vertices
            float* a = acc + (size_t)k * 9; C3hVertex v; float w = 1.0f / a[8], l;
            v.pos[0] = a[0] * w; v.pos[1] = a[1] * w; v.pos[2] = a[2] * w;
            l = sqrtf(a[3] * a[3] + a[4] * a[4] + a[5] * a[5]); if (l < 1e-6f) { a[3] = 0; a[4] = 0; a[5] = 1; l = 1; }
            v.normal[0] = a[3] / l; v.normal[1] = a[4] / l; v.normal[2] = a[5] / l;
            v.uv[0] = a[6] * w; v.uv[1] = a[7] * w;
            OutAdd(&verts, &v, sizeof v);
            if (lod == 0) { int q; for (q = 0; q < 3; q++) { if (v.pos[q] < bmin[q]) bmin[q] = v.pos[q]; if (v.pos[q] > bmax[q]) bmax[q] = v.pos[q]; } }
        }
        nv += nacc;
        free(acc); MapFree(&map);
        if (ni == firstI) { nv = firstV; verts.n = (size_t)firstV * sizeof(C3hVertex); continue; }
        {   C3hPart* p = &parts[nparts++]; const C3dObj* so = &m->obj[ob];
            p->first_vertex = firstV; p->vertex_count = nv - firstV; p->first_index = firstI; p->index_count = ni - firstI;
            p->texture = TexId(so->tex); memcpy(p->color, so->color, 16); memcpy(p->params, so->params, 12);
            strncpy(p->name, so->name, 31); }
    }
    if (nparts) {
        h = (C3hChunk*)OutAdd(&o, NULL, sizeof *h);
        h->tile_x = (int16_t)tx; h->tile_y = (int16_t)ty; h->lod = (uint32_t)lod; h->n_parts = nparts; h->n_verts = nv; h->n_indices = ni;
        OutAdd(&o, parts, nparts * sizeof(C3hPart)); OutAdd(&o, verts.p, verts.n); OutAdd(&o, idx.p, idx.n);
    }
    free(parts); free(verts.p); free(idx.p);
    out->p = o.p; out->n = o.n;
    return tris;
}

static int OpenLandscape(const char* dir, char* msg) {
    char p[MAX_PATH * 2]; Buf trn, oha; uint32_t w, h; float cell; DWORD t0 = GetTickCount();
    uint64_t totTris[LODS] = {0}, totBytes[LODS] = {0};
    strncpy(g_dir, dir, sizeof g_dir - 1);
    // a new OPEN replaces everything
    { uint32_t i, l; for (i = 0; i < g_nchunks; i++) for (l = 0; l < LODS; l++) free(g_chunks[i].blob[l].p);
      for (i = 0; i < g_ntex; i++) free(g_texfile[i].p);
      free(g_chunks); free(g_info); free(g_tex); free(g_texfile); g_chunks = NULL; g_info = NULL; g_tex = NULL; g_texfile = NULL; g_nchunks = g_ntex = 0; }
    { const char* nm = strrchr(dir, '\\'); nm = nm ? nm + 1 : dir;
      _snprintf(p, sizeof p, "%s\\%s.trn", dir, nm); trn = ReadFileAll(p);
      if (trn.n < 12) { sprintf(msg, "cannot read %s", p); return -1; }
      memcpy(&w, trn.p, 4); memcpy(&h, trn.p + 4, 4); memcpy(&cell, trn.p + 8, 4); free(trn.p);
      g_tx = w / 64; g_ty = h / 64; g_tile = 64.0f * cell;
      _snprintf(p, sizeof p, "%s\\%s.oha", dir, nm); oha = ReadFileAll(p); }
    if (!oha.p) { sprintf(msg, "no .oha: no autogen"); g_nchunks = 0; return 0; }
    oha.p[oha.n] = 0;
    {   char* line = strtok((char*)oha.p, "\r\n");
        for (; line; line = strtok(NULL, "\r\n")) {
            int key, tx, ty, ci, cj, l, n = g_cpt; char val[64] = ""; C3d m; Buf f; TriList* lists; uint32_t t;
            if (sscanf(line, "%d %63s", &key, val) < 2 || !val[0]) continue;
            tx = key / 1000; ty = key % 1000;
            _snprintf(p, sizeof p, "%s\\Autogen\\o%03d%03d.c3d", dir, tx, ty); f = ReadFileAll(p);
            if (!f.p) { _snprintf(p, sizeof p, "%s\\Autogen\\o%02d%02d.c3d", dir, tx, ty); f = ReadFileAll(p); }
            if (!f.p || !ParseC3d(&m, f)) { Log("tile %d %d: cannot read/parse %s", tx, ty, p); free(f.p); continue; }
            // triangle lists per chunk and object (by centroid)
            lists = calloc((size_t)n * n * (m.nobj ? m.nobj : 1), sizeof(TriList));
            {   int ob;
                for (ob = 0; ob < m.nobj; ob++) {
                    const C3dObj* so = &m.obj[ob];
                    for (t = so->fi; t + 2 < so->fi + so->ni; t += 3) {
                        const float* a = m.v[m.idx[t]].pos; const float* b = m.v[m.idx[t + 1]].pos; const float* c = m.v[m.idx[t + 2]].pos;
                        float x = (a[0] + b[0] + c[0]) / 3.0f, y = (a[1] + b[1] + c[1]) / 3.0f; TriList* tl;
                        ci = (int)floorf((x + g_tile * 0.5f) / (g_tile / n)); cj = (int)floorf((y + g_tile * 0.5f) / (g_tile / n));
                        if (ci < 0) ci = 0; if (ci >= n) ci = n - 1; if (cj < 0) cj = 0; if (cj >= n) cj = n - 1;
                        if (m.idx[t] >= m.nverts || m.idx[t + 1] >= m.nverts || m.idx[t + 2] >= m.nverts) continue;
                        tl = &lists[((size_t)ci * n + cj) * m.nobj + ob];
                        if (tl->n == tl->cap) { tl->cap = tl->cap ? tl->cap * 2 : 256; tl->t = realloc(tl->t, tl->cap * 4); }
                        tl->t[tl->n++] = t;
                    }
                }
            }
            for (ci = 0; ci < n; ci++) for (cj = 0; cj < n; cj++) {
                TriList* per = &lists[((size_t)ci * n + cj) * m.nobj]; C3hChunkInfo inf; ChunkData cd; int any = 0, ob;
                for (ob = 0; ob < m.nobj; ob++) any |= per[ob].n != 0;
                if (!any) continue;
                memset(&inf, 0, sizeof inf); memset(&cd, 0, sizeof cd);
                inf.tile_x = (int16_t)tx; inf.tile_y = (int16_t)ty; inf.cx = (uint16_t)ci; inf.cy = (uint16_t)cj;
                inf.min[0] = inf.min[1] = inf.min[2] = 1e30f; inf.max[0] = inf.max[1] = inf.max[2] = -1e30f;
                for (l = 0; l < LODS; l++) {
                    inf.tris[l] = BuildChunk(&cd.blob[l], &m, tx, ty, l, per, inf.min, inf.max);
                    inf.bytes[l] = (uint32_t)cd.blob[l].n; totTris[l] += inf.tris[l]; totBytes[l] += cd.blob[l].n;
                    if (cd.blob[l].n > C3H_ARENA_SIZE) { Log("chunk %d %d %d %d lod %d too large (%zu bytes), dropped", tx, ty, ci, cj, l, cd.blob[l].n); free(cd.blob[l].p); cd.blob[l].p = NULL; cd.blob[l].n = 0; inf.bytes[l] = 0; }
                }
                g_info = realloc(g_info, (g_nchunks + 1) * sizeof *g_info); g_chunks = realloc(g_chunks, (g_nchunks + 1) * sizeof *g_chunks);
                g_info[g_nchunks] = inf; g_chunks[g_nchunks] = cd; g_nchunks++;
            }
            for (t = 0; t < (uint32_t)(n * n * m.nobj); t++) free(lists[t].t);
            free(lists); free(m.obj); free(f.p);
            Log("tile %d %d: %u objects, %u vertices, %u triangles", tx, ty, (unsigned)m.nobj, m.nverts, m.nidx / 3);
        }
    }
    free(oha.p);
    sprintf(msg, "%u chunks, %u textures; triangles lod0 %llu lod1 %llu lod2 %llu; MB %llu / %llu / %llu; %lu ms", g_nchunks, g_ntex,
            (unsigned long long)totTris[0], (unsigned long long)totTris[1], (unsigned long long)totTris[2],
            (unsigned long long)(totBytes[0] >> 20), (unsigned long long)(totBytes[1] >> 20), (unsigned long long)(totBytes[2] >> 20),
            (unsigned long)(GetTickCount() - t0));
    return 0;
}

// DDS with the first <drop> mip levels removed (DXT1/3/5 or 32-bit uncompressed, no DX10 header).
static size_t MakeTexture(uint8_t* dst, size_t cap, int id, int drop, char* msg) {
    Buf* f; uint32_t hgt, wid, mips, pfFlags, fourcc, bits; size_t off = 128, sz; int bs = 0, i;
    if (id < 0 || (uint32_t)id >= g_ntex) { sprintf(msg, "bad texture id %d", id); return 0; }
    f = &g_texfile[id];
    if (!f->p) {
        char p[MAX_PATH * 2]; _snprintf(p, sizeof p, "%s\\Autogen\\%s", g_dir, g_tex[id]);
        *f = ReadFileAll(p);
        if (!f->p) { sprintf(msg, "cannot read %s", p); return 0; }
    }
    if (f->n < 128 || memcmp(f->p, "DDS ", 4)) { sprintf(msg, "not a DDS: %s", g_tex[id]); return 0; }
    memcpy(&hgt, f->p + 12, 4); memcpy(&wid, f->p + 16, 4); memcpy(&mips, f->p + 28, 4);
    memcpy(&pfFlags, f->p + 80, 4); memcpy(&fourcc, f->p + 84, 4); memcpy(&bits, f->p + 88, 4);
    if (mips < 1) mips = 1;
    if (pfFlags & 4) { if (fourcc == 0x31545844) bs = 8; else if (fourcc == 0x33545844 || fourcc == 0x35545844) bs = 16; }
    else if (bits != 32) { sprintf(msg, "unsupported DDS format in %s", g_tex[id]); return 0; }
    if (drop >= (int)mips) drop = (int)mips - 1;
    for (i = 0; i < drop; i++) {
        uint32_t w = wid >> i, h = hgt >> i; if (!w) w = 1; if (!h) h = 1;
        off += bs ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * bs : (size_t)w * h * 4;
    }
    if (off > f->n) { sprintf(msg, "truncated DDS %s", g_tex[id]); return 0; }
    sz = 128 + (f->n - off);
    if (sz > cap) { sprintf(msg, "texture too large"); return 0; }
    memcpy(dst, f->p, 128); memcpy(dst + 128, f->p + off, f->n - off);
    {   uint32_t w = wid >> drop, h = hgt >> drop, m = mips - drop, lin; if (!w) w = 1; if (!h) h = 1;
        lin = bs ? ((w + 3) / 4) * ((h + 3) / 4) * bs : w * 4;
        memcpy(dst + 12, &h, 4); memcpy(dst + 16, &w, 4); memcpy(dst + 28, &m, 4); memcpy(dst + 20, &lin, 4); }
    return sz;
}

int main(int argc, char** argv) {
    DWORD pid = 0; char name[64], logp[MAX_PATH] = ""; int i;
    HANDLE map, evReq, evDone, game; C3hHeader* hd; uint8_t* arena;
    for (i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--pid")) pid = (DWORD)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--log")) strncpy(logp, argv[++i], sizeof logp - 1);
    }
    if (logp[0]) g_log = fopen(logp, "w");
    Log("c3helper %s, 64-bit=%d, pid arg %lu", __DATE__, (int)(sizeof(void*) == 8), (unsigned long)pid);
    if (!pid) { Log("no --pid"); return 2; }
    _snprintf(name, sizeof name, "Local\\c3h_%lu_shm", (unsigned long)pid);  map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    _snprintf(name, sizeof name, "Local\\c3h_%lu_req", (unsigned long)pid);  evReq = OpenEventA(EVENT_ALL_ACCESS, FALSE, name);
    _snprintf(name, sizeof name, "Local\\c3h_%lu_done", (unsigned long)pid); evDone = OpenEventA(EVENT_ALL_ACCESS, FALSE, name);
    game = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!map || !evReq || !evDone) { Log("cannot open shared objects (map %p req %p done %p, err %lu)", map, evReq, evDone, GetLastError()); return 3; }
    hd = (C3hHeader*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!hd || hd->magic != C3H_MAGIC || hd->version != C3H_VERSION) { Log("shared header mismatch (%p)", (void*)hd); return 4; }
    arena = (uint8_t*)hd + hd->header_size;
    hd->helper_ready = 1;
    Log("serving (arena %u MB, game handle %p)", hd->arena_size >> 20, game);
    for (;;) {
        HANDLE hs[2]; DWORD w; int n = 0;
        hs[n++] = evReq; if (game) hs[n++] = game;
        w = WaitForMultipleObjects(n, hs, FALSE, 5000);
        if (w == WAIT_OBJECT_0 + 1) { Log("game exited"); break; }
        if (w != WAIT_OBJECT_0) continue;
        if (hd->done_seq == hd->req_seq) continue;
        hd->status = 0; hd->resp_size = 0; hd->resp_msg[0] = 0;
        switch (hd->req_type) {
        case C3H_REQ_OPEN: {
            char dir[sizeof hd->req_path]; memcpy(dir, hd->req_path, sizeof dir); dir[sizeof dir - 1] = 0;
            Log("OPEN %s", dir);
            hd->status = OpenLandscape(dir, hd->resp_msg);
            if (hd->status == 0) {
                C3hLandscape L; size_t need = sizeof L + (size_t)g_nchunks * sizeof(C3hChunkInfo);
                memset(&L, 0, sizeof L);
                L.tiles_x = g_tx; L.tiles_y = g_ty; L.tile_size = g_tile; L.chunks_per_tile = g_cpt; L.n_chunks = g_nchunks; L.n_textures = g_ntex;
                L.n_lods = LODS; memcpy(L.lod_cell, kLodCell, sizeof kLodCell);
                if (need > hd->arena_size) { hd->status = -1; sprintf(hd->resp_msg, "chunk table too large"); }
                else { memcpy(arena, &L, sizeof L); memcpy(arena + sizeof L, g_info, (size_t)g_nchunks * sizeof(C3hChunkInfo)); hd->resp_size = (uint32_t)need; }
            }
            Log("OPEN -> %d: %s", hd->status, hd->resp_msg);
            break; }
        case C3H_REQ_CHUNK: {
            int c = hd->req_arg[0], l = hd->req_arg[1];
            if (c < 0 || (uint32_t)c >= g_nchunks || l < 0 || l >= LODS) { hd->status = -1; sprintf(hd->resp_msg, "bad chunk %d lod %d", c, l); break; }
            if (!g_chunks[c].blob[l].n) { hd->status = 1; break; }
            memcpy(arena, g_chunks[c].blob[l].p, g_chunks[c].blob[l].n); hd->resp_size = (uint32_t)g_chunks[c].blob[l].n;
            break; }
        case C3H_REQ_TEXTURE: {
            int l = hd->req_arg[1]; size_t n;
            if (l < 0) l = 0; if (l >= LODS) l = LODS - 1;
            n = MakeTexture(arena, hd->arena_size, hd->req_arg[0], kTexLod[l], hd->resp_msg);
            if (!n) { hd->status = -1; Log("TEXTURE %d: %s", hd->req_arg[0], hd->resp_msg); }
            hd->resp_size = (uint32_t)n;
            break; }
        case C3H_REQ_STATS: {
            sprintf(hd->resp_msg, "chunks %u textures %u", g_nchunks, g_ntex);
            break; }
        case C3H_REQ_QUIT:
            Log("QUIT");
            hd->done_seq = hd->req_seq; SetEvent(evDone);
            return 0;
        default:
            hd->status = -1; sprintf(hd->resp_msg, "unknown request %u", hd->req_type);
        }
        MemoryBarrier();
        hd->done_seq = hd->req_seq;
        SetEvent(evDone);
    }
    hd->helper_ready = 0;
    return 0;
}
