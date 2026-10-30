/* Núcleo portátil do remake visual dos personagens (docs/remake-plano.md, fase 1).
 *
 * C99 puro, sem nada do PSXRecomp nem do SDL: o adaptador do PC (src/bag2_heat.c) usa isto hoje, e o do
 * Polygonia (DuckStation, fase 5) deve usar o mesmo. Três partes:
 *   - asset (rmk_asset.c): malha soldada, atributos por canto e camadas de textura HD, formato v6
 *     ("HTR6", tools/remake/export_char.py, um por personagem), com grupos de faces por mão alternativa,
 *     as páginas de textura (expressão do rosto) e as faces semitransparentes; lê também o v4 e o v5;
 *   - pose (rmk_pose.c): MATRIX câmera×osso do GTE -> posições de vértice no espaço da câmera, resumo do
 *     quadro (retângulo na tela, faixa de Z, slot da OT), normais por vértice;
 *   - GL (rmk_gl.c): desenho com shader próprio, no subconjunto comum de GL 3.3 core e GLES 3.0 (sem
 *     tessellation/geometry shader nem SSBO). Phong Tessellation por instancing: grade baricêntrica fixa
 *     como vértices, uma instância por face, posições/normais lidas de uma textura float. */
#ifndef RMK_H
#define RMK_H

#include <stdint.h>
#include <string.h>

#define RMK_MAX_NODES 64
#define RMK_MAX_GROUPS 16            /* grupo 0 = corpo (sempre); 1.. = gestos das mãos */
#define RMK_MAX_LAYERS 64
#define RMK_MAX_PAGES 8              /* TIMs (páginas de textura) do personagem */
#define RMK_MAX_REMAP 8              /* casas da página remapeadas num quadro (expressão do rosto) */
#define RMK_NO_PAGE 0xFFu
#define RMK_TESS_L 4                 /* divisões por aresta da grade de Phong (16 triângulos por face) */

#pragma pack(push, 1)
typedef struct { int16_t x, y, z; uint8_t owner, ao; } RmkVert;   /* espaço do nó dono; ao 255 = aberto */
typedef struct {
    uint16_t v[3], zp[2];            /* vértices (ordem do NCLIP do jogo); zp: os dois do slot da OT */
    uint8_t mode; int8_t layer;      /* modo 0 cor, 1 cor + textura por cima, 2 só textura; + 4 sem mapa de normal;
                                        + RMK_MODE_SEMI semitransparente (0,5 B + 0,5 F nos texels com STP) */
    uint8_t uv[3][2];                /* texels 0..255 da página original */
    uint8_t base[3][3];              /* cor do material 0..255 */
    uint8_t over[3][3];              /* modulação da textura 0..255 (128 = neutro) */
    uint8_t group;                   /* 0 corpo; g > 0 só com o gesto g ativo (máscara 1 << g) */
} RmkTri;
#pragma pack(pop)

#define RMK_MODE_SEMI 16u

/* Gesto de mão: o jogo troca o mesh inteiro da mão (o nó ligado na posição `tree_pos` da árvore muda),
 * reconhecido na RAM pela assinatura do MeshHeader desse nó. Os vértices exclusivos do grupo são
 * contíguos: [v0, v0 + nv), depois dos n_body do corpo. v7 (fase 5): os vértices exclusivos do grupo g usam o osso
 * próprio n_drawn + g - 1 (a matriz do nó do jogo que leva aquele mesh, onde quer que ele esteja na árvore), e
 * tree_pos = RMK_NO_POS marca uma peça sem posição (objeto que o jogo pendura numa mão: um nó a mais na árvore). */
typedef struct { uint32_t tree_pos, sig_nverts, sig_base, v0, nv; } RmkGroup;
#define RMK_NO_POS 255u

