/* Carga do asset do remake, formato v5 (tools/remake/export_heat.py; little-endian; o v4 também é lido):
 *   char[4] "HTR5"; u32 versão (5), n_nodes, n_verts, n_tris, n_layers, tex_size, n_body, n_groups, n_pages
 *               (o v4 é "HTR4", versão 4, sem n_pages)
 *   por nó:      u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
 *   por grupo (1..n_groups): u8 posição na árvore, u8 pad, u16 nverts e u32 base_index do mesh do jogo
 *                desse gesto, u32 primeiro vértice, u32 nº de vértices (exclusivos do grupo, contíguos)
 *   por página (v5): s16 x, y na VRAM (x em halfwords, -1 = desconhecida), u16 largura, altura (texels),
 *                u8 bits por texel, u8 pad[3]
 *   por camada (v5): u8 página (0xFF = nenhuma)
 *   por vértice: RmkVert (8 bytes); os n_body primeiros são do corpo
 *   por face:    RmkTri (37 bytes), na ordem em que o jogo liga as primitivas na OT
 *   por camada:  tex_size x tex_size RGBA8 (Real-ESRGAN 4x, alfa 0 = transparente) */
#include "rmk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rmk_asset_free(RmkAsset* a) {
    free(a->verts);
    free(a->tris);
    free(a->layers);
    free(a->nmaps);
    memset(a, 0, sizeof *a);
}

/* Mapas de normal (espaço tangente, RGBA8: xyz * 0,5 + 0,5, alfa = máscara) gerados das camadas HD; a mesma
 * conta de tools/remake/detail.py (renders offline). A arte é anime com traço preto. Altura por texel:
 *   estampa colorida (saturação alta: chamas, logotipo) em relevo, caixa de raio 3 duas vezes;
 *   traço preto (contornos, costuras) como sulco, raio 2;
 *   sombreado pintado (dobras do pano) como ondulação, luminância com raio 8;
 * normal = normalize(-k dh/du, -k dh/dv, 1) por diferenças centrais; texel transparente: normal plana.
 * Roda uma vez na ativação (CPU). */
#define NM_PRINT 1.0f
#define NM_INK 0.8f
#define NM_FOLD 1.2f

/* caixa separável de raio r com borda repetida, aplicada em x e depois em y */
static void box(float* h, float* tmp, int S, int r) {
    const double inv = 1.0 / (2 * r + 1);
    for (int y = 0; y < S; y++) {
        const float* row = h + (size_t)y * S;
        double acc = 0.0;
        for (int k = -r; k <= r; k++) acc += row[k < 0 ? 0 : k >= S ? S - 1 : k];
        for (int x = 0; x < S; x++) {
            tmp[(size_t)y * S + x] = (float)(acc * inv);
            const int add = x + r + 1, sub = x - r;
            acc += row[add >= S ? S - 1 : add] - row[sub < 0 ? 0 : sub];
        }
    }
    for (int x = 0; x < S; x++) {
        double acc = 0.0;
        for (int k = -r; k <= r; k++) acc += tmp[(size_t)(k < 0 ? 0 : k >= S ? S - 1 : k) * S + x];
        for (int y = 0; y < S; y++) {
            h[(size_t)y * S + x] = (float)(acc * inv);
            const int add = y + r + 1, sub = y - r;
            acc += tmp[(size_t)(add >= S ? S - 1 : add) * S + x] - tmp[(size_t)(sub < 0 ? 0 : sub) * S + x];
        }
    }
}

