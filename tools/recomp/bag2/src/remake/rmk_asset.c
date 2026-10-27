/* Carga do asset do remake, formato v3 (tools/remake/export_heat.py; little-endian):
 *   char[4] "HTR3"; u32 versão (3), n_nodes, n_verts, n_tris, n_layers, tex_size
 *   por nó:      u16 nverts (0 = sem mesh), u16 pad, u32 base_index (MeshHeader+0x98)
 *   por vértice: RmkVert (8 bytes)
 *   por face:    RmkTri (36 bytes), na ordem em que o jogo liga as primitivas na OT
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
    uint32_t hdr[6];
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "HTR3", 4) && fread(hdr, 4, 6, f) == 6 && hdr[0] == 3;
    if (ok) {
        a->n_nodes = hdr[1]; a->n_verts = hdr[2]; a->n_tris = hdr[3]; a->n_layers = hdr[4]; a->tex_size = hdr[5];
        ok = a->n_nodes <= RMK_MAX_NODES && a->n_verts && a->n_verts < 65536 && a->n_tris && a->n_layers <= 64 &&
             (a->tex_size == 256 || a->tex_size == 512 || a->tex_size == 1024 || a->tex_size == 2048);
    }
    for (uint32_t i = 0; ok && i < a->n_nodes; i++) {
        uint16_t nv, pad;
        uint32_t base;
        ok = fread(&nv, 2, 1, f) == 1 && fread(&pad, 2, 1, f) == 1 && fread(&base, 4, 1, f) == 1;
        a->node_nverts[i] = nv;
        a->node_base[i] = base;
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
        ok &= t->zp[0] < a->n_verts && t->zp[1] < a->n_verts && t->layer < (int)a->n_layers;
    }
    for (uint32_t i = 0; ok && i < a->n_verts; i++) ok = a->verts[i].owner < a->n_nodes;
    fclose(f);
    if (!ok) rmk_asset_free(a);
    return ok;
}