/* Página de textura (um TIM do personagem): onde o jogo a põe na VRAM (vx em halfwords, -1 = desconhecida),
 * tamanho em texels e bits por texel. A expressão do rosto é cópia de VRAM dentro de uma página (MoveImage). */
typedef struct { int16_t vx, vy; uint16_t w, h; uint8_t bpp; uint32_t hash, clut_hash; } RmkPage;   /* v6: FNV-1a da
                                                                       imagem do TIM (amostrada) e da CLUT inteira */

typedef struct {
    uint32_t version, char_index;    /* versão do arquivo; índice do personagem (tools/remake/bag2model.CHARS, v6) */
    char name[16];
    uint32_t n_nodes, n_verts, n_tris, n_layers, tex_size;
    uint16_t node_nverts[RMK_MAX_NODES];   /* assinatura de cada osso na RAM (MeshHeader +0x94/+0x98) */
    uint32_t node_base[RMK_MAX_NODES];
    uint8_t node_pages[RMK_MAX_NODES];     /* páginas usadas pelas faces originais do nó (bit p = TIM p; v6) */
    int mesh_nodes;                  /* nós com mesh, os primeiros na ordem da árvore */
    int n_drawn;                     /* deles, os desenhados (posições da árvore); o resto são mãos alternativas */
    uint32_t n_body, n_groups;       /* vértices do corpo (sempre ativos); grupos 1..n_groups */
    int n_bones;                     /* matrizes do esqueleto: n_drawn (v6) ou n_drawn + n_groups (v7) */
    RmkGroup groups[RMK_MAX_GROUPS];
    uint32_t n_pages;                /* asset v5; 0 no v4 (sem expressão do rosto) */
    RmkPage pages[RMK_MAX_PAGES];
    uint8_t layer_page[RMK_MAX_LAYERS];   /* página de cada camada (RMK_NO_PAGE = nenhuma) */
    RmkVert* verts;
    RmkTri* tris;
    uint8_t* layers;                 /* n_layers x tex_size² RGBA8 (alfa 0 = transparente, 255 = opaco; o STP do
                                        arquivo, alfa 143, vai para o alfa do mapa de normal na carga) */
    uint8_t* stp;                    /* 1 bit por texel: cor com STP (semitransparente nas faces RMK_MODE_SEMI) */
    uint8_t* nmaps;                  /* mapas de normal das camadas (rmk_asset_build_normals; alfa = STP), ou NULL */
} RmkAsset;

/* 1 = ok. Em erro, o asset fica zerado. */
int rmk_asset_load(RmkAsset* a, const char* path);
/* Só o cabeçalho e as tabelas (assinaturas dos ossos, grupos, páginas), sem malha nem texturas: para reconhecer o
 * personagem antes de carregar o resto. 1 = ok. */
int rmk_asset_peek(RmkAsset* a, const char* path);
void rmk_asset_free(RmkAsset* a);
/* Mapas de normal gerados das camadas (traço preto vira sulco), com o STP no alfa. strength: inclinação por
 * unidade de altura por texel. 1 = ok. */
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
/* Expressão do rosto: a casa [x0, x1) x [y0, y1) da página (texels) mostra o que está em (x + du, y + dv). */
typedef struct { uint16_t page, x0, y0, x1, y1; int16_t du, dv, pad; } RmkRemap;
#define RMK_FRAME_MAGIC 0x334B4D52u  /* "RMK3" */
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
    /* sombra no chão (fase 3, RMK_FLAG_FLOOR): plano do chão no espaço da câmera (normal para cima e um ponto) e
     * direção em que a sombra do jogo projeta (a da luz do jogo, câmera), opacidade e maciez da penumbra */
    float floor_n[3], floor_p[3], floor_l[3];
    float floor_alpha, floor_soft;
    uint32_t n_remap;              /* expressão do rosto (por último: o plugin regrava o bloco se o jogo trocar */
    RmkRemap remap[RMK_MAX_REMAP];   /* a casa depois de ligar o marcador, antes de a GPU chegar nele) */
} RmkFrame;                          /* seguido de float pos[3 * n_verts] (compacto, rmk_compact) */
#define RMK_FLAG_FLAT 1u             /* sem Phong (só a malha original), para conferir a geometria */
#define RMK_FLAG_NO_TEX 2u           /* sem textura (só luz), depuração */
#define RMK_FLAG_NO_NMAP 4u          /* sem mapas de normal (comparação) */
#define RMK_FLAG_FIXED_LIGHT 8u      /* luz fixa na câmera da fase 1 em vez das luzes do palco (comparação) */
#define RMK_FLAG_NO_SHADOW 16u       /* sem sombra própria (comparação) */
#define RMK_FLAG_FLOOR 32u           /* este pedido é a sombra no chão (rmk_gl_draw_floor), não o personagem */
#define RMK_FLAG_NO_SEMI 64u         /* faces semitransparentes desenhadas opacas (comparação) */
#define RMK_FLAG_MIRROR 128u         /* fase 6: reflexo no chão brilhante; pos já espelhada pelo plano floor_n/floor_p,
                                        floor_alpha = força, floor_soft = distância abaixo do chão em que some */