int rmk_asset_build_normals(RmkAsset* a, float strength) {
    const int S = (int)a->tex_size;
    const size_t n = (size_t)S * S;
    free(a->nmaps);
    a->nmaps = a->n_layers ? (uint8_t*)malloc(n * 4u * a->n_layers) : NULL;
    float* h = (float*)malloc(n * sizeof(float));
    float* c = (float*)malloc(n * sizeof(float));
    float* tmp = (float*)malloc(n * sizeof(float));
    if (!a->nmaps || !h || !c || !tmp) { free(h); free(c); free(tmp); free(a->nmaps); a->nmaps = NULL; return 0; }
    static const float w[3] = {NM_PRINT, -NM_INK, NM_FOLD};
    static const int r[3] = {3, 2, 8};
    for (uint32_t l = 0; l < a->n_layers; l++) {
        const uint8_t* px = a->layers + n * 4u * l;
        memset(h, 0, n * sizeof(float));
        for (int ch = 0; ch < 3; ch++) {
            for (size_t i = 0; i < n; i++) {
                const uint8_t* q = &px[4 * i];
                float v = 0.0f;
                if (q[3]) {
                    const float Y = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
                    if (ch == 0) {
                        const int mx = q[0] > q[1] ? (q[0] > q[2] ? q[0] : q[2]) : (q[1] > q[2] ? q[1] : q[2]);
                        const int mn = q[0] < q[1] ? (q[0] < q[2] ? q[0] : q[2]) : (q[1] < q[2] ? q[1] : q[2]);
                        v = ((float)(mx - mn) / 255.0f - 0.25f) / 0.2f;
                    } else if (ch == 1) {
                        v = (0.30f - Y) / 0.22f;
                    } else {
                        v = Y;
                    }
                    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
                }
                c[i] = v;
            }
            box(c, tmp, S, r[ch]);
            box(c, tmp, S, r[ch]);
            for (size_t i = 0; i < n; i++) h[i] += w[ch] * c[i];
        }
        for (size_t i = 0; i < n; i++) if (!px[4 * i + 3]) h[i] = 0.0f;
        uint8_t* o = a->nmaps + n * 4u * l;
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                const size_t i = (size_t)y * S + x;
                const float du = h[(size_t)y * S + (x < S - 1 ? x + 1 : x)] - h[(size_t)y * S + (x > 0 ? x - 1 : x)];
                const float dv = h[(size_t)(y < S - 1 ? y + 1 : y) * S + x] - h[(size_t)(y > 0 ? y - 1 : y) * S + x];
                float nx = -strength * du, ny = -strength * dv, nz = 1.0f;
                const float il = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
                nx *= il; ny *= il; nz *= il;
                o[4 * i] = (uint8_t)(nx * 127.5f + 127.5f);
                o[4 * i + 1] = (uint8_t)(ny * 127.5f + 127.5f);
                o[4 * i + 2] = (uint8_t)(nz * 127.5f + 127.5f);
                o[4 * i + 3] = px[4 * i + 3];
            }
    }
    free(h); free(c); free(tmp);
    return 1;
}

