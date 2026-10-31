/* Carga do asset do remake, formato v7 (tools/remake/export_char.py; little-endian; v4..v6 também são lidos):
 *   char[4] "HTR7"; u32 versão (7), n_nodes, n_verts, n_tris, n_layers, tex_size, n_body, n_groups, n_pages,
 *               índice do personagem; char nome[16]
 *               (v5: "HTR5", versão 5, sem índice nem nome; v4: "HTR4", versão 4, sem n_pages)
 *   por nó:      u16 nverts (0 = sem mesh), u8 páginas das faces originais (v6; pad antes), u8 1 = nó desenhado
 *                (v6: os primeiros n_drawn, na ordem da árvore; pad antes), u32 base_index (MeshHeader+0x98)
 *   por grupo (1..n_groups): u8 posição na árvore (v7: 255 = peça sem posição), u8 pad, u16 nverts e u32
 *                base_index do mesh do jogo desse gesto, u32 primeiro vértice, u32 nº de vértices (exclusivos do grupo,
 *                contíguos; v7: com o osso próprio n_drawn + g - 1, os do v6 com o da posição)
 *   por página (v5): s16 x, y na VRAM (x em halfwords, -1 = desconhecida), u16 largura, altura (texels),
 *                u8 bits por texel, u8 pad[3]; v6: + u32 FNV-1a de 64 palavras da imagem do TIM e u32 FNV-1a da
 *                CLUT inteira
 *   por camada (v5): u8 página (0xFF = nenhuma)
 *   por vértice: RmkVert (8 bytes); os n_body primeiros são do corpo
 *   por face:    RmkTri (37 bytes), na ordem em que o jogo liga as primitivas na OT
 *   por camada:  tex_size x tex_size RGBA8 (Real-ESRGAN 4x, alfa 0 = transparente, 143 = cor com STP, 255 = opaco)
 * Na carga o alfa vira só cobertura (STP -> 255) e o STP vai para um bit por texel (a->stp); o mapa de normal leva
 * o STP no alfa para o shader (semitransparência das faces RMK_MODE_SEMI). */
#include "rmk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rmk_asset_free(RmkAsset* a) {
    free(a->verts);
    free(a->tris);
    free(a->layers);
    free(a->stp);
    free(a->nmaps);
    free(a->mips);
    free(a->nmips);
    memset(a, 0, sizeof *a);
}

int rmk_mip_levels(uint32_t s) {
    int n = 0;
    while (s > 1) { s >>= 1; n++; }
    return n;
}

size_t rmk_mip_offset(uint32_t S, uint32_t nl, int level, uint32_t layer) {
    size_t off = 0;
    for (int l = 1; l < level; l++) { const uint32_t s = S >> l; off += (size_t)s * s * 4u * nl; }
    const uint32_t s = S >> level;
    return off + (size_t)s * s * 4u * layer;
}

/* média 2x2 por canal, RGBA8 (o que o glGenerateMipmap faz com alfa não pré-multiplicado) */
static void mip_box(const uint8_t* src, uint32_t s, uint8_t* dst) {
    const uint32_t d = s >> 1;
    for (uint32_t y = 0; y < d; y++) {
        const uint8_t* r0 = src + (size_t)(2 * y) * s * 4u;
        const uint8_t* r1 = r0 + (size_t)s * 4u;
        uint8_t* o = dst + (size_t)y * d * 4u;
        for (uint32_t x = 0; x < d; x++, r0 += 8, r1 += 8, o += 4)
            for (int c = 0; c < 4; c++) o[c] = (uint8_t)((r0[c] + r0[4 + c] + r1[c] + r1[4 + c] + 2u) >> 2);
    }
}

static uint8_t* build_chain(const uint8_t* base, uint32_t S, uint32_t nl, int L) {
    uint8_t* buf = (uint8_t*)malloc(rmk_mip_offset(S, nl, L, nl));
    if (!buf) return NULL;
    const size_t layer_bytes = (size_t)S * S * 4u;
    for (uint32_t j = 0; j < nl; j++) {
        const uint8_t* prev = base + layer_bytes * j;
        uint32_t ps = S;
        for (int l = 1; l <= L; l++) {
            uint8_t* dst = buf + rmk_mip_offset(S, nl, l, j);
            mip_box(prev, ps, dst);
            prev = dst; ps >>= 1;
        }
    }
    return buf;
}