/* ---- GL ---------------------------------------------------------------------------------------- */
typedef void* (*RmkGetProc)(const char* name);
typedef struct RmkGL RmkGL;
typedef struct {
    uint32_t fbo;                    /* framebuffer de quem chama (religado depois do passo da sombra) */
    float ndc[4];                    /* NDC = (tela + deslocamento de desenho) * (ndc[0], ndc[2]) + (ndc[1], ndc[3]) */
    int viewport[4];                 /* do alvo (pixels) */
    int scissor[4];                  /* recorte do alvo (pixels); a profundidade é limpa só no retângulo */
    float bbox[4];                   /* retângulo do desenho na tela do PS1 */
    int skip_x[2];                   /* colunas [x0, x1) do alvo que não aparecem (no espelho da tela larga, o centro
                                        que a apresentação cobre com a superfície canônica); x1 <= x0 = nenhuma */
    int pass, scale;                 /* passo do desenho de host (0 canônico, 1 espelho) e pixels do alvo por pixel do
                                        PS1 (o palco captura o telão do fbo no passo 0) */
} RmkTarget;

/* O retângulo box (x, y, w, h, pixels do alvo) sem as colunas skip_x: até 2 retângulos em r; devolve quantos. */
static inline int rmk_target_rects(const RmkTarget* t, const int box[4], int r[2][4]) {
    const int x0 = box[0], x1 = box[0] + box[2];
    int n = 0;
    if (box[2] <= 0 || box[3] <= 0) return 0;
    if (t->skip_x[1] <= t->skip_x[0] || x1 <= t->skip_x[0] || x0 >= t->skip_x[1]) {
        memcpy(r[0], box, 4 * sizeof(int));
        return 1;
    }
    if (x0 < t->skip_x[0]) { r[n][0] = x0; r[n][1] = box[1]; r[n][2] = t->skip_x[0] - x0; r[n][3] = box[3]; n++; }
    if (x1 > t->skip_x[1]) { r[n][0] = t->skip_x[1]; r[n][1] = box[1]; r[n][2] = x1 - t->skip_x[1]; r[n][3] = box[3]; n++; }
    return n;
}

/* ---- pós-efeito do palco (rmk_post.c, fase 3) ----------------------------------------------------- */
/* Bloom multiescala e graduação de cor aplicados no meio do quadro, depois da cena e antes do HUD (o adaptador
 * liga o pedido na ordem de desenho do jogo), então HUD, menus e FMV nunca passam por aqui. Pulso no tempo da
 * música: `beat` é a fase dentro da batida já com o atraso do som (0 = batida). */