int rmk_asset_load(RmkAsset* a, const char* path) {
    memset(a, 0, sizeof *a);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    uint32_t hdr[9] = {0};
    int ok = fread(magic, 1, 4, f) == 4 && (!memcmp(magic, "HTR4", 4) || !memcmp(magic, "HTR5", 4));
    const int v5 = ok && magic[3] == '5';
    ok = ok && fread(hdr, 4, v5 ? 9 : 8, f) == (size_t)(v5 ? 9 : 8) && hdr[0] == (v5 ? 5u : 4u);
    if (ok) {
        a->n_nodes = hdr[1]; a->n_verts = hdr[2]; a->n_tris = hdr[3]; a->n_layers = hdr[4]; a->tex_size = hdr[5];
        a->n_body = hdr[6]; a->n_groups = hdr[7]; a->n_pages = hdr[8];
        ok = a->n_nodes <= RMK_MAX_NODES && a->n_verts && a->n_verts < 65536 && a->n_tris && a->n_layers <= RMK_MAX_LAYERS &&
             (a->tex_size == 256 || a->tex_size == 512 || a->tex_size == 1024 || a->tex_size == 2048) &&
             a->n_body <= a->n_verts && a->n_groups < RMK_MAX_GROUPS && a->n_pages <= RMK_MAX_PAGES;
    }
    for (uint32_t i = 0; ok && i < a->n_nodes; i++) {
        uint16_t nv, pad;
        uint32_t base;
        ok = fread(&nv, 2, 1, f) == 1 && fread(&pad, 2, 1, f) == 1 && fread(&base, 4, 1, f) == 1;
        a->node_nverts[i] = nv;
        a->node_base[i] = base;
    }
    for (uint32_t g = 1; ok && g <= a->n_groups; g++) {
        uint8_t pos, pad;
        uint16_t nv;
        uint32_t w[3];
        ok = fread(&pos, 1, 1, f) == 1 && fread(&pad, 1, 1, f) == 1 && fread(&nv, 2, 1, f) == 1 && fread(w, 4, 3, f) == 3;
        RmkGroup* gr = &a->groups[g];
        gr->tree_pos = pos; gr->sig_nverts = nv; gr->sig_base = w[0]; gr->v0 = w[1]; gr->nv = w[2];
        ok = ok && gr->v0 >= a->n_body && gr->v0 + gr->nv <= a->n_verts && pos < RMK_MAX_NODES;
    }
    for (uint32_t p = 0; ok && p < a->n_pages; p++) {
        int16_t xy[2];
        uint16_t wh[2];
        uint8_t bpp_pad[4];
        ok = fread(xy, 2, 2, f) == 2 && fread(wh, 2, 2, f) == 2 && fread(bpp_pad, 1, 4, f) == 4;
        RmkPage* g = &a->pages[p];
        g->vx = xy[0]; g->vy = xy[1]; g->w = wh[0]; g->h = wh[1]; g->bpp = bpp_pad[0];
        ok = ok && (g->bpp == 4 || g->bpp == 8 || g->bpp == 16) && g->w && g->h;
    }
    memset(a->layer_page, RMK_NO_PAGE, sizeof a->layer_page);
    if (ok && v5) ok = fread(a->layer_page, 1, a->n_layers, f) == a->n_layers;
    for (uint32_t l = 0; ok && l < a->n_layers; l++) ok = a->layer_page[l] == RMK_NO_PAGE || a->layer_page[l] < a->n_pages;
    while (ok && a->mesh_nodes < (int)a->n_nodes && a->node_nverts[a->mesh_nodes]) a->mesh_nodes++;
    const size_t layer_bytes = (size_t)a->tex_size * a->tex_size * 4u;
    if (ok) {
        a->verts = (RmkVert*)malloc(sizeof(RmkVert) * a->n_verts);
        a->tris = (RmkTri*)malloc(sizeof(RmkTri) * a->n_tris);
        a->layers = a->n_layers ? (uint8_t*)malloc(layer_bytes * a->n_layers) : NULL;
        ok = a->verts && a->tris && (a->layers || !a->n_layers) &&
             fread(a->verts, sizeof(RmkVert), a->n_verts, f) == a->n_verts &&
             fread(a->tris, sizeof(RmkTri), a->n_tris, f) == a->n_tris &&
             (!a->n_layers || fread(a->layers, layer_bytes, a->n_layers, f) == a->n_layers);
    }
    for (uint32_t i = 0; ok && i < a->n_tris; i++) {
        const RmkTri* t = &a->tris[i];
        for (int k = 0; k < 3; k++) ok &= t->v[k] < a->n_verts;
        ok &= t->zp[0] < a->n_verts && t->zp[1] < a->n_verts && t->layer < (int)a->n_layers && t->group <= a->n_groups;
        /* uma face de gesto só usa vértices do corpo ou do próprio grupo */
        for (int k = 0; ok && k < 3 && t->group; k++) {
            const RmkGroup* g = &a->groups[t->group];
            ok = t->v[k] < a->n_body || (t->v[k] >= g->v0 && t->v[k] < g->v0 + g->nv);
        }
        if (ok && !t->group) for (int k = 0; k < 3; k++) ok &= t->v[k] < a->n_body;
    }
    for (uint32_t i = 0; ok && i < a->n_verts; i++) ok = a->verts[i].owner < a->n_nodes;
    fclose(f);
    if (!ok) rmk_asset_free(a);
    return ok;
}
