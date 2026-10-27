/* Núcleo portátil do remake visual dos personagens (notes/remake-plano.md, fase 1).
 *
 * C99 puro, sem nada do PSXRecomp nem do SDL: o adaptador do PC (src/bag2_heat.c) usa isto hoje, e o do
 * Polygonia (DuckStation, fase 5) deve usar o mesmo. Três partes:
 *   - asset (rmk_asset.c): malha soldada, atributos por canto e camadas de textura HD, formato v4
 *     ("HTR4", tools/remake/export_heat.py), com grupos de faces por gesto das mãos (fase 2);
 *   - pose (rmk_pose.c): MATRIX câmera×osso do GTE -> posições de vértice no espaço da câmera, resumo do
 *     quadro (retângulo na tela, faixa de Z, slot da OT), normais por vértice;
 *   - GL (rmk_gl.c): desenho com shader próprio, no subconjunto comum de GL 3.3 core e GLES 3.0 (sem
 *     tessellation/geometry shader nem SSBO). Phong Tessellation por instancing: grade baricêntrica fixa
 *     como vértices, uma instância por face, posições/normais lidas de uma textura float. */
#ifndef RMK_H
#define RMK_H

#include <stdint.h>

#define RMK_MAX_NODES 64
#define RMK_MAX_GROUPS 16            /* grupo 0 = corpo (sempre); 1.. = gestos das mãos */
#define RMK_TESS_L 4                 /* divisões por aresta da grade de Phong (16 triângulos por face) */

#pragma pack(push, 1)
typedef struct { int16_t x, y, z; uint8_t owner, ao; } RmkVert;   /* espaço do nó dono; ao 255 = aberto */
typedef struct {
    uint16_t v[3], zp[2];            /* vértices (ordem do NCLIP do jogo); zp: os dois do slot da OT */
    uint8_t mode; int8_t layer;      /* modo 0 cor, 1 cor + textura por cima, 2 só textura; + 4 sem mapa de normal */
    uint8_t uv[3][2];                /* texels 0..255 da página original */
    uint8_t base[3][3];              /* cor do material 0..255 */
    uint8_t over[3][3];              /* modulação da textura 0..255 (128 = neutro) */
    uint8_t group;                   /* 0 corpo; g > 0 só com o gesto g ativo (máscara 1 << g) */
} RmkTri;
#pragma pack(pop)

/* Gesto de mão: o jogo troca o mesh inteiro da mão (o nó ligado na posição `tree_pos` da árvore muda),
 * reconhecido na RAM pela assinatura do MeshHeader desse nó. Os vértices exclusivos do grupo são
 * contíguos: [v0, v0 + nv), depois dos n_body do corpo. */
typedef struct { uint32_t tree_pos, sig_nverts, sig_base, v0, nv; } RmkGroup;

typedef struct {
    uint32_t n_nodes, n_verts, n_tris, n_layers, tex_size;
    uint16_t node_nverts[RMK_MAX_NODES];   /* assinatura de cada osso na RAM (MeshHeader +0x94/+0x98) */
    uint32_t node_base[RMK_MAX_NODES];
    int mesh_nodes;                  /* nós com mesh, os primeiros na ordem da árvore */
    uint32_t n_body, n_groups;       /* vértices do corpo (sempre ativos); grupos 1..n_groups */
    RmkGroup groups[RMK_MAX_GROUPS];
    RmkVert* verts;
    RmkTri* tris;
    uint8_t* layers;                 /* n_layers x tex_size² RGBA8 (alfa 0 = transparente) */
    uint8_t* nmaps;                  /* mapas de normal das camadas (rmk_asset_build_normals), ou NULL */
} RmkAsset;

/* 1 = ok. Em erro, o asset fica zerado. */
int rmk_asset_load(RmkAsset* a, const char* path);
void rmk_asset_free(RmkAsset* a);
/* Mapas de normal gerados das camadas (traço preto vira sulco). strength: inclinação por unidade de altura
 * por texel. 1 = ok. */
int rmk_asset_build_normals(RmkAsset* a, float strength);

/* ---- pose -------------------------------------------------------------------------------------- */
typedef struct { float m[9], t[3]; } RmkBone;            /* m = rotação 4.12 / 4096, t = translação */
void rmk_bone_from_gte(RmkBone* b, const int16_t m[9], const int32_t t[3]);
/* Máscara de grupos ativos (bit 0 = corpo, sempre). Vértices ativos e conversão entre o vetor completo
 * (n_verts, índices do asset) e o compacto (corpo + grupos ativos em ordem, o que vai no quadro). */
uint32_t rmk_active_verts(const RmkAsset* a, uint32_t mask);
void rmk_compact(const RmkAsset* a, uint32_t mask, const float* full, float* compact);
void rmk_expand(const RmkAsset* a, uint32_t mask, const float* compact, float* full);
#define RMK_FACE_ON(t, mask) (((mask) >> (t)->group) & 1u)
/* pos: 3 floats por vértice do asset (espaço da câmera do GTE: x direita, y para baixo, z para dentro);
 * só os vértices ativos em `mask` são calculados. */