#define RMK_POST_MAGIC 0x31545350u   /* "PST1" */
typedef struct {
    uint32_t magic, serial, flags;
    float beat, bar;                 /* fase 0..1 na batida e no compasso de 4 (< 0: sem música) */
    float bloom, thr, knee, satw;    /* força do bloom, limiar e joelho (máximo do RGB), peso dos tons sem cor */
    float pulse, pulse_expo;         /* pulso no bloom e na exposição (amplitude na batida) */
    float expo, contrast, sat, vignette;
    float tint_lo[3], tint_hi[3];    /* cor das sombras e dos realces (1 = neutro) */
    float adapt;                     /* atenuação do bloom onde há muito brilho em volta (palco claro) */
} RmkPostFrame;
#define RMK_POST_OFF 1u              /* não processa (o marcador só prepara recursos) */
#define RMK_POST_NO_BLOOM 2u
#define RMK_POST_NO_GRADE 4u
#define RMK_POST_SPLIT 8u            /* só a metade direita (comparação A/B no mesmo quadro) */
#define RMK_POST_SHOW_BRIGHT 16u     /* depuração: mostra o que entra no bloom */
/* Retângulo em pixels do FBO (origem embaixo, como o GL). */
typedef struct { int x, y, w, h; } RmkRect;
typedef struct RmkPost RmkPost;
RmkPost* rmk_post_create(RmkGetProc get_proc, int gles);
/* Compila os programas (contexto GL corrente). 1 = pronto. */
int rmk_post_prepare(RmkPost* p);
/* Processa `r` do FBO `fbo` no lugar (o alfa, bit 15 do PS1, fica intacto). Deixa ligados `fbo` e o viewport
 * `vp`; quem chama restaura o resto (programa, VAO, texturas 8..11, blend, recorte, máscara de cor). */
void rmk_post_apply(RmkPost* p, const RmkPostFrame* fr, uint32_t fbo, RmkRect r, const int vp[4]);
/* Tela larga em que o centro vale na outra superfície (`hr` do FBO `hr_fbo`, que cai em `c` da faixa `band` do
 * FBO `wide_fbo`, mesmo tamanho): a cena sai do centro de lá e das laterais de cá, e a composição escreve o centro
 * lá e as laterais cá (sem emendar nem devolver o centro: duas cópias de resolução cheia a menos). Deixa ligados
 * `wide_fbo` e `vp`. */
void rmk_post_apply_wide(RmkPost* p, const RmkPostFrame* fr, uint32_t wide_fbo, RmkRect band, RmkRect c,
                         uint32_t hr_fbo, RmkRect hr, const int vp[4]);
/* Cópia de cor entre FBOs (filtro linear se os tamanhos diferem); deixa `bind` ligado. */
void rmk_post_blit(RmkPost* p, uint32_t src, RmkRect rs, uint32_t dst, RmkRect rd, uint32_t bind);
/* Envelope do pulso (0..1) na fase da batida: ataque na batida e queda exponencial; mais forte no 1 do compasso. */
float rmk_post_envelope(float beat, float bar);
const char* rmk_post_error(const RmkPost* p);

/* ---- palco (rmk_stage.c, fase 6) ---------------------------------------------------------------- */
/* O palco é um modelo no formato dos personagens (objetos rígidos, um "osso" por nó com mesh) com o asset HTR7 de
 * sempre (malha, camadas HD; tools/remake/export_stage.py) e uma tabela .stg: cada primitiva de cada nó na ordem do
 * buffer de pacotes do jogo (nó +0xA8, um ou dois pacotes por primitiva) e as faces do asset que ela vira. O
 * adaptador troca, na OT do quadro, cada sequência de pacotes dos nós por um pedido de desenho com os pacotes vivos
 * (cor, UV, CLUT e página de cada vértice, como o jogo os montou); o desenho segue a ordem de pintor deles, sem
 * profundidade, com luz por pixel (mapa de normal, especular, oclusão ambiente) e a sombra dos dançarinos. */