int rmk_asset_build_mips(RmkAsset* a) {
    const int L = rmk_mip_levels(a->tex_size);
    free(a->mips); a->mips = NULL;
    free(a->nmips); a->nmips = NULL;
    if (!a->n_layers || L == 0 || !a->layers) return 1;
    a->mips = build_chain(a->layers, a->tex_size, a->n_layers, L);
    if (!a->mips) return 0;
    if (a->nmaps) {
        a->nmips = build_chain(a->nmaps, a->tex_size, a->n_layers, L);
        if (!a->nmips) { free(a->mips); a->mips = NULL; return 0; }
    }
    return 1;
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
        const uint8_t* stp = a->stp ? a->stp + n / 8u * l : NULL;
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
                o[4 * i + 3] = stp && ((stp[i >> 3] >> (i & 7)) & 1u) ? 255 : 0;   /* STP */
            }
    }
    free(h); free(c); free(tmp);
    return 1;
}

/* Cabeçalho e tabelas; deixa `f` no começo dos vértices. */
static int read_tables(RmkAsset* a, FILE* f) {
    char magic[4];
    uint32_t hdr[10] = {0};
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "HTR", 3) && magic[3] >= '4' && magic[3] <= '7';
    const int ver = ok ? magic[3] - '0' : 0, nh = ver >= 6 ? 10 : ver == 5 ? 9 : 8;
    ok = ok && fread(hdr, 4, (size_t)nh, f) == (size_t)nh && hdr[0] == (uint32_t)ver;
    if (ok) {
        a->version = (uint32_t)ver;
        a->n_nodes = hdr[1]; a->n_verts = hdr[2]; a->n_tris = hdr[3]; a->n_layers = hdr[4]; a->tex_size = hdr[5];
        a->n_body = hdr[6]; a->n_groups = hdr[7]; a->n_pages = ver >= 5 ? hdr[8] : 0;
        a->char_index = ver >= 6 ? hdr[9] : 3u;   /* v4/v5: só existia o HEAT */
        if (ver >= 6) ok = fread(a->name, 1, 16, f) == 16;
        else memcpy(a->name, "HEAT", 5);
        a->name[15] = 0;
        ok = ok && a->n_nodes <= RMK_MAX_NODES && a->n_verts && a->n_verts < 65536 && a->n_tris &&
             a->n_layers <= RMK_MAX_LAYERS &&
             (a->tex_size == 256 || a->tex_size == 512 || a->tex_size == 1024 || a->tex_size == 2048) &&
             a->n_body <= a->n_verts && a->n_groups < RMK_MAX_GROUPS && a->n_pages <= RMK_MAX_PAGES;
    }
    for (uint32_t i = 0; ok && i < a->n_nodes; i++) {
        uint16_t nv;
        uint8_t pg[2];
        uint32_t base;
        ok = fread(&nv, 2, 1, f) == 1 && fread(pg, 1, 2, f) == 2 && fread(&base, 4, 1, f) == 1;
        a->node_nverts[i] = nv;
        a->node_pages[i] = ver >= 6 ? pg[0] : 0;
        a->node_base[i] = base;
        if (ver >= 6 && pg[1]) a->n_drawn = i + 1;
    }
    if (ver < 6) a->n_drawn = 20;   /* v4/v5: só o HEAT, nós 0..19 desenhados */
    for (uint32_t g = 1; ok && g <= a->n_groups; g++) {
        uint8_t pos, pad;
        uint16_t nv;
        uint32_t w[3];
        ok = fread(&pos, 1, 1, f) == 1 && fread(&pad, 1, 1, f) == 1 && fread(&nv, 2, 1, f) == 1 && fread(w, 4, 3, f) == 3;
        RmkGroup* gr = &a->groups[g];
        gr->tree_pos = pos; gr->sig_nverts = nv; gr->sig_base = w[0]; gr->v0 = w[1]; gr->nv = w[2];
        ok = ok && gr->v0 >= a->n_body && gr->v0 + gr->nv <= a->n_verts &&
             (pos < RMK_MAX_NODES || (ver >= 7 && pos == RMK_NO_POS));
    }
    a->n_bones = ver >= 7 ? a->n_drawn + (int)a->n_groups : a->n_drawn;
    ok = ok && a->n_bones <= RMK_MAX_NODES;
    for (uint32_t p = 0; ok && p < a->n_pages; p++) {
        int16_t xy[2];
        uint16_t wh[2];
        uint8_t bpp_pad[4];
        uint32_t hash[2] = {0, 0};
        ok = fread(xy, 2, 2, f) == 2 && fread(wh, 2, 2, f) == 2 && fread(bpp_pad, 1, 4, f) == 4 &&
             (ver < 6 || fread(hash, 4, 2, f) == 2);
        RmkPage* g = &a->pages[p];
        g->vx = xy[0]; g->vy = xy[1]; g->w = wh[0]; g->h = wh[1]; g->bpp = bpp_pad[0];
        g->hash = hash[0]; g->clut_hash = hash[1];
        ok = ok && (g->bpp == 4 || g->bpp == 8 || g->bpp == 16) && g->w && g->h;
    }
    memset(a->layer_page, RMK_NO_PAGE, sizeof a->layer_page);
    if (ok && ver >= 5) ok = fread(a->layer_page, 1, a->n_layers, f) == a->n_layers;
    for (uint32_t l = 0; ok && l < a->n_layers; l++) ok = a->layer_page[l] == RMK_NO_PAGE || a->layer_page[l] < a->n_pages;
    while (ok && a->mesh_nodes < (int)a->n_nodes && a->node_nverts[a->mesh_nodes]) a->mesh_nodes++;
    return ok;
}