void rmk_skin(const RmkAsset* a, const RmkBone* bones, int n_bones, uint32_t mask, float* pos);

typedef struct { float ofx, ofy, h; } RmkProj;            /* tela = OF + H * (x, y) / z */
/* Luzes do palco como o GTE as usa: L = matriz de luz do osso (linhas = direção de cada luz no espaço do osso,
 * 4.12), M = MATRIX câmera×osso do mesmo osso, lcm = matriz de cor (LR1 LR2 LR3 LG1 .. LB3, 4.12; coluna j =
 * cor da luz j, pode ser negativa), bk = cor ambiente (20.12). Vira direção na câmera (normalizada) e cor. */
void rmk_stage_light(const int16_t L[9], const int16_t M[9], const int16_t lcm[9], const int32_t bk[3],
                     float ldir[3][3], float lcol[3][3], float bk_out[3]);
typedef struct {
    float bbox[4];                   /* x0, y0, x1, y1 na tela do PS1, com margem para o Phong */
    float zmin, zmax;                /* Z dos vértices de faces visíveis */
    int ot_near;                     /* menor slot da OT ((z0 + z1) >> 3) entre as faces visíveis */
    int visible;                     /* faces visíveis (NCLIP > 0, como o jogo) */
} RmkFrameInfo;
void rmk_frame_info(const RmkAsset* a, const float* pos, uint32_t mask, const RmkProj* pr, float shift_x,
                    RmkFrameInfo* out);
/* Normais por vértice (soma das normais das faces ativas, viradas para a câmera) com o AO do asset: 8 floats
 * por vértice (x, y, z, ao, nx, ny, nz, 0), o formato que rmk_gl sobe para a GPU. */
void rmk_pack_geometry(const RmkAsset* a, const float* pos, uint32_t mask, float* out);

/* ---- quadro: o que vai do jogo para o desenho (POD, copiado por valor entre threads) --------- */
#define RMK_FRAME_MAGIC 0x324B4D52u  /* "RMK2" */
typedef struct {
    uint32_t magic, n_verts, serial, flags;
    uint32_t groups;                 /* máscara de grupos ativos; n_verts = rmk_active_verts(groups) */
    float ofx, ofy, h, shift_x;      /* projeção do GTE e deslocamento extra na tela (comparação/carga) */
    float znear, zfar;               /* faixa de profundidade do desenho */
    float alpha;                     /* Phong Tessellation */
    float expo, key, fill, rim, amb, rough;   /* luz (ajustável por comando de depuração) */
    float zbias;                     /* tolerância relativa de profundidade (ordem de pintor dentro dela) */
    float bbox[4];                   /* RmkFrameInfo.bbox: retângulo do desenho na tela */
    float ldir[3][3], lcol[3][3];    /* luzes do palco (GTE) no espaço da câmera: direção para a luz e cor */
    float bk[3];                     /* cor ambiente do palco (BK do GTE) */
} RmkFrame;                          /* seguido de float pos[3 * n_verts] (compacto, rmk_compact) */
#define RMK_FLAG_FLAT 1u             /* sem Phong (só a malha original), para conferir a geometria */
#define RMK_FLAG_NO_TEX 2u           /* sem textura (só luz), depuração */
#define RMK_FLAG_NO_NMAP 4u          /* sem mapas de normal (comparação) */
#define RMK_FLAG_FIXED_LIGHT 8u      /* luz fixa na câmera da fase 1 em vez das luzes do palco (comparação) */
#define RMK_FLAG_NO_SHADOW 16u       /* sem sombra própria (comparação) */

/* ---- GL ---------------------------------------------------------------------------------------- */
typedef void* (*RmkGetProc)(const char* name);
typedef struct RmkGL RmkGL;
typedef struct {
    uint32_t fbo;                    /* framebuffer de quem chama (religado depois do passo da sombra) */
    float ndc[4];                    /* NDC = (tela + deslocamento de desenho) * (ndc[0], ndc[2]) + (ndc[1], ndc[3]) */
    int viewport[4];                 /* do alvo (pixels) */
    int scissor[4];                  /* recorte do alvo (pixels); a profundidade é limpa só no retângulo */
    float bbox[4];                   /* retângulo do desenho na tela do PS1 */
} RmkTarget;

RmkGL* rmk_gl_create(RmkGetProc get_proc, int gles);
/* Cria os recursos aos poucos (uma camada de textura por chamada). Contexto GL corrente. 1 = pronto. */
int rmk_gl_prepare(RmkGL* g, const RmkAsset* a);
/* Desenha um quadro (pos: vetor completo, n_verts do asset). O estado GL que o desenho muda fica documentado
 * em rmk_gl.c; quem chama restaura. */
void rmk_gl_draw(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t);
const char* rmk_gl_error(const RmkGL* g);   /* último erro de compilação/recurso ("" se nenhum) */

#endif