#define RMK_STAGE_TAB_MAGIC 0x31475453u     /* "STG1" */
typedef struct { uint32_t mesh_off, buf_bytes, n_prims, prim0; } RmkStageNode;
#pragma pack(push, 1)
typedef struct {
    uint16_t words;                  /* palavras do(s) pacote(s) da primitiva no buffer do nó, com as tags */
    uint8_t n_pk, n_faces;           /* pacotes (2 = decalque: base sem textura + textura) e faces do asset (0..2) */
    uint32_t face0;                  /* primeira face (as n_faces seguidas) */
    uint8_t corner[2][3];            /* slot do vértice no pacote (0..3) de cada canto de cada face */
    uint8_t prefix;                  /* 1: DR_TPAGE de 2 palavras (tag + E1h) antes do pacote (listas semitransparentes sem
                                        textura; FUN_8001FF34): o pacote fica com o jogo, só a contagem muda */
    uint8_t pad;
} RmkStagePrim;
typedef struct { uint16_t tpage, clut; uint8_t layer, clut_hash[3]; } RmkStageKey;   /* textura + CLUT do jogo -> camada;
    clut_hash: 24 bits baixos do FNV-1a da CLUT que a camada usou (16 ou 256 cores, little-endian; 0 = desconhecido) */
/* .stg v2/v3: CLUT animada por MoveImage (o jogo copia uma linha de um banco de paletas na CLUT da chave): a variante de
 * cada origem (src_x, src_y) da cópia. kind 0: camada própria (`layer`); kind 1 (v3, fase 6c): afim, a camada base da
 * chave com rgb' = k·rgb + b no shader (k em 8.8, b em 1/4096 da escala cheia): quase toda paleta animada é só um
 * escurecimento ou um deslocamento de cor da paleta base, e assim não custa camada nem memória. O pacote leva a variante
 * afim como camada n_layers + índice. */
typedef struct { uint16_t key; int16_t src_x, src_y; uint8_t layer, kind; int16_t k[3], b[3]; } RmkStageVar;   /* 20 B */
#define RMK_STAGE_VAR_LAYER 0u
#define RMK_STAGE_VAR_AFFINE 1u
#pragma pack(pop)
typedef struct {
    uint32_t version, container, n_nodes, n_layers, n_keys, model_bytes;
    uint32_t ov_hash, model_hash;    /* FNV-1a dos 512 primeiros bytes do overlay e dos 256 primeiros do modelo */
    RmkStageNode nodes[RMK_MAX_NODES];
    uint32_t n_prims;
    RmkStagePrim* prims;
    RmkStageKey* keys;
    uint32_t n_var;                  /* v2 */
    RmkStageVar* var;
} RmkStage;
int rmk_stage_load(RmkStage* s, const char* path);   /* 1 = ok */
void rmk_stage_free(RmkStage* s);
uint32_t rmk_fnv1a(const uint8_t* p, uint32_t n);

/* Pacote vivo de uma primitiva (o que o jogo pôs na OT): cores de cada slot do pacote base, cores e UV do pacote com
 * textura (decalque: os dois; senão o mesmo), camada HD da página + CLUT (0xFF = a da face no asset). */
#pragma pack(push, 1)
typedef struct {
    uint16_t prim;                   /* índice global (RmkStage.prims) */
    uint8_t flags;                   /* RMK_SPKT_* */
    uint8_t layer;
    uint8_t col[4][3], tcol[4][3], uv[4][2];
    uint32_t twin;                   /* janela de textura (GP0 E2h, 20 bits) ativa no pacote; 0 = nenhuma */
} RmkStagePkt;                       /* 40 bytes */
#pragma pack(pop)
/* Telão (fase 6c): pacote com página de 16 bpp dentro dos buffers de tela (x < 512): no 235 a parede do fundo lê o
 * próprio quadro (POLY_FT4 com tpage = GetTPage(2, X, Y), X em {0..256}, Y 0 ou 256). O pacote leva a camada
 * RMK_STAGE_LAYER_FB | (Y == 256 ? 8 : 0) | X / 64; o núcleo copia a faixa 512 x 256 do buffer Y do fbo para uma textura
 * na primeira sequência do quadro que a usa (semântica do PS1: o que estiver na VRAM nesse ponto da OT) e desenha a
 * face com ela, sem luz. */
