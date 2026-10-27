/* Carga do asset do remake, formato v4 (tools/remake/export_heat.py; little-endian):
 *   char[4] "HTR4"; u32 versão (4), n_nodes, n_verts, n_tris, n_layers, tex_size, n_body, n_groups
 *   por nó:      u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
 *   por grupo (1..n_groups): u8 posição na árvore, u8 pad, u16 nverts e u32 base_index do mesh do jogo
 *                desse gesto, u32 primeiro vértice, u32 nº de vértices (exclusivos do grupo, contíguos)
 *   por vértice: RmkVert (8 bytes); os n_body primeiros são do corpo
 *   por face:    RmkTri (37 bytes), na ordem em que o jogo liga as primitivas na OT
 *   por camada:  tex_size x tex_size RGBA8 (Real-ESRGAN 4x, alfa 0 = transparente) */
#include "rmk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rmk_asset_free(RmkAsset* a) {
    free(a->verts);
    free(a->tris);
    free(a->layers);
    memset(a, 0, sizeof *a);
}

int rmk_asset_load(RmkAsset* a, const char* path) {
    memset(a, 0, sizeof *a);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    uint32_t hdr[8];
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "HTR4", 4) && fread(hdr, 4, 8, f) == 8 && hdr[0] == 4;
    if (ok) {
        a->n_nodes = hdr[1]; a->n_verts = hdr[2]; a->n_tris = hdr[3]; a->n_layers = hdr[4]; a->tex_size = hdr[5];
        a->n_body = hdr[6]; a->n_groups = hdr[7];
        ok = a->n_nodes <= RMK_MAX_NODES && a->n_verts && a->n_verts < 65536 && a->n_tris && a->n_layers <= 64 &&
             (a->tex_size == 256 || a->tex_size == 512 || a->tex_size == 1024 || a->tex_size == 2048) &&
             a->n_body <= a->n_verts && a->n_groups < RMK_MAX_GROUPS;
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