int rmk_asset_peek(RmkAsset* a, const char* path) {
    memset(a, 0, sizeof *a);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    const int ok = read_tables(a, f);
    fclose(f);
    if (!ok) memset(a, 0, sizeof *a);
    return ok;
}

int rmk_asset_load(RmkAsset* a, const char* path) {
    memset(a, 0, sizeof *a);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    int ok = read_tables(a, f);
    const size_t layer_bytes = (size_t)a->tex_size * a->tex_size * 4u;
    if (ok) {
        a->verts = (RmkVert*)malloc(sizeof(RmkVert) * a->n_verts);
        a->tris = (RmkTri*)malloc(sizeof(RmkTri) * a->n_tris);
        a->layers = a->n_layers ? (uint8_t*)malloc(layer_bytes * a->n_layers) : NULL;
        a->stp = a->n_layers ? (uint8_t*)calloc(layer_bytes / 32u, a->n_layers) : NULL;
        ok = a->verts && a->tris && (a->layers || !a->n_layers) && (a->stp || !a->n_layers) &&
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
    const uint32_t nb = a->version >= 7 ? (uint32_t)a->n_bones : a->n_nodes;   /* v7: ossos próprios dos grupos */
    for (uint32_t i = 0; ok && i < a->n_verts; i++) ok = a->verts[i].owner < nb;
    /* alfa só de cobertura; o STP (alfa intermediário do arquivo) num bit por texel. Palco (fase 6, índice >= 100): o
     * alfa é um campo suave de cobertura (borda em 128, export_stage.smooth_alpha) com o STP no bit baixo */
    const size_t texels = (size_t)a->tex_size * a->tex_size;
    const int stage = a->char_index >= 100u;
    for (uint32_t l = 0; ok && l < a->n_layers; l++) {
        uint8_t* px = a->layers + layer_bytes * l;
        uint8_t* st = a->stp + texels / 8u * l;
        for (size_t i = 0; i < texels; i++) {
            const uint8_t al = px[4 * i + 3];
            if (stage) { if ((al & 1u) && al > 8u) st[i >> 3] |= (uint8_t)(1u << (i & 7)); }
            else if (al > 64 && al < 200) { st[i >> 3] |= (uint8_t)(1u << (i & 7)); px[4 * i + 3] = 255; }
        }
    }
    fclose(f);
    if (!ok) rmk_asset_free(a);
    return ok;
}