#define RMK_STAGE_LAYER_FB 0xE0u     /* 0xE0..0xEF */
#define RMK_STAGE_LAYER_IS_FB(l) (((l) & 0xF0u) == RMK_STAGE_LAYER_FB)
#define RMK_SPKT_TEX 1u              /* tem textura */
#define RMK_SPKT_SEMI 2u             /* semitransparente; modo de mistura do PS1 nos bits 4..5 */
#define RMK_SPKT_QUAD 4u             /* face livre de 4 vértices (os 4 slots do pacote) */
#define RMK_SPKT_FREE 0x8000u        /* prim com este bit: face livre (geometria do código do overlay, não do modelo),
                                        vértices em RmkStageFrame (prim & 0x7FFF = o primeiro, na ordem dos slots) */
#define RMK_STAGE_MAX_FREE 3600u     /* vértices livres por quadro (o quadro cabe num desenho de host de 64 KB) */
#define RMK_SPKT_BLEND(f) (((f) >> 4) & 3u)
#define RMK_STAGE_FRAME_MAGIC 0x46475453u   /* "STGF" */
#define RMK_STAGE_RUN_MAGIC 0x52475453u     /* "STGR" */
typedef struct {
    uint32_t magic, serial, flags, n_bones;
    float ofx, ofy, h, pad0;         /* projeção do GTE */
    float relight, detail, spec, ao; /* pesos: reiluminação, relevo do mapa de normal, especular, oclusão ambiente */
    float shadow, pulse, expo, rough;   /* sombra dos dançarinos, pulso da música (0..1), exposição, rugosidade */
    float ldir[3][3], lcol[3][3], bk[3];   /* luzes do palco (GTE, câmera), como as dos dançarinos */
    float floor_on;                  /* 1: a sombra macia no chão dos dançarinos já sai (o chão não recebe a do mapa) */
    float floor_n[3], pad1;          /* normal do chão (para cima, câmera) */
    RmkBone bones[RMK_MAX_NODES];    /* MATRIX câmera x nó de cada nó com mesh; nó fora do quadro: identidade com
                                        t[2] = RMK_STAGE_UNSEEN_Z (longe, e o núcleo sabe que não é pose de verdade) */
} RmkStageFrame;
#define RMK_STAGE_UNSEEN_Z 1e6f
typedef struct { uint32_t magic, serial, n, has_frame; } RmkStageRun;   /* + RmkStageFrame (has_frame) + n pacotes */
/* RmkStageFrame é seguido de n_free_faces bytes (3 ou 4: vértices de cada face livre, na ordem; completados até um
 * múltiplo de 4) e de float pos[3 * n_free_verts] (câmera do GTE): a geometria do código do overlay (chão, fundo,
 * peças especiais) que o adaptador reconheceu pelos vértices transformados (fase 6). */
typedef struct { uint32_t n_free_faces, n_free_verts; } RmkStageFree;
#define RMK_SFLAG_NO_RELIGHT 1u      /* só a cor do jogo com a textura HD (comparação) */
#define RMK_SFLAG_NO_NMAP 2u
#define RMK_SFLAG_NO_SHADOW 4u
#define RMK_SFLAG_NO_PHONG 16u       /* sem Phong nas faces curvas (comparação) */
#define RMK_SFLAG_NO_LIGHTS 8u       /* sem luzes do palco no quadro (ninguém em cena): sem reiluminação */
#define RMK_SFLAG_NODE_COLORS 256u   /* depuração: cada nó de uma cor, sem textura nem luz */
/* Sombra de um dançarino para o palco: mapa de profundidade ortográfico (textura GL) e o espaço dele (câmera). */
typedef struct { uint32_t tex; float u[3], v[3], d[3], c[4]; } RmkShadowView;
typedef struct RmkStageGL RmkStageGL;
RmkStageGL* rmk_stage_gl_create(RmkGetProc get_proc, int gles);
/* Recursos aos poucos (uma camada por chamada), como rmk_gl_prepare. 1 = pronto. */
int rmk_stage_gl_prepare(RmkStageGL* g, const RmkAsset* a, const RmkStage* s);
/* Quadro novo: posições dos vértices pelas matrizes dos nós e normais (uma vez por quadro). */
void rmk_stage_gl_frame(RmkStageGL* g, const RmkAsset* a, const RmkStageFrame* fr, const RmkStageFree* fv,
                        const uint8_t* face_n, const float* free_pos);
/* Uma sequência de pacotes do jogo, na ordem dele. `key` identifica a sequência no quadro: o espelho da tela larga
 * repete a mesma sem montar de novo. Mesmo contrato de estado GL que rmk_gl_draw (unidades 8..10, 14 e 15). */
void rmk_stage_gl_draw(RmkStageGL* g, const RmkAsset* a, const RmkStage* s, const RmkStagePkt* pk, uint32_t n,
                       uint32_t key, const RmkTarget* t, const RmkShadowView* sh, int n_sh);
const char* rmk_stage_gl_error(const RmkStageGL* g);
uint64_t rmk_stage_gl_bytes(const RmkStageGL* g);
uint32_t rmk_stage_gl_curved(const RmkStageGL* g);   /* faces que saem pela grade de Phong */
void rmk_stage_gl_destroy(RmkStageGL* g);

RmkGL* rmk_gl_create(RmkGetProc get_proc, int gles);
/* Sombra do personagem para o palco (fase 6): o mapa da sombra no chão se foi feito para o último quadro dele, senão
 * a sombra própria (luz principal). 0 = nenhum ainda. */
int rmk_gl_shadow_view(const RmkGL* g, RmkShadowView* out);
/* Cria os recursos aos poucos (uma camada de textura por chamada). Contexto GL corrente. 1 = pronto; depois disso o
 * desenho não lê mais as camadas nem os mapas de normal do asset (quem chama pode liberar a cópia de CPU). */
int rmk_gl_prepare(RmkGL* g, const RmkAsset* a);
/* Desenha um quadro (pos: vetor completo, n_verts do asset). O estado GL que o desenho muda fica documentado
 * em rmk_gl.c; quem chama restaura. */
void rmk_gl_draw(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t);
/* Sombra macia do personagem no plano do chão (fr->floor_*): mapa de sombra próprio na direção da sombra do jogo e
 * penumbra que abre com a distância ao chão (PCSS); escurece o que já está desenhado (o palco), sem tocar o alfa.
 * Quem chama liga o pedido na ordem do jogo no lugar da sombra original. Mesmo estado GL que rmk_gl_draw. */
void rmk_gl_draw_floor(RmkGL* g, const RmkAsset* a, const RmkFrame* fr, const float* pos, const RmkTarget* t);
const char* rmk_gl_error(const RmkGL* g);   /* último erro de compilação/recurso ("" se nenhum) */
/* Memória de GPU que o contexto pediu (bytes, estimativa pelos tamanhos das texturas e buffers, com mipmaps). */
uint64_t rmk_gl_bytes(const RmkGL* g);
/* Apaga todos os objetos GL do contexto e o próprio contexto (personagem que saiu de cena). Contexto GL corrente, a
 * mesma thread que criou; depois disso `g` não vale mais. */
void rmk_gl_destroy(RmkGL* g);

#endif
