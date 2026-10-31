/* Remake dos personagens dentro do jogo (docs/remake-visual.md; plano em docs/remake-plano.md). Começou só com o
 * HEAT (fases 0–3); na fase 4 virou genérico: um asset por personagem (mods/remake/<nome>.bin, v6, de
 * tools/remake/export_char.py, fora do git), reconhecido pela assinatura dos ossos, com os dois jogadores.
 *
 * Como o jogo desenha um personagem: FUN_8002BEB4 (por personagem, todo quadro de partida) olha o tipo do
 * personagem (estado +4) e o jogador (+5) e chama FUN_80029D4C (tipo 0: HEAT) ou uma função do overlay do
 * personagem (os outros), com a raiz da árvore de ossos (estado +0x28). As duas percorrem a árvore (filho +0x94,
 * irmão +0x98, nós de 0xB0 bytes num vetor a partir da raiz; +0x6C = MATRIX câmera×osso já pronta para o GTE, +0x8C
 * = MeshHeader, +0xA0 = tipo) e chamam FUN_80029588 por osso (a1 = buffer de pacotes, a2 = nó + 0x2C), que monta os
 * pacotes e liga na OT. Dois ganchos de filtro, com a mesma conta:
 *   - entrada de FUN_80029D4C (árvore inteira; pula o desenho todo);
 *   - entrada de FUN_80029588 (por osso, para os overlays): na raiz da árvore reconhece o personagem e desenha o
 *     remake; os outros ossos da mesma árvore são pulados um a um.
 * Personagem reconhecido = cada osso bate com o asset (nº de vértices em MeshHeader +0x94, índice-base de normais em
 * +0x98); nas posições das mãos o jogo liga outro nó (gesto: o script da dança religa o irmão do antebraço) e o asset
 * tem um grupo de faces por mão. O remake vai pelo desenho de host (padrão, fase 1): a pose vira um pacote marcador
 * na arena de DMA, ligado no slot da OT da face mais próxima; quando a GPU emulada chega nele, o renderizador GL
 * chama char_host_draw com o FBO de alta resolução (patch local psxrecomp-host-draw.patch), e o núcleo portátil
 * (src/remake/) faz Phong Tessellation, luz por pixel, texturas HD com mipmaps, semitransparência do PS1.
 * O caminho por pacotes GT3 da fase 0 (fallback, heat_mode flags bit 2) continua só para o HEAT.
 *
 * Carga sob demanda: na ativação só as tabelas de cada asset (assinaturas); a malha e as texturas (50–60 MB cada)
 * são lidas numa thread quando o jogo carrega um TIM do personagem (LoadImage com a impressão digital do asset, na
 * tela de carregamento) ou, se não deu, no primeiro desenho; os recursos GL são criados aos poucos por marcadores de
 * preparo. Até ficar pronto, o original aparece.
 * Páginas de textura: o jogo põe os TIMs de cada jogador em lugares diferentes da VRAM. O plugin lê a página
 * (tpage) dos pacotes que o próprio personagem montou (nó +0xA8, tamanho em +0xA4) e cruza com as páginas que cada
 * osso usa no asset. Expressão do rosto: o código do personagem copia variantes (olhos, boca) para as "casas" da
 * página do rosto na VRAM com MoveImage; os ganchos em MoveImage/LoadImage mantêm uma tabela de casas remapeadas em
 * coordenadas da VRAM, convertida para as páginas de cada personagem no quadro.
 * Sombra no chão (fase 3): o jogo projeta o esqueleto de sombra no plano y = 0 do mundo na direção da luz em
 * 0x80060CB8 e desenha por subtração no slot 4167 da OT (FUN_8002A46C). Para o personagem com remake o plugin pula
 * essa função e liga no mesmo slot um segundo pedido de desenho de host com a pose (sombra macia, PCSS).
 * A luz do remake pulsa com a música (bag2_stage_pulse, src/bag2_stage.c).
 * Comandos TCP (build-debug): heat_mode, heat_light, heat_stats, heat_dump, heat_force, heat_face, heat_floor,
 * heat_chars. */
#include "mod_plugins.h"
#include "cpu_state.h"
#include "remake/rmk.h"
/* Android (PSX_GLES): o núcleo compila os shaders como GLSL ES 3.00. */
#if defined(PSX_GLES)
#define BAG2_GLES 1
#else
#define BAG2_GLES 0
#endif
#include "gpu_timer.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static double now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
#else
#include <pthread.h>
#include <time.h>
static double now_us(void) { struct timespec t; timespec_get(&t, TIME_UTC); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
#endif
static void prep_note(double us, int ready);   /* tempo dos passos de preparo GL (heat_hitch) */
static void rt_gap_note(double now, double draw_us);   /* intervalo entre desenhos na thread de render (heat_hitch) */

#define DRAW_TREE_FN 0x80029D4Cu
#define NODE_FILL_FN 0x80029588u      /* FUN_80029588(mesh, buffer de pacotes, nó + 0x2C, ...): um osso */
#define SHADOW_FN 0x8002A46Cu         /* FUN_8002A46C(raiz da sombra, ..., MATRIX do personagem): sombra no chão */
#define CAMERA_MATRIX 0x8005E4D0u     /* MATRIX mundo -> câmera do palco (DAT_8005E4D0) */
#define SHADOW_LIGHT 0x80060CB8u      /* ponteiro para a direção da luz da sombra no mundo (3 shorts, y > 0) */
#define SHADOW_SLOT 4167              /* slot da OT da sombra do jogo (DAT_8006EE88 + 0x3F9C) */
#define LOAD_IMAGE_FN 0x80048234u    /* libgpu LoadImage(RECT*, u_long*) */
#define MOVE_IMAGE_FN 0x800482F4u    /* libgpu MoveImage(RECT* origem, x, y): executa na hora (GPU ociosa) */
#define OT_PTR 0x8006EE88u           /* DAT_8006EE88: OT do quadro (ClearOTagR(ot - 4, 0x1001)) */
#define FRAME_PARITY 0x8006EE90u     /* DAT_8006EE90: buffer duplo do quadro */
#define GAME_FRAME 0x8006EE78u       /* contador de quadros do jogo (o jogo monta a OT a 30 Hz) */
#define OT_MIN 95                    /* índices válidos de (ot - 0x180) + 4*i */
#define OT_MAX (95 + 4096)
#define MAX_NODES 48
#define NODE_SIZE 0xB0u
#define PKT_BYTES 80u                /* 40 (tag + GT3) + 40 (sufixo de precisão) */
#define MAX_PKTS 40000u
#define BANKS_PER_LAYER 8
static const int k_bank_rows[BANKS_PER_LAYER] = {0, 64, 128, 192, 512, 576, 640, 704};

#define HOST_DRAW_ID 1
#define HD_FLAG_PREPARE 1u           /* marcador só para criar os recursos GL aos poucos */
#define HD_FLAG_FREE 2u              /* marcador só para apagar os recursos GL de quem saiu de cena (fase 5) */
#define EVICT_GONE_S 3.0             /* descarga: sem desenho há 3 s, fora de uma partida em pausa... */
#define EVICT_IDLE_S 60.0            /* ...e (carregado para a próxima partida, ainda não desenhado) até 60 s */
static const uint32_t k_player_state[2] = {0x80060E30u, 0x80060E38u};   /* estado do 1P e do 2P (+0x28 = raiz) */
#define HD_CHAR_SHIFT 8              /* flags do cabeçalho: personagem (asset) nos bits 8..13 */
#define MARKS_PER_HALF 24u           /* marcadores por quadro: 2 personagens + sombras + 3 cópias de cada + preparo */
#define MAX_CHARS 36                 /* 18 personagens x 2 figurinos (normal e alternativo) */
#define MAX_INST 4                   /* personagens na partida ao mesmo tempo (raízes diferentes) */
#define MAX_VREMAP 32                /* casas remapeadas na VRAM (expressão do rosto, todos os personagens) */
#define HEAT_INDEX 3u                /* tools/remake/bag2model.CHARS */

typedef RmkVert HVert;
typedef RmkTri HTri;
typedef struct { float x, y, z; } V3;

/* nome do arquivo de cada personagem (tools/remake/export_char.slug), na ordem de bag2model.CHARS */
static const char* const k_slugs[18] = {"bio", "capoeira", "comet", "heat", "hiro", "kelly", "kittyn", "pander",
    "robozgold", "shorty", "strike", "tsutomu", "chichisally", "columbo", "hustlekong", "mcload", "michaeldoi", "sushiboy"};

/* Um asset (personagem num figurino). As tabelas (`t`) existem desde a ativação; `full` (malha + texturas) depois da
 * carga, e não muda enquanto carregado (a thread de render lê). Fase 5: só os personagens em cena ficam carregados;
 * quem saiu (keep_loaded: sem desenho há 3 s e nem em pausa na partida nem esperando a partida começar) é
 * descarregado: a thread de render apaga os recursos GL num marcador e depois a do jogo solta a cópia de CPU. Um
 * desenho ou o LoadImage de um TIM dele carrega de novo. */
typedef struct {
    char path[64];
    int alt;                         /* figurino alternativo (<nome>_alt.bin: o do jogador 2 numa partida espelhada) */
    int heat;                        /* o HEAT normal: o único com o caminho por pacotes e os testes da fase 2 */
    RmkAsset t;                      /* só cabeçalho e tabelas (rmk_asset_peek) */
    RmkAsset* full;                  /* asset inteiro (thread de carga); NULL até state >= 2 */
    volatile int state;              /* 0 só tabelas, 1 carregando, 2 carregado, 3 descarregando, -1 falhou */
    double load_ms;
    double last_use_us;              /* último desenho (ou pedido de carga) na thread do jogo */
    int marked;                      /* algum marcador deste asset foi ligado desde a carga (a GPU pode ter recursos) */
    int drawn;                       /* desenhado desde a carga (antes disso: carregado para a partida que vai começar) */
    int keep_cpu;                    /* manter as camadas na CPU depois da subida (caminho por pacotes do HEAT) */
    uint32_t loads, evictions;
    /* thread de render */
    RmkGL* gl;
    volatile int rt_ready, rt_failed, rt_freed;
    volatile uint32_t rt_prepares;
    volatile uint64_t gl_bytes;      /* memória de GPU do contexto (rmk_gl_bytes) */
    volatile double rt_drawn_us;     /* último desenho (thread de render): os mapas de sombra dele valem para o palco */
    float* full_pos;                 /* posições no vetor completo do asset */
} Chr;
static Chr s_ch[MAX_CHARS];
static int s_nch;
static int s_img_twin[MAX_CHARS];    /* o outro figurino com o mesmo modelo e a mesma imagem (só a CLUT muda), -1 */
static uint32_t s_max_verts;         /* maior n_verts entre os assets (buffers de pose) */

/* Um personagem na partida: raiz do vetor de nós (o mesmo asset pode aparecer duas vezes). Thread do jogo. */
typedef struct {
    uint32_t root;
    int ch;                          /* asset; -1 = livre */
    uint32_t seen_fc;                /* último quadro do jogo em que foi desenhado */
    int16_t page_vx[RMK_MAX_PAGES], page_vy[RMK_MAX_PAGES];   /* origem de cada página na VRAM (halfwords) */
    uint32_t pages_ok, pages_try_fc;
    int16_t clut_x, clut_y;          /* uma CLUT que os pacotes do personagem usam (figurino; 0 = ainda não vista) */
    int clut_ok;
    float* pos;                      /* pose do último quadro (vetor completo) */
    RmkFrame floor_fr;               /* último quadro (modo 1/carga), para a sombra no chão sair só quando o jogo */
    uint32_t floor_fc;               /* desenharia a dele (FUN_8002A46C), com a pose mais recente */
    uint32_t floor_mark_fc;          /* quadro do último marcador da sombra nova no chão (fase 6: o palco não repete) */
    int floor_valid;
    struct { float n[3], p[3], l[3]; int ok; float hip, foot; } ground;   /* chão e direção da sombra do jogo */
    struct { float ldir[3][3], lcol[3][3], bk[3]; } stage;               /* luzes do palco (GTE) */
    uint32_t mask;                   /* grupos (mãos) do último quadro */
    float bbox[4], hand_xy[2][3], head_xy[4], shift;
    uint32_t marks[MARKS_PER_HALF], n_marks, marks_parity;   /* marcadores deste personagem na metade atual */
} Inst;
static Inst s_inst[MAX_INST];

static RmkAsset A;                   /* o HEAT carregado (cópia rasa de full), para o caminho por pacotes da fase 0 */
static int s_loaded;                 /* A pronto */
static uint8_t* s_amask[64];         /* máscara de alfa por camada HD (caminho por pacotes) */
static uint32_t s_arena;             /* arena dos pacotes GT3 (2 metades) */
static uint32_t s_arena_off, s_last_parity = 0xFFFFFFFFu;
static uint32_t s_marks;             /* arena dos marcadores de desenho de host (2 metades) */
static uint32_t s_mark_bytes, s_mark_off, s_mark_parity = 0xFFFFFFFFu;
static int s_mode = 1;               /* 0 original, 1 remake, 2 os dois sobrepostos, 3 os dois lado a lado,
                                        4..6 mais 1..3 cópias do remake ao lado (teste de carga),
                                        7 original + pacotes (fase 0) + desenho de host lado a lado (só o HEAT) */
static float s_shift;                /* deslocamento x do remake nos modos 3..6 (pixels do PS1) */
static float s_last_shift;           /* o do último desenho do HEAT (capturas) */
static int s_copy_xor;               /* modos 4..6: flags das cópias = s_flags ^ isto (comparação A/B no mesmo quadro) */
static int s_level = 4;              /* pacotes: máximo de divisões por aresta (4 -> até 16 triângulos por face) */
static float s_alpha = 0.6f;         /* Phong Tessellation */
/* bit 0 = pacotes sem cor base sob textura (depuração); bit 1 = pacotes agrupados por banco dentro do slot da
 * OT (corta as trocas de textura, ~79 % -> ~49 % da thread de render por HEAT); bit 2 = caminho por pacotes
 * (fase 0) em vez do desenho de host; bit 3 = desenho de host sem Phong (malha original); bit 4 = sem textura;
 * bit 5 = sem mapas de normal; bit 6 = luz fixa da fase 1 em vez das luzes do palco; bit 7 = sem sombra própria;
 * bit 8 = sem a sombra nova no chão (fica a do jogo); bit 9 = luz sem o pulso da música; bit 10 = faces
 * semitransparentes opacas; bit 11 = só o HEAT (os outros personagens ficam originais). */
static int s_flags = 2;
static float s_light[7] = {1.0f, 1.2f, 1.0f, 1.0f, 0.8f, 0.5f, 0.01f};   /* exposição, principal, preenchimento,
                                                         contraluz, ambiente, rugosidade, tolerância de profundidade */
static float s_zr[2];                /* faixa de Z do último remake (comando heat_mode) */
static float s_floor[3] = {0.85f, 0.4f, 0.35f};   /* sombra no chão: opacidade, maciez; pulso da luz do remake */
static uint32_t s_heat_root;         /* raiz da árvore do último HEAT (heat_force) */
static volatile int s_dump_request;
static char s_dump_path[260] = "heat_dump.txt";
static float s_bbox[4];              /* último retângulo do HEAT na tela (pixels do PS1), para capturas */
static uint32_t s_last_mask;         /* grupos (gestos) do último HEAT desenhado */
static float s_hand_xy[2][3];        /* osso de cada mão do HEAT na tela (pixels do PS1, sem deslocamento) e z */
static float s_head_xy[4];           /* osso da cabeça (13) do HEAT na tela, z e H do GTE (recorte do rosto) */
static int s_force[2] = {-1, -1};    /* heat_force: gesto forçado em cada mão do HEAT (teste), -1 = o do jogo */
static uint32_t s_d4c_root;          /* raiz que FUN_80029D4C deixou para o original (FUN_80029588 não refaz) */
static uint32_t s_skip[MAX_NODES], s_n_skip;   /* nós da árvore com remake sendo percorrida (FUN_80029588 pula) */
/* expressão do rosto: casas remapeadas na VRAM (halfwords, absolutas) -> deslocamento até o conteúdo */
typedef struct { int16_t x0, y0, x1, y1, dx, dy; } VRemap;
static VRemap s_vr[MAX_VREMAP];
static uint32_t s_nvr;
/* TIM e CLUT de personagem carregados na VRAM (LoadImage com a impressão digital de uma página de asset): decidem o
 * figurino quando os dois têm o mesmo modelo e só a textura (ou só a CLUT) muda, e dizem se o personagem ainda está
 * na VRAM (um LoadImage que cobre o retângulo tira a entrada: descarga, fase 5) */
static struct { int16_t x, y, w, h; int8_t ch, page; uint32_t hash; } s_vram_tim[16];
static uint32_t s_n_vram_tim;
static struct { int16_t x, y, w, h; int8_t ch; uint32_t hash; } s_vram_clut[16];
static uint32_t s_n_vram_clut;
/* LoadImage recentes (heat_chars): retângulo e impressão digital */
static struct { int16_t x, y, w, h; uint32_t hash; int ch; } s_loads[16];
static uint32_t s_nloads;
/* LoadImage que bateram com um TIM de asset (heat_chars): retângulo, asset, página, segundo da sessão */
static struct { int16_t x, y, w, h; int8_t ch, page; float t; } s_hits[8];
static uint32_t s_nhits;

/* thread de render (desenho de host): escritos só lá, lidos pelos comandos */
static volatile double s_rt_us;
static volatile uint32_t s_rt_draws;
static GpuTimer s_gt_body, s_gt_floor;   /* tempo de GPU por chamada: corpo (com a sombra própria) e chão */
static uint32_t s_serial;

static uint32_t rd32(uint32_t a) { return psx_mod_read_word(a); }
static int16_t rd16(uint32_t a) { return (int16_t)psx_mod_read_half(a); }
static void wr32(uint32_t a, uint32_t v) { psx_mod_write_word(a, v); }
static void write_arena(uint32_t addr, const uint32_t* w, uint32_t bytes) {
    if (!psx_mod_write_gpu_dma(addr, w, bytes))
        for (uint32_t i = 0; i < bytes / 4u; i++) wr32(addr + 4u * i, w[i]);
}
static int ram_ptr(uint32_t a) { return a >= 0x80000000u && a < 0x80200000u; }

/* Pior caso de vértices num quadro: corpo + o maior grupo de cada posição de mão. */
static uint32_t max_active_verts(const RmkAsset* a) {
    uint32_t n = a->n_body;
    for (uint32_t pos = 0; pos < RMK_MAX_NODES; pos++) {
        uint32_t m = 0;
        for (uint32_t g = 1; g <= a->n_groups; g++)
            if (a->groups[g].tree_pos == pos && a->groups[g].nv > m) m = a->groups[g].nv;
        n += m;
    }
    return n;
}

/* ------------------------------------------------------------------ carga sob demanda */
static void mem_barrier(void) {
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
}

static void load_char(Chr* c) {
    const double t0 = now_us();
    RmkAsset* a = (RmkAsset*)calloc(1, sizeof *a);
    int ok = a && rmk_asset_load(a, c->path);
    /* mapas de normal das camadas (fase 2, com o STP no alfa): uma vez, na thread de carga */
    if (ok) ok = rmk_asset_build_normals(a, 16.0f);
    if (ok) ok = rmk_asset_build_mips(a);
    if (ok) ok = a->n_verts <= s_max_verts && max_active_verts(a) <= max_active_verts(&c->t);
    c->load_ms = (now_us() - t0) / 1000.0;
    if (!ok) {
        if (a) { rmk_asset_free(a); free(a); }
        c->state = -1;
        return;
    }
    c->full = a;
    mem_barrier();
    c->state = 2;
}
#ifdef _WIN32
static DWORD WINAPI load_thread(LPVOID p) { load_char((Chr*)p); return 0; }
#else
static void* load_thread(void* p) { load_char((Chr*)p); return NULL; }
#endif
static void request_load(int ch, const char* why) {
    Chr* c = &s_ch[ch];
    c->last_use_us = now_us();       /* também mantém carregado quem o jogo carrega de novo (revanche) */
    if (c->state != 0) return;
    c->state = 1;
    c->marked = 0;
    c->drawn = 0;
    c->loads++;
    psx_mod_counter_add(why, 1);   /* o framework guarda o ponteiro do nome: só literais */
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, load_thread, c, 0, NULL);
    if (h) CloseHandle(h); else load_char(c);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, load_thread, c) == 0) pthread_detach(th); else load_char(c);
#endif
}

/* O HEAT pronto (com as camadas na CPU) vira o asset do caminho por pacotes (thread do jogo). */
static void packets_asset(void) {
    if (s_loaded) return;
    for (int i = 0; i < s_nch; i++) {
        const Chr* c = &s_ch[i];
        if (!c->heat || c->state != 2 || !c->full->layers) continue;
        A = *c->full;
        for (uint32_t l = 0; l < A.n_layers && l < 64; l++) {
            const size_t n = (size_t)A.tex_size * A.tex_size;
            if (!(s_amask[l] = (uint8_t*)malloc(n))) continue;
            const uint8_t* px = A.layers + n * 4u * l;
            for (size_t k = 0; k < n; k++) s_amask[l][k] = px[4 * k + 3] >= 128;
        }
        s_loaded = 1;
    }
}

/* ------------------------------------------------------------------ descarga (fase 5) */
/* LoadImage em (x, y, w, h): tira as entradas de TIM e CLUT de personagem que o retângulo cobre em pelo menos 1/4 (o
 * jogo pôs outra coisa naquela VRAM; uma CLUT pequena dentro da página não conta). */
static int covers(int x, int y, int w, int h, int ex, int ey, int ew, int eh) {
    const int ox = (x + w < ex + ew ? x + w : ex + ew) - (x > ex ? x : ex);
    const int oy = (y + h < ey + eh ? y + h : ey + eh) - (y > ey ? y : ey);
    return ox > 0 && oy > 0 && 4 * ox * oy >= ew * eh;
}
static void vram_cover(int x, int y, int w, int h) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < s_n_vram_tim; i++)
        if (!covers(x, y, w, h, s_vram_tim[i].x, s_vram_tim[i].y, s_vram_tim[i].w, s_vram_tim[i].h)) s_vram_tim[k++] = s_vram_tim[i];
    s_n_vram_tim = k;
    k = 0;
    for (uint32_t i = 0; i < s_n_vram_clut; i++)
        if (!covers(x, y, w, h, s_vram_clut[i].x, s_vram_clut[i].y, s_vram_clut[i].w, s_vram_clut[i].h)) s_vram_clut[k++] = s_vram_clut[i];
    s_n_vram_clut = k;
}

/* Algum TIM ou CLUT deste asset (ou da imagem que ele divide com o outro figurino) ainda está na VRAM. */
static int vram_has(int ch) {
    for (uint32_t k = 0; k < s_n_vram_tim; k++)
        if (s_vram_tim[k].ch == ch || (s_img_twin[ch] >= 0 && s_vram_tim[k].ch == s_img_twin[ch])) return 1;
    for (uint32_t k = 0; k < s_n_vram_clut; k++)
        if (s_vram_clut[k].ch == ch) return 1;
    return 0;
}

/* O asset é de um personagem da partida em andamento: a raiz no estado de um jogador (na pausa os dançarinos não são
 * desenhados, mas o estado continua; depois do QUIT ou na seleção ele some ou fica velho, e então a VRAM decide). */
static uint32_t second_bone_base(uint32_t root);
static int in_match(int ch) {
    const RmkAsset* t = &s_ch[ch].t;
    for (int p = 0; p < 2; p++) {
        const uint32_t st = rd32(k_player_state[p]);
        const uint32_t root = ram_ptr(st) ? rd32(st + 0x28u) : 0u;
        if (!ram_ptr(root)) continue;
        int inst = 0;
        for (int i = 0; i < MAX_INST && !inst; i++) inst = s_inst[i].ch == ch && s_inst[i].root == root;
        if (!inst) continue;
        /* o ponteiro do 2P continua velho depois do QUIT: a árvore ainda tem de ser a do personagem (nó 0 e 2º osso) */
        const uint32_t m0 = rd32(root + 0x8Cu);
        if (ram_ptr(m0) && rd32(m0 + 0x94u) == t->node_nverts[0] && second_bone_base(root) == t->node_base[1]) return 1;
    }
    return 0;
}

/* Fica carregado: desenhado há menos de EVICT_GONE_S; ou com os TIMs na VRAM e numa partida (pausa) ou ainda não
 * desenhado desde a carga (tela de carregamento da partida) há menos de EVICT_IDLE_S. */
static int keep_loaded(int ch, double now) {
    const Chr* c = &s_ch[ch];
    const double idle = (now - c->last_use_us) / 1e6;
    if (idle < EVICT_GONE_S) return 1;
    if (!vram_has(ch)) return 0;
    return in_match(ch) || (!c->drawn && idle < EVICT_IDLE_S);
}

/* Começa a descarregar: os personagens da partida com este asset saem e o caminho por pacotes solta o HEAT. Sem
 * marcador ligado desde a carga, a GPU não tem nada dele e a cópia de CPU sai no próximo passo; senão a thread de
 * render apaga os recursos GL no próximo marcador (heat_vblank liga um quando ninguém é desenhado). */
static void start_evict(int ch) {
    Chr* c = &s_ch[ch];
    if (c->state != 2) return;
    for (int i = 0; i < MAX_INST; i++)
        if (s_inst[i].ch == ch) s_inst[i].ch = -1;
    if (c->heat && s_loaded) {
        for (uint32_t l = 0; l < 64; l++) { free(s_amask[l]); s_amask[l] = NULL; }
        memset(&A, 0, sizeof A);
        s_loaded = 0;
    }
    c->rt_freed = 0;
    mem_barrier();
    c->state = 3;
    c->evictions++;
    psx_mod_counter_add("bag2_heat.evict", 1);
}

static void finish_evict(Chr* c) {
    if (c->full) { rmk_asset_free(c->full); free(c->full); c->full = NULL; }
    c->marked = 0;
    mem_barrier();
    c->state = 0;
    psx_mod_counter_add("bag2_heat.evict_done", 1);
}

/* ------------------------------------------------------------------ ativação */
static int twins(int a, int b);
/* Na ativação (antes do jogo rodar) só as tabelas de cada asset presente; as arenas também têm de ser alocadas
 * aqui. Malha e texturas sob demanda (request_load), recursos GL aos poucos (marcadores de preparo). */
static void heat_activate(void) {
    if (!s_nch) {
        uint32_t mb = 0;
        for (int j = 0; j < MAX_CHARS; j++) {
            Chr* c = &s_ch[s_nch];
            const int k = j % 18, alt = j >= 18;
            snprintf(c->path, sizeof c->path, "mods/remake/%s%s.bin", k_slugs[k], alt ? "_alt" : "");
            if (!rmk_asset_peek(&c->t, c->path)) continue;
            if (c->t.version < 6) c->t.char_index = (uint32_t)k;
            c->alt = alt;
            c->heat = !alt && c->t.char_index == HEAT_INDEX;
            const uint32_t mv = max_active_verts(&c->t);
            if (mv > mb) mb = mv;
            if (c->t.n_verts > s_max_verts) s_max_verts = c->t.n_verts;
            s_nch++;
        }
        psx_mod_counter_add(s_nch ? "bag2_heat.asset_ok" : "bag2_heat.asset_missing", (uint32_t)(s_nch ? s_nch : 1));
        for (int i = 0; i < MAX_INST; i++) s_inst[i].ch = -1;
        for (int i = 0; i < s_nch; i++) {   /* gêmeo de imagem: mesmo modelo e todas as páginas iguais (só a CLUT muda) */
            s_img_twin[i] = -1;
            for (int j = 0; j < s_nch; j++) {
                int same = j != i && twins(i, j) && s_ch[i].t.n_pages == s_ch[j].t.n_pages;
                for (uint32_t p = 0; same && p < s_ch[i].t.n_pages; p++) same = s_ch[i].t.pages[p].hash == s_ch[j].t.pages[p].hash;
                if (same) s_img_twin[i] = j;
            }
        }
        if (s_nch && !s_marks) {
            s_mark_bytes = (uint32_t)(8u + sizeof(PSXModHostDrawHeader) + sizeof(RmkFrame) + 12u * mb + 15u) & ~15u;
            s_marks = psx_mod_alloc_texture_packet_memory(2u * MARKS_PER_HALF * s_mark_bytes, 64u);
            if (!s_marks) psx_mod_counter_add("bag2_heat.marks_fail", 1);
        }
    }
    if (!s_arena) {
        s_arena = psx_mod_alloc_texture_packet_memory(2u * MAX_PKTS * PKT_BYTES, 64u);
        if (!s_arena) psx_mod_counter_add("bag2_heat.arena_fail", 1);
    }
}

/* ------------------------------------------------------------------ árvore de ossos */
static int collect(uint32_t node, uint32_t* out, int n) {
    while (node && n < MAX_NODES && ram_ptr(node)) {
        if (rd32(node + 0xA0) == 1) out[n++] = node;
        n = collect(rd32(node + 0x94), out, n);
        node = rd32(node + 0x98);
    }
    return n;
}

/* Índice-base do mesh do 2º nó com mesh da árvore (assinatura da raiz junto com o nó 0); 0xFFFFFFFF sem ele. */
static uint32_t second_bone_base(uint32_t root) {
    uint32_t nodes[MAX_NODES];
    const int n = collect(root, nodes, 0);
    const uint32_t m1 = n >= 2 ? rd32(nodes[1] + 0x8C) : 0u;
    return ram_ptr(m1) ? rd32(m1 + 0x98) : 0xFFFFFFFFu;
}

/* Nó do jogo de cada posição do corpo e de cada grupo (0 = ausente). */
typedef struct { uint32_t pos[MAX_NODES], grp[RMK_MAX_GROUPS]; } TreeMap;

/* 0 se a árvore não é a do asset; senão a máscara de grupos ativos (bit 0 = corpo, bit g = grupo g) e o nó de cada
 * posição e grupo. Cada nó do jogo é reconhecido pelo mesh (nº de vértices e índice-base do MeshHeader, únicos no
 * modelo), não pela posição na lista (fase 5): posição do corpo (a do índice, senão a primeira livre com o mesmo
 * mesh), ou grupo (mão de gesto, mão alternativa ou, no v7, peça que o jogo pendura numa mão: um nó a mais). Falha
 * com um mesh que o asset não tem ou uma posição do corpo faltando; nas posições de mão a matriz é a do nó do grupo
 * ativo ali. */
static uint32_t tree_groups(const RmkAsset* a, const uint32_t* nodes, int n, TreeMap* tm) {
    if (n < 2 || n > MAX_NODES) return 0;
    uint8_t hand[MAX_NODES] = {0};
    for (uint32_t g = 1; g <= a->n_groups; g++)
        if (a->groups[g].tree_pos < (uint32_t)a->n_drawn) hand[a->groups[g].tree_pos] = 1;
    memset(tm, 0, sizeof *tm);
    uint32_t mask = 1u;
    for (int i = 0; i < n; i++) {
        const uint32_t mesh = rd32(nodes[i] + 0x8C);
        if (!ram_ptr(mesh)) return 0;
        const uint32_t nv = rd32(mesh + 0x94), base = rd32(mesh + 0x98);
        int p = -1;
        if (i < a->n_drawn && !hand[i] && !tm->pos[i] && nv == a->node_nverts[i] && base == a->node_base[i]) p = i;
        for (int q = 0; q < a->n_drawn && p < 0; q++)
            if (!hand[q] && !tm->pos[q] && nv == a->node_nverts[q] && base == a->node_base[q]) p = q;
        if (p >= 0) { tm->pos[p] = nodes[i]; continue; }
        uint32_t g = 1;
        while (g <= a->n_groups && (tm->grp[g] || nv != a->groups[g].sig_nverts || base != a->groups[g].sig_base)) g++;
        if (g > a->n_groups) return 0;
        tm->grp[g] = nodes[i];
        mask |= 1u << g;
        if (a->groups[g].tree_pos < (uint32_t)a->n_drawn && !tm->pos[a->groups[g].tree_pos])
            tm->pos[a->groups[g].tree_pos] = nodes[i];
    }
    /* posição do corpo faltando: falha; posição de grupo sem nenhum grupo ativo (peça que some, como o nó 4 do
     * CHICHI&SALLY): nada dela é desenhado, e a matriz (só para vértices soldados, que são do pai) fica a da raiz */
    for (int q = 0; q < a->n_drawn; q++)
        if (!tm->pos[q]) {
            if (!hand[q]) return 0;
            tm->pos[q] = nodes[0];
        }
    return mask;
}

/* Mesmo modelo (os dois figurinos de vários personagens só trocam a textura): a mesma árvore, mesh a mesh, e os
 * mesmos grupos. Figurinos com uma mão diferente (PANDER) não são gêmeos: o reconhecimento pelo mesh já os separa. */
static int twins(int a, int b) {
    const RmkAsset *x = &s_ch[a].t, *y = &s_ch[b].t;
    if (x->n_drawn != y->n_drawn || x->n_groups != y->n_groups) return 0;
    for (int i = 0; i < x->n_drawn; i++)
        if (x->node_nverts[i] != y->node_nverts[i] || x->node_base[i] != y->node_base[i]) return 0;
    for (uint32_t g = 1; g <= x->n_groups; g++)
        if (x->groups[g].sig_nverts != y->groups[g].sig_nverts || x->groups[g].sig_base != y->groups[g].sig_base ||
            x->groups[g].tree_pos != y->groups[g].tree_pos) return 0;
    return 1;
}
static int has_clut(int c, uint32_t h) {
    for (uint32_t p = 0; p < s_ch[c].t.n_pages; p++) if (s_ch[c].t.pages[p].clut_hash == h) return 1;
    return 0;
}
static int has_image(int c, uint32_t h) {
    for (uint32_t p = 0; p < s_ch[c].t.n_pages; p++) if (s_ch[c].t.pages[p].hash == h) return 1;
    return 0;
}

/* Figurino de uma árvore que bate com dois assets gêmeos (mesmo modelo, só a textura ou só a CLUT muda): a CLUT que
 * os pacotes do personagem usam, ou o TIM nas páginas dele, quando pertence a um figurino só (a página 0 do PANDER,
 * por exemplo, é igual nos dois); sem prova agora (o jogo cobriu aquela VRAM), o já decidido para esta raiz. */
static int pick_costume(uint32_t root, int ch) {
    int other = -1;
    for (int j = 0; j < s_nch && other < 0; j++) if (j != ch && twins(j, ch)) other = j;
    if (other < 0) return ch;
    for (int i = 0; i < MAX_INST; i++) {
        const Inst* in = &s_inst[i];
        if (in->ch < 0 || in->root != root) continue;
        if (in->clut_ok)
            for (uint32_t k = 0; k < s_n_vram_clut; k++)
                if (in->clut_x >= s_vram_clut[k].x && in->clut_x < s_vram_clut[k].x + s_vram_clut[k].w &&
                    in->clut_y >= s_vram_clut[k].y && in->clut_y < s_vram_clut[k].y + s_vram_clut[k].h) {
                    const int a = has_clut(ch, s_vram_clut[k].hash), b = has_clut(other, s_vram_clut[k].hash);
                    if (a != b) return a ? ch : other;
                }
        for (uint32_t p = 0; p < RMK_MAX_PAGES; p++) {
            if (!((in->pages_ok >> p) & 1u)) continue;
            for (uint32_t k = 0; k < s_n_vram_tim; k++)
                if (s_vram_tim[k].x == in->page_vx[p] && s_vram_tim[k].y == in->page_vy[p]) {
                    const int a = has_image(ch, s_vram_tim[k].hash), b = has_image(other, s_vram_tim[k].hash);
                    if (a != b) return a ? ch : other;
                }
        }
        if (in->ch == ch || in->ch == other) return in->ch;
        break;
    }
    return ch;
}

/* Asset da árvore (-1 = nenhum), máscara de grupos e nós por posição/grupo. Rejeita cedo pelo nó 0. */
static int match_tree(const uint32_t* nodes, int n, uint32_t* mask, TreeMap* tm) {
    if (n < 2) return -1;
    const uint32_t m0 = rd32(nodes[0] + 0x8C);
    if (!ram_ptr(m0)) return -1;
    const uint32_t nv0 = rd32(m0 + 0x94), b0 = rd32(m0 + 0x98);
    int seen = 0;
    for (int i = 0; i < s_nch; i++) {
        const RmkAsset* t = &s_ch[i].t;
        if (t->node_nverts[0] != nv0 || t->node_base[0] != b0) continue;
        seen = 1;
        if ((*mask = tree_groups(t, nodes, n, tm)) != 0) return i;
    }
    /* raiz conhecida, árvore não: mesh que o asset não tem; o original aparece nesse quadro */
    if (seen) psx_mod_counter_add("bag2_heat.match_fail", 1);
    return -1;
}

/* quadros por grupo ativo (contadores bag2_heat.grupo<g>, só o HEAT) */
static void count_groups(uint32_t mask) {
    static const char* names[RMK_MAX_GROUPS] = {"bag2_heat.grupo0", "bag2_heat.grupo1", "bag2_heat.grupo2",
        "bag2_heat.grupo3", "bag2_heat.grupo4", "bag2_heat.grupo5", "bag2_heat.grupo6", "bag2_heat.grupo7",
        "bag2_heat.grupo8", "bag2_heat.grupo9", "bag2_heat.grupo10", "bag2_heat.grupo11", "bag2_heat.grupo12",
        "bag2_heat.grupo13", "bag2_heat.grupo14", "bag2_heat.grupo15"};
    for (uint32_t g = 1; g < RMK_MAX_GROUPS; g++)
        if ((mask >> g) & 1u) psx_mod_counter_add(names[g], 1);
}

static void read_bone(uint32_t node, RmkBone* b) {
    int16_t m[9];
    int32_t t[3];
    for (int k = 0; k < 9; k++) m[k] = rd16(node + 0x6C + 2 * k);
    for (int k = 0; k < 3; k++) t[k] = (int32_t)rd32(node + 0x80 + 4 * k);
    rmk_bone_from_gte(b, m, t);
}

/* Matrizes do esqueleto do asset: uma por posição do corpo e, no v7, uma por grupo ativo (osso próprio). */
static void read_bones(const RmkAsset* a, const TreeMap* tm, uint32_t mask, RmkBone* b) {
    for (int p = 0; p < a->n_drawn; p++) read_bone(tm->pos[p], &b[p]);
    if (a->version >= 7)
        for (uint32_t g = 1; g <= a->n_groups; g++)
            if ((mask >> g) & 1u) read_bone(tm->grp[g], &b[a->n_drawn + (int)g - 1]);
}

/* Luzes do palco do quadro (fase 2): o GTE ilumina cada osso com a matriz de luz do nó (+0x4C, direções no
 * espaço do osso), a matriz de cor LCM (gte_ctrl 16..20, uma cor por luz, pode ser negativa) e o ambiente BK
 * (13..15). Com a MATRIX câmera×osso do mesmo nó (+0x6C) as direções vão para a câmera; todos os ossos dão a
 * mesma direção (conferido no heat_dump). O palco põe LCM e BK antes do desenho dos personagens. */
static void read_stage_light(const struct CPUState* cpu, uint32_t node0, Inst* in) {
    int16_t L[9], M[9], lcm[9];
    int32_t bk[3];
    for (int k = 0; k < 9; k++) { L[k] = rd16(node0 + 0x4C + 2 * k); M[k] = rd16(node0 + 0x6C + 2 * k); }
    const uint32_t c16 = cpu->gte_ctrl[16], c17 = cpu->gte_ctrl[17], c18 = cpu->gte_ctrl[18], c19 = cpu->gte_ctrl[19];
    lcm[0] = (int16_t)c16; lcm[1] = (int16_t)(c16 >> 16); lcm[2] = (int16_t)c17;
    lcm[3] = (int16_t)(c17 >> 16); lcm[4] = (int16_t)c18; lcm[5] = (int16_t)(c18 >> 16);
    lcm[6] = (int16_t)c19; lcm[7] = (int16_t)(c19 >> 16); lcm[8] = (int16_t)cpu->gte_ctrl[20];
    for (int k = 0; k < 3; k++) bk[k] = (int32_t)cpu->gte_ctrl[13 + k];
    rmk_stage_light(L, M, lcm, bk, in->stage.ldir, in->stage.lcol, in->stage.bk);
}

float bag2_stage_pulse(void);
float bag2_stage_reflection(float* fade);
void bag2_stage_skip_x(const PSXModHostDrawTarget* t, int skip[2]);

/* Plano do chão (y = 0 do mundo) e direção da sombra do jogo no espaço da câmera: a sombra original é o esqueleto
 * cisalhado para y = 0 ao longo da luz em SHADOW_LIGHT, pela câmera em CAMERA_MATRIX (FUN_8002A46C). A normal fica
 * virada para o personagem (quadril, nó 0). */
static void read_ground(uint32_t node0, Inst* in) {
    int16_t m[9], l[3];
    for (int k = 0; k < 9; k++) m[k] = rd16(CAMERA_MATRIX + 2 * k);
    const uint32_t lp = rd32(SHADOW_LIGHT);
    if (lp < 0x80000000u || lp >= 0x801FFFF8u) { in->ground.ok = 0; return; }
    for (int k = 0; k < 3; k++) l[k] = rd16(lp + 2 * k);
    float n[3], p[3], d[3], h[3];
    for (int i = 0; i < 3; i++) {
        n[i] = (float)m[3 * i + 1] / 4096.0f;
        p[i] = (float)(int32_t)rd32(CAMERA_MATRIX + 0x14 + 4 * i);
        d[i] = ((float)m[3 * i] * l[0] + (float)m[3 * i + 1] * l[1] + (float)m[3 * i + 2] * l[2]) / 4096.0f;
        h[i] = (float)(int32_t)rd32(node0 + 0x80 + 4 * i);
    }
    const float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2], dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    in->ground.ok = nn > 0.25f && dd > 0.0f && l[1] > 0;   /* o jogo só desenha a sombra com y > 0 */
    if (!in->ground.ok) return;
    in->ground.hip = (h[0] - p[0]) * n[0] + (h[1] - p[1]) * n[1] + (h[2] - p[2]) * n[2];
    if (in->ground.hip < 0.0f) { for (int i = 0; i < 3; i++) n[i] = -n[i]; in->ground.hip = -in->ground.hip; }
    memcpy(in->ground.n, n, sizeof n); memcpy(in->ground.p, p, sizeof p); memcpy(in->ground.l, d, sizeof d);
}

static RmkProj gte_proj(const struct CPUState* cpu) {
    RmkProj p;
    p.ofx = (float)(int32_t)cpu->gte_ctrl[24] / 65536.0f;
    p.ofy = (float)(int32_t)cpu->gte_ctrl[25] / 65536.0f;
    p.h = (float)(cpu->gte_ctrl[26] & 0xFFFFu);
    return p;
}

/* ------------------------------------------------------------------ personagens na partida */
static Inst* inst_for(uint32_t root, int ch) {
    const uint32_t fc = psx_mod_read_half(GAME_FRAME);
    Inst* best = NULL;
    for (int i = 0; i < MAX_INST; i++) {
        Inst* in = &s_inst[i];
        if (in->ch >= 0 && in->root == root) {
            if (in->ch == ch) return in;
            best = in;                   /* mesma raiz, outro personagem (nova partida): recomeça */
            break;
        }
    }
    if (!best) {
        for (int i = 0; i < MAX_INST && !best; i++) if (s_inst[i].ch < 0) best = &s_inst[i];
        if (!best) {                     /* nenhum livre: o desenhado há mais tempo */
            best = &s_inst[0];
            for (int i = 1; i < MAX_INST; i++)
                if ((uint16_t)(fc - s_inst[i].seen_fc) > (uint16_t)(fc - best->seen_fc)) best = &s_inst[i];
        }
    }
    float* pos = best->pos;
    memset(best, 0, sizeof *best);
    best->pos = pos ? pos : (float*)calloc(3u * s_max_verts, sizeof(float));
    best->root = root;
    best->ch = ch;
    best->seen_fc = fc;
    best->pages_try_fc = fc - 1000u;
    psx_mod_counter_add("bag2_heat.inst_new", 1);
    return best->pos ? best : NULL;
}

/* Origem de cada página (TIM) deste personagem na VRAM: tpage dos pacotes que o jogo montou para cada osso (nó +0xA8,
 * tamanho em +0xA4; montados uma vez, só xy/cor mudam por quadro) cruzada com as páginas que as faces originais do
 * osso usam no asset. Um osso com uma página só ainda desconhecida e um tpage só ainda sem dono resolve a página. */
static void find_pages(Inst* in, const RmkAsset* a, const uint32_t* nodes, int n) {
    uint16_t tp[MAX_NODES][4];
    int ntp[MAX_NODES];
    for (int i = 0; i < n && i < MAX_NODES; i++) {
        ntp[i] = 0;
        if (i >= (int)a->n_nodes || !a->node_pages[i]) continue;
        const uint32_t mesh = rd32(nodes[i] + 0x8C);
        if (!ram_ptr(mesh) || rd32(mesh + 0x98) != a->node_base[i]) continue;   /* mão de outro gesto */
        const uint32_t b = rd32(nodes[i] + 0xA8), size = rd32(nodes[i] + 0xA4);
        if (!ram_ptr(b) || size > 0x10000u) continue;
        uint32_t p = b;
        for (int k = 0; k < 4096 && p + 8u <= b + size; k++) {
            const uint32_t ln = rd32(p) >> 24, cmd = rd32(p + 4) >> 24;
            if (cmd >= 0x20 && cmd < 0x40 && (cmd & 4)) {   /* polígono com textura: tpage no 2º par de UV */
                const uint16_t t = (uint16_t)(rd32(p + ((cmd & 0x10) ? 24u : 20u)) >> 16) & 0x1FFu;
                if (!in->clut_ok) {             /* CLUT no 1º par de UV */
                    const uint32_t c = rd32(p + 12u) >> 16;
                    in->clut_x = (int16_t)((c & 63u) * 16u); in->clut_y = (int16_t)((c >> 6) & 511u);
                    in->clut_ok = 1;
                }
                int j = 0;
                while (j < ntp[i] && tp[i][j] != t) j++;
                if (j == ntp[i] && j < 4) tp[i][ntp[i]++] = t;
            }
            p += 4u + 4u * ln;
        }
    }
    uint32_t vram_used = 0;   /* tpages já com dono: bit (x/64 + 16 * y/256) */
    for (uint32_t p = 0; p < a->n_pages; p++)
        if ((in->pages_ok >> p) & 1u) vram_used |= 1u << ((in->page_vx[p] >> 6) + 16 * (in->page_vy[p] >> 8));
    for (int pass = 0, progress = 1; progress && pass < 8; pass++) {
        progress = 0;
        for (int i = 0; i < n && i < MAX_NODES; i++) {
            if (i >= (int)a->n_nodes) break;
            const uint32_t want = a->node_pages[i] & ~in->pages_ok & ((1u << a->n_pages) - 1u);
            if (!want || (want & (want - 1u)) || !ntp[i]) continue;
            int free_j = -1, nfree = 0;
            for (int j = 0; j < ntp[i]; j++) {
                const uint32_t bit = 1u << ((tp[i][j] & 15u) + 16u * ((tp[i][j] >> 4) & 1u));
                if (!(vram_used & bit)) { free_j = j; nfree++; }
            }
            if (nfree != 1) continue;
            uint32_t p = 0;
            while (!((want >> p) & 1u)) p++;
            in->page_vx[p] = (int16_t)((tp[i][free_j] & 15u) * 64u);
            in->page_vy[p] = (int16_t)(((tp[i][free_j] >> 4) & 1u) * 256u);
            in->pages_ok |= 1u << p;
            vram_used |= 1u << ((tp[i][free_j] & 15u) + 16u * ((tp[i][free_j] >> 4) & 1u));
            progress = 1;
        }
    }
    /* Página que nenhum pacote do buffer do nó usa (os decalques do rosto saem de outro lugar): o TIM com a mesma
     * impressão digital que o LoadImage pôs na VRAM; com dois (partida espelhada, figurinos de mesma imagem), o mais
     * perto da página 0 deste personagem (as páginas de um jogador ficam juntas: 1P em 704..832, 2P em 832..960). */
    if (in->pages_ok & 1u)
        for (uint32_t p = 1; p < a->n_pages; p++) {
            if ((in->pages_ok >> p) & 1u) continue;
            int best = -1, bd = 1 << 30;
            for (uint32_t k = 0; k < s_n_vram_tim && k < 16; k++) {
                const int c = s_vram_tim[k].ch, pg = s_vram_tim[k].page;
                if (c < 0 || s_ch[c].t.pages[pg].hash != a->pages[p].hash) continue;
                const int d = abs(s_vram_tim[k].x - in->page_vx[0]) + abs(s_vram_tim[k].y - in->page_vy[0]);
                if (d < bd) { bd = d; best = (int)k; }
            }
            if (best < 0) continue;
            in->page_vx[p] = s_vram_tim[best].x; in->page_vy[p] = s_vram_tim[best].y;
            in->pages_ok |= 1u << p;
            psx_mod_counter_add("bag2_heat.pages_from_load", 1);
        }
    psx_mod_counter_add("bag2_heat.pages_scan", 1);
}

/* ================================================================== expressão do rosto */
/* O jogo troca a expressão copiando, na VRAM, uma variante do atlas da página do rosto para a casa que as faces
 * amostram (MoveImage, executado na hora, durante a lógica do quadro). A tabela s_vr diz, por casa (coordenadas da
 * VRAM), onde está o conteúdo atual; no quadro de cada personagem ela vira (página, casa em texels, deslocamento)
 * pelas origens das páginas dele, e o shader do núcleo lê a camada HD dali. Cópia de fora da página (tpage de
 * 64 x 256 halfwords) ou de parte de outra casa: a casa sai da tabela (volta ao conteúdo do disco). */
static int same_tpage(int x0, int y0, int x1, int y1) { return (x0 >> 6) == (x1 >> 6) && (y0 >> 8) == (y1 >> 8); }

/* Casas deste personagem no quadro (até RMK_MAX_REMAP). */
static uint32_t inst_remaps(const Inst* in, const RmkAsset* a, RmkRemap* out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_nvr && n < RMK_MAX_REMAP; i++) {
        const VRemap* r = &s_vr[i];
        for (uint32_t p = 0; p < a->n_pages; p++) {
            if (!((in->pages_ok >> p) & 1u)) continue;
            const RmkPage* g = &a->pages[p];
            const int per = 16 / g->bpp, vx = in->page_vx[p], vy = in->page_vy[p];
            const int pw = (g->w + per - 1) / per;
            if (r->x0 < vx || r->y0 < vy || r->x1 > vx + pw || r->y1 > vy + g->h) continue;
            RmkRemap* o = &out[n++];
            o->page = (uint16_t)p;
            o->x0 = (uint16_t)((r->x0 - vx) * per); o->x1 = (uint16_t)((r->x1 - vx) * per);
            o->y0 = (uint16_t)(r->y0 - vy); o->y1 = (uint16_t)(r->y1 - vy);
            o->du = (int16_t)(r->dx * per); o->dv = r->dy; o->pad = 0;
            break;
        }
    }
    return n;
}

/* Tira da tabela as casas que tocam o retângulo da VRAM; 1 se tirou alguma. */
static int vremap_drop(int x, int y, int w, int h) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < s_nvr; i++) {
        const VRemap* r = &s_vr[i];
        if (r->x0 < x + w && r->x1 > x && r->y0 < y + h && r->y1 > y) continue;
        s_vr[k++] = *r;
    }
    const int dropped = k != s_nvr;
    s_nvr = k;
    return dropped;
}

/* A tabela mudou depois de um personagem ligar o marcador deste quadro e antes de a GPU chegar nele (a cópia sai
 * antes da OT do quadro): regrava o bloco da expressão nos marcadores. Marcador já desenhado: a escrita não faz
 * nada (o payload foi copiado por valor). */
static void remap_changed(void) {
    psx_mod_counter_add("bag2_heat.face_moves", 1);
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    int any = 0;
    for (int i = 0; i < MAX_INST; i++) {
        Inst* in = &s_inst[i];
        if (in->ch < 0 || !in->n_marks || in->marks_parity != parity || parity != s_mark_parity) continue;
        const RmkAsset* a = &s_ch[in->ch].t;
        uint32_t blk[(sizeof(uint32_t) + sizeof(RmkRemap) * RMK_MAX_REMAP) / 4u];
        memset(blk, 0, sizeof blk);
        blk[0] = inst_remaps(in, a, (RmkRemap*)&blk[1]);
        for (uint32_t k = 0; k < in->n_marks; k++)
            write_arena(in->marks[k] + 8u + (uint32_t)sizeof(PSXModHostDrawHeader) + (uint32_t)offsetof(RmkFrame, n_remap),
                        blk, (uint32_t)sizeof blk);
        any = 1;
    }
    psx_mod_counter_add(any ? "bag2_heat.face_move_after" : "bag2_heat.face_move_before", 1);
}

static void heat_move_image(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!s_nch) return;
    const uint32_t rect = cpu->gpr[4];
    const int sx = rd16(rect), sy = rd16(rect + 2), w = rd16(rect + 4), h = rd16(rect + 6);
    const int dx = (int)(int16_t)cpu->gpr[5], dy = (int)(int16_t)cpu->gpr[6];
    if (w <= 0 || h <= 0) return;                     /* MoveImage recusa e devolve -1 */
    VRemap nr;
    int add = 0;
    if (same_tpage(dx, dy, dx + w - 1, dy + h - 1) && same_tpage(sx, sy, sx + w - 1, sy + h - 1) &&
        same_tpage(sx, sy, dx, dy)) {
        /* origem dentro de uma casa já remapeada: o conteúdo dela está no deslocamento daquela casa */
        int ox = sx - dx, oy = sy - dy, partial = 0;
        for (uint32_t i = 0; i < s_nvr; i++) {
            const VRemap* r = &s_vr[i];
            if (r->x0 >= sx + w || r->x1 <= sx || r->y0 >= sy + h || r->y1 <= sy) continue;
            if (r->x0 <= sx && r->x1 >= sx + w && r->y0 <= sy && r->y1 >= sy + h) { ox += r->dx; oy += r->dy; }
            else partial = 1;
        }
        if (partial) psx_mod_counter_add("bag2_heat.face_move_unknown", 1);
        else if (ox || oy) {
            nr.x0 = (int16_t)dx; nr.y0 = (int16_t)dy; nr.x1 = (int16_t)(dx + w); nr.y1 = (int16_t)(dy + h);
            nr.dx = (int16_t)ox; nr.dy = (int16_t)oy;
            add = 1;
        }
    }
    if (add) {   /* só casas dentro de páginas de personagens já achadas (enquanto nenhuma foi achada, todas) */
        int known = 0, inside = 0;
        for (int i = 0; i < MAX_INST; i++) {
            const Inst* in = &s_inst[i];
            if (in->ch < 0 || !in->pages_ok) continue;
            known = 1;
            for (uint32_t p = 0; p < s_ch[in->ch].t.n_pages; p++)
                if (((in->pages_ok >> p) & 1u) && same_tpage(dx, dy, in->page_vx[p], in->page_vy[p])) inside = 1;
        }
        add = !known || inside;
    }
    int changed = vremap_drop(dx, dy, w, h);
    if (add) {
        if (s_nvr == MAX_VREMAP) { memmove(s_vr, s_vr + 1, sizeof(VRemap) * (MAX_VREMAP - 1u)); s_nvr--; }
        s_vr[s_nvr++] = nr;
        changed = 1;
    }
    if (changed) remap_changed();
}

/* LoadImage: tira as casas cobertas e, se a imagem é um TIM de algum personagem com remake (impressão digital de 64
 * palavras espalhadas pela imagem, tools/remake/export_char.tim_hash), começa a carregar o asset dele (tela de
 * carregamento da partida). */
static void heat_load_image(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!s_nch) return;
    const uint32_t rect = cpu->gpr[4], data = cpu->gpr[5];
    const int x = rd16(rect), y = rd16(rect + 2), w = rd16(rect + 4), h = rd16(rect + 6);
    if (vremap_drop(x, y, w, h)) remap_changed();
    if (w > 0 && h > 0) vram_cover(x, y, w, h);
    const uint32_t nb = (uint32_t)(w * h) * 2u;   /* bytes da imagem */
    if ((w == 16 || w == 256) && h <= 32 && h > 0 && ram_ptr(data) && ram_ptr(data + nb - 1u)) {
        /* CLUT (inteira) de um TIM de personagem: guarda onde ela está */
        uint32_t ch_ = 0x811C9DC5u;
        for (uint32_t k = 0; k < nb / 4u; k++) {
            const uint32_t v = rd32(data + 4u * k);
            for (int b = 0; b < 4; b++) ch_ = (ch_ ^ ((v >> (8 * b)) & 0xFFu)) * 0x01000193u;
        }
        int hit = -1;
        for (int i = 0; i < s_nch && hit < 0; i++)
            for (uint32_t p = 0; p < s_ch[i].t.n_pages; p++)
                if (s_ch[i].t.pages[p].clut_hash == ch_) { hit = i; break; }
        if (hit >= 0) {
            if (s_n_vram_clut == 16) { memmove(s_vram_clut, s_vram_clut + 1, 15u * sizeof s_vram_clut[0]); s_n_vram_clut = 15; }
            const uint32_t k = s_n_vram_clut++;
            s_vram_clut[k].x = (int16_t)x; s_vram_clut[k].y = (int16_t)y;
            s_vram_clut[k].w = (int16_t)w; s_vram_clut[k].h = (int16_t)h; s_vram_clut[k].ch = (int8_t)hit;
            s_vram_clut[k].hash = ch_;
            psx_mod_counter_add("bag2_heat.clut_hits", 1);
            /* Carrega só se a imagem do personagem (deste figurino ou do gêmeo com a mesma imagem) também está na
             * VRAM: a CLUT sozinha pode ser igual à de outra tela (o CAPOEIRA era carregado no menu por isso). */
            for (uint32_t j = 0; j < s_n_vram_tim; j++) {
                const int c2 = s_vram_tim[j].ch;
                if (c2 == hit || (c2 >= 0 && s_img_twin[hit] == c2)) {
                    request_load(hit, "bag2_heat.load_clut");
                    break;
                }
            }
        }
        return;
    }
    if (w * h < 256 || !ram_ptr(data) || !ram_ptr(data + nb - 1u)) return;
    uint32_t hsh = 0x811C9DC5u, v0 = rd32(data);
    int uniform = 1;
    for (uint32_t k = 0; k < 64; k++) {
        const uint32_t v = rd32(data + ((k * nb / 64u) & ~3u));
        uniform &= v == v0;
        for (int b = 0; b < 4; b++) hsh = (hsh ^ ((v >> (8 * b)) & 0xFFu)) * 0x01000193u;
    }
    int hit = -1, hit_p = 0;
    /* Só na área dos dançarinos na VRAM (1P em 704..831, 2P em 832..1023, linhas 256..511) e com a amostra não
     * uniforme: a página 0 do CAPOEIRA tem a mesma impressão digital que tiras do vídeo de abertura e uma textura do
     * palco (amostra toda igual), e era carregada em toda partida. */
    if (x >= 704 && y >= 256 && !uniform)
    for (int i = 0; i < s_nch && hit < 0; i++)
        for (uint32_t p = 0; p < s_ch[i].t.n_pages; p++)
            if (s_ch[i].t.pages[p].hash == hsh) { hit = i; hit_p = (int)p; break; }
    if (hit >= 0) {   /* registro para o heat_hits (de onde veio cada carga) */
        static double t0;
        if (!t0) t0 = now_us();
        const uint32_t j = s_nhits++ % 8u;
        s_hits[j].x = (int16_t)x; s_hits[j].y = (int16_t)y; s_hits[j].w = (int16_t)w; s_hits[j].h = (int16_t)h;
        s_hits[j].ch = (int8_t)hit; s_hits[j].page = (int8_t)hit_p; s_hits[j].t = (float)((now_us() - t0) / 1e6);
    }
    if (hit >= 0) {   /* figurino e presença na VRAM: guarda qual TIM está neste retângulo */
        if (s_n_vram_tim == 16) { memmove(s_vram_tim, s_vram_tim + 1, 15u * sizeof s_vram_tim[0]); s_n_vram_tim = 15; }
        const uint32_t k = s_n_vram_tim++;
        s_vram_tim[k].x = (int16_t)x; s_vram_tim[k].y = (int16_t)y;
        s_vram_tim[k].w = (int16_t)w; s_vram_tim[k].h = (int16_t)h;
        s_vram_tim[k].ch = (int8_t)hit; s_vram_tim[k].page = (int8_t)hit_p; s_vram_tim[k].hash = hsh;
        /* o gêmeo de mesma imagem cuja CLUT já chegou (a ordem dos LoadImage pode ser CLUT antes da imagem) */
        for (uint32_t j = 0; j < s_n_vram_clut; j++)
            if (s_vram_clut[j].ch >= 0 && s_img_twin[s_vram_clut[j].ch] == hit) request_load(s_vram_clut[j].ch, "bag2_heat.load_clut");
    }
    const uint32_t k = s_nloads++ % 16u;
    s_loads[k].x = (int16_t)x; s_loads[k].y = (int16_t)y; s_loads[k].w = (int16_t)w; s_loads[k].h = (int16_t)h;
    s_loads[k].hash = hsh; s_loads[k].ch = hit;
    if (hit >= 0) request_load(hit, "bag2_heat.load_tim");
}

/* Savestate salvo no meio da montagem de um quadro: a OT restaurada já liga os marcadores (e pacotes GT3) que o
 * plugin pôs nela antes de salvar, e os cursores das arenas são do host (não voltam com o estado). Gravar de novo
 * nesses endereços faz um marcador apontar para si mesmo, e o DMA da GPU dava ~262 mil voltas nele (15 s de desenho
 * de host). Até a paridade do quadro virar, nada novo nas arenas: os personagens e a sombra saem pelo original. */
static int s_mark_block = 0;
static uint32_t s_mark_block_parity;
static int marks_blocked(void) {
    if (!s_mark_block) return 0;
    if ((rd32(FRAME_PARITY) & 1u) == s_mark_block_parity) return 1;
    s_mark_block = 0;
    return 0;
}

/* Depois de carregar um savestate a VRAM é outra: as casas voltam ao conteúdo do disco até a próxima cópia, e as
 * páginas de cada personagem são achadas de novo. As arenas recomeçam do zero no próximo quadro (marks_blocked). */
static void heat_savestate_loaded(void) {
    s_nvr = 0;
    for (int i = 0; i < MAX_INST; i++) { s_inst[i].n_marks = 0; s_inst[i].pages_ok = 0; }
    s_n_skip = 0;
    s_mark_block = 1;
    s_mark_block_parity = rd32(FRAME_PARITY) & 1u;
    s_mark_parity = 0xFFFFFFFFu;
    s_last_parity = 0xFFFFFFFFu;
}

/* ================================================================== desenho de host (fase 1) */
/* Liga um marcador no slot `slot` da OT do quadro. Layout: tag (1 palavra a seguir), NOP com
 * PSX_MOD_HOST_DRAW_NOP, cabeçalho, RmkFrame + posições (fora do nó: a GPU emulada não lê). Devolve o endereço. */
static uint32_t emit_marker(uint32_t slot, uint16_t flags, const RmkAsset* a, const RmkFrame* fr, const float* pos) {
    static uint32_t buf[(8u + sizeof(PSXModHostDrawHeader) + sizeof(RmkFrame)) / 4u + 3u * 65536u];
    /* pos: vetor completo; vai compacto (corpo + gestos ativos), fr->n_verts vértices */
    if (!s_marks || marks_blocked()) return 0;
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_mark_parity) { s_mark_parity = parity; s_mark_off = 0; }
    if (s_mark_off + s_mark_bytes > MARKS_PER_HALF * s_mark_bytes) return 0;
    const uint32_t addr = s_marks + parity * MARKS_PER_HALF * s_mark_bytes + s_mark_off;
    PSXModHostDrawHeader h;
    memset(&h, 0, sizeof h);
    h.magic = PSX_MOD_HOST_DRAW_MAGIC;
    h.id = HOST_DRAW_ID;
    h.flags = flags;
    h.bytes = fr ? (uint32_t)(sizeof(RmkFrame) + 12u * fr->n_verts) : 0u;
    if (fr) {
        h.bbox[0] = (int32_t)floorf(fr->bbox[0]); h.bbox[1] = (int32_t)floorf(fr->bbox[1]);
        h.bbox[2] = (int32_t)ceilf(fr->bbox[2]); h.bbox[3] = (int32_t)ceilf(fr->bbox[3]);
    } else {
        h.bbox[0] = 0; h.bbox[1] = 0; h.bbox[2] = -1; h.bbox[3] = -1;   /* nada visível: só o callback */
    }
    const uint32_t ot = rd32(OT_PTR) - 0x180u + 4u * slot;
    buf[0] = 1u << 24 | (rd32(ot) & 0xFFFFFFu);
    buf[1] = PSX_MOD_HOST_DRAW_NOP;
    memcpy(&buf[2], &h, sizeof h);
    uint32_t bytes = 8u + (uint32_t)sizeof h;
    if (fr) {
        memcpy((uint8_t*)buf + bytes, fr, sizeof *fr);
        rmk_compact(a, fr->groups, pos, (float*)((uint8_t*)buf + bytes + sizeof *fr));
        bytes += h.bytes;
    }
    write_arena(addr, buf, bytes);
    wr32(ot, addr & 0xFFFFFFu);
    s_mark_off += s_mark_bytes;
    if (!(flags & HD_FLAG_FREE)) s_ch[(flags >> HD_CHAR_SHIFT) & 63].marked = 1;   /* a GPU pode criar recursos dele */
    return addr;
}

static void note_mark(Inst* in, uint32_t addr) {
    const uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (in->marks_parity != parity) { in->marks_parity = parity; in->n_marks = 0; }
    if (addr && in->n_marks < MARKS_PER_HALF) in->marks[in->n_marks++] = addr;
}

static void host_frame(RmkFrame* fr, const Inst* in, const RmkAsset* a, const RmkProj* pr, const RmkFrameInfo* fi,
                       float shift, uint32_t mask) {
    memset(fr, 0, sizeof *fr);
    fr->magic = RMK_FRAME_MAGIC;
    fr->groups = mask;
    fr->n_verts = rmk_active_verts(a, mask);
    fr->serial = ++s_serial;
    fr->flags = ((s_flags & 8) ? RMK_FLAG_FLAT : 0u) | ((s_flags & 16) ? RMK_FLAG_NO_TEX : 0u) |
                ((s_flags & 32) ? RMK_FLAG_NO_NMAP : 0u) | ((s_flags & 64) ? RMK_FLAG_FIXED_LIGHT : 0u) |
                ((s_flags & 128) ? RMK_FLAG_NO_SHADOW : 0u) | ((s_flags & 1024) ? RMK_FLAG_NO_SEMI : 0u);
    memcpy(fr->ldir, in->stage.ldir, sizeof fr->ldir);
    memcpy(fr->lcol, in->stage.lcol, sizeof fr->lcol);
    memcpy(fr->bk, in->stage.bk, sizeof fr->bk);
    fr->ofx = pr->ofx; fr->ofy = pr->ofy; fr->h = pr->h; fr->shift_x = shift;
    float zn = fi->zmin * 0.9f - 16.0f, zf = fi->zmax * 1.1f + 16.0f;
    if (zn < 16.0f) zn = 16.0f;
    if (zf < zn + 64.0f) zf = zn + 64.0f;
    fr->znear = zn; fr->zfar = zf;
    fr->alpha = s_alpha;
    fr->expo = s_light[0]; fr->key = s_light[1]; fr->fill = s_light[2]; fr->rim = s_light[3];
    fr->amb = s_light[4]; fr->rough = s_light[5]; fr->zbias = s_light[6];
    if (!(s_flags & 512)) {          /* luz do palco no tempo da música: a principal e o contraluz sobem na batida */
        const float pulse = bag2_stage_pulse() * s_floor[2];
        fr->key *= 1.0f + pulse; fr->rim *= 1.0f + 1.5f * pulse;
    }
    memcpy(fr->floor_n, in->ground.n, sizeof fr->floor_n);
    memcpy(fr->floor_p, in->ground.p, sizeof fr->floor_p);
    memcpy(fr->floor_l, in->ground.l, sizeof fr->floor_l);
    fr->floor_alpha = s_floor[0]; fr->floor_soft = s_floor[1];
    memcpy(fr->bbox, fi->bbox, sizeof fr->bbox);
    fr->n_remap = inst_remaps(in, a, fr->remap);
    s_zr[0] = fi->zmin; s_zr[1] = fi->zmax;
}

static int use_host_path(void) { return !(s_flags & 4) && s_marks; }

/* Sombra nova no chão: desenho de host, modo 1 ou de carga (nos de comparação o original continua com a sombra
 * dele) e chão lido. */
static int floor_on(const Inst* in) {
    return !(s_flags & 256) && use_host_path() && (s_mode == 1 || s_mode >= 4) && s_mode != 7 && in->ground.ok &&
           !marks_blocked();
}

/* Um personagem pelo desenho de host (shift = deslocamento na tela, para comparação e carga). */
static void host_char(Inst* in, int ch, const RmkProj* pr, uint32_t mask, float shift, int copy) {
    const double t0 = now_us();
    const RmkAsset* a = s_ch[ch].full;
    RmkFrameInfo fi;
    rmk_frame_info(a, in->pos, mask, pr, shift, &fi);
    if (!fi.visible) return;
    int slot = fi.ot_near;
    slot = slot < OT_MIN ? OT_MIN : slot > OT_MAX ? OT_MAX : slot;
    RmkFrame fr;
    const int saved = s_flags;
    if (copy) s_flags ^= s_copy_xor;
    host_frame(&fr, in, a, pr, &fi, shift, mask);
    s_flags = saved;
    const double t1 = now_us();
    const uint32_t addr = emit_marker((uint32_t)slot, (uint16_t)(ch << HD_CHAR_SHIFT), a, &fr, in->pos);
    if (addr) psx_mod_counter_add("bag2_heat.markers", 1);
    note_mark(in, addr);
    /* reflexo no chão brilhante do palco (fase 6): a pose espelhada pelo plano do chão, logo antes da sombra no chão */
    float fade = 0.0f;
    const float refl = copy || !in->ground.ok ? 0.0f : bag2_stage_reflection(&fade);
    if (refl > 0.0f && a->n_verts <= s_max_verts) {
        static float mpos[3u * 65536u];
        const float* n = in->ground.n;
        const float* pp = in->ground.p;
        for (uint32_t i = 0; i < a->n_verts; i++) {
            const float* q = &in->pos[3 * i];
            const float d = 2.0f * ((q[0] - pp[0]) * n[0] + (q[1] - pp[1]) * n[1] + (q[2] - pp[2]) * n[2]);
            mpos[3 * i] = q[0] - d * n[0]; mpos[3 * i + 1] = q[1] - d * n[1]; mpos[3 * i + 2] = q[2] - d * n[2];
        }
        RmkFrameInfo mi;
        rmk_frame_info(a, mpos, mask, pr, shift, &mi);
        RmkFrame mf;
        host_frame(&mf, in, a, pr, &mi, shift, mask);
        mf.flags |= RMK_FLAG_MIRROR | RMK_FLAG_NO_SHADOW;
        mf.floor_alpha = refl;
        mf.floor_soft = fade;
        if (emit_marker(SHADOW_SLOT + 1u, (uint16_t)(ch << HD_CHAR_SHIFT), a, &mf, mpos))
            psx_mod_counter_add("bag2_heat.mirror_marks", 1);
    }
    if (!copy) {                     /* a sombra no chão sai quando o jogo chama a função de sombra deste personagem */
        in->floor_fr = fr;
        in->floor_fc = psx_mod_read_half(GAME_FRAME);
        in->floor_valid = 1;
        memcpy(in->bbox, fi.bbox, sizeof in->bbox);
    }
    psx_mod_counter_add("bag2_heat.us_geometry", (uint32_t)(t1 - t0));
    psx_mod_counter_add("bag2_heat.us_emit", (uint32_t)(now_us() - t1));
    psx_mod_counter_add("bag2_heat.frames", 1);
    psx_mod_counter_add(s_ch[ch].heat ? "bag2_heat.frames_heat" : "bag2_heat.frames_outros", 1);
}

/* Thread de render: apaga os recursos GL de quem está descarregando (state 3) e avisa a thread do jogo (rt_freed),
 * que solta a cópia de CPU. Os marcadores de desenho dele que ainda estavam na fila já passaram (mesma thread, na
 * ordem). */
static volatile uint32_t s_gl_frees;
static void rt_collect(void) {
    for (int i = 0; i < s_nch; i++) {
        Chr* c = &s_ch[i];
        if (c->state != 3 || c->rt_freed) continue;
        if (c->gl) { rmk_gl_destroy(c->gl); c->gl = NULL; s_gl_frees++; }
        free(c->full_pos); c->full_pos = NULL;
        c->rt_ready = 0; c->rt_failed = 0; c->rt_prepares = 0; c->gl_bytes = 0;
        mem_barrier();
        c->rt_freed = 1;
    }
}

/* Thread que tem o contexto GL (render, ou a do jogo num ponto de sincronia). */
static void char_host_draw(const PSXModHostDrawTarget* t, const void* payload, uint32_t bytes) {
    const double t0 = now_us();
    rt_collect();
    if (t->flags & HD_FLAG_FREE) return;
    const int ch = (t->flags >> HD_CHAR_SHIFT) & 63;
    if (ch >= s_nch) return;
    Chr* c = &s_ch[ch];
    if (c->state != 2) return;
    if (!c->gl && !c->rt_failed) {
        c->gl = rmk_gl_create(t->get_proc, BAG2_GLES);
        if (!c->gl) c->rt_failed = 1;
    }
    if (!c->rt_ready && !c->rt_failed) {
        c->rt_prepares++;
        const double tp0 = now_us();
        const int ready = rmk_gl_prepare(c->gl, c->full);
        prep_note(now_us() - tp0, ready);
        if (ready) {
            c->rt_ready = 1;
            c->gl_bytes = rmk_gl_bytes(c->gl);
            /* as camadas já estão na GPU: a cópia de CPU (~130 MB com os mapas de normal) sai, menos quando o caminho
             * por pacotes da fase 0 (HEAT, flag 4 ou modo 7) pediu as camadas (bancos de 15 bits gerados na hora) */
            if (!c->keep_cpu) {
                RmkAsset* a = c->full;
                free(a->layers); a->layers = NULL;
                free(a->nmaps); a->nmaps = NULL;
                free(a->mips); a->mips = NULL;
                free(a->nmips); a->nmips = NULL;
                free(a->stp); a->stp = NULL;
            }
        } else if (*rmk_gl_error(c->gl)) {
            fprintf(stdout, "bag2_heat: desenho de host do %s desligado: %s\n", c->t.name, rmk_gl_error(c->gl));
            fflush(stdout);
            c->rt_failed = 1;
        }
    }
    if ((t->flags & HD_FLAG_PREPARE) || t->pass < 0 || !c->rt_ready) return;
    const RmkAsset* a = c->full;
    const RmkFrame* fr = (const RmkFrame*)payload;
    if (!c->full_pos && !(c->full_pos = (float*)calloc(3u * a->n_verts, sizeof(float)))) return;
    if (!fr || bytes < sizeof *fr || fr->magic != RMK_FRAME_MAGIC || (fr->groups & ~((2u << a->n_groups) - 1u)) ||
        fr->n_verts != rmk_active_verts(a, fr->groups) || bytes < sizeof *fr + 12u * fr->n_verts) return;
    rmk_expand(a, fr->groups, (const float*)(fr + 1), c->full_pos);
    RmkTarget rt;
    rt.fbo = t->fbo;
    rt.ndc[0] = 1.0f / t->x_half;
    rt.ndc[1] = ((float)t->draw_off_x + t->shift + t->x_off) / t->x_half - 1.0f;
    rt.ndc[2] = 1.0f / t->y_half;
    rt.ndc[3] = ((float)t->draw_off_y + t->shift) / t->y_half - 1.0f;
    memcpy(rt.viewport, t->viewport, sizeof rt.viewport);
    memcpy(rt.scissor, t->scissor, sizeof rt.scissor);
    memcpy(rt.bbox, fr->bbox, sizeof rt.bbox);
    bag2_stage_skip_x(t, rt.skip_x);
    rt.pass = t->pass; rt.scale = t->scale;
    GpuTimer* gt = (fr->flags & RMK_FLAG_FLOOR) ? &s_gt_floor : &s_gt_body;
    gt_init(gt, t->get_proc);
    gt_begin(gt);
    if (fr->flags & RMK_FLAG_FLOOR) rmk_gl_draw_floor(c->gl, a, fr, c->full_pos, &rt);
    else rmk_gl_draw(c->gl, a, fr, c->full_pos, &rt);
    gt_end(gt);
    s_rt_draws++;
    c->rt_drawn_us = now_us();
    s_rt_us += c->rt_drawn_us - t0;
    rt_gap_note(c->rt_drawn_us, c->rt_drawn_us - t0);
}

/* Fase 6 (src/bag2_stage.c, geometria do palco): as luzes do palco do dançarino desenhado por último (thread do jogo;
 * as mesmas da luz dele, GTE, no espaço da câmera) e os mapas de sombra dos dançarinos desenhados há pouco (thread de
 * render), para a luz e a sombra no cenário. */
/* Plano do chão (normal para cima, câmera) do dançarino desenhado por último e se a sombra nova no chão dele saiu há
 * pouco (o palco não escurece o chão de novo com o mapa de sombra). 0 = sem chão lido. */
int bag2_heat_ground(float n[3], int* floor_shadow) {
    const uint16_t fc = psx_mod_read_half(GAME_FRAME);
    const Inst* best = NULL;
    int fs = 0;
    for (int i = 0; i < MAX_INST; i++) {
        const Inst* in = &s_inst[i];
        if (in->ch < 0 || !in->ground.ok || (uint16_t)(fc - in->seen_fc) > 30u) continue;
        if ((uint16_t)(fc - in->floor_mark_fc) <= 4u) fs = 1;
        if (!best || (int16_t)(in->seen_fc - best->seen_fc) > 0) best = in;
    }
    if (!best) return 0;
    memcpy(n, best->ground.n, sizeof best->ground.n);
    *floor_shadow = fs;
    return 1;
}

int bag2_heat_stage_lights(float ldir[3][3], float lcol[3][3], float bk[3]) {
    const uint16_t fc = psx_mod_read_half(GAME_FRAME);
    const Inst* best = NULL;
    for (int i = 0; i < MAX_INST; i++) {
        const Inst* in = &s_inst[i];
        if (in->ch < 0 || (uint16_t)(fc - in->seen_fc) > 30u) continue;
        if (!best || (int16_t)(in->seen_fc - best->seen_fc) > 0) best = in;
    }
    if (!best) return 0;
    memcpy(ldir, best->stage.ldir, sizeof best->stage.ldir);
    memcpy(lcol, best->stage.lcol, sizeof best->stage.lcol);
    memcpy(bk, best->stage.bk, sizeof best->stage.bk);
    return 1;
}

int bag2_heat_shadow_views(RmkShadowView* out, int max) {
    const double now = now_us();
    int n = 0;
    for (int i = 0; i < s_nch && n < max; i++) {
        Chr* c = &s_ch[i];
        if (c->state != 2 || !c->rt_ready || !c->gl || now - c->rt_drawn_us > 150e3) continue;
        if (rmk_gl_shadow_view(c->gl, &out[n])) n++;
    }
    return n;
}

/* VBlank (thread do jogo): 4 vezes por segundo decide quem descarrega (keep_loaded) e termina as descargas que a
 * thread de render já liberou. Enquanto alguma
 * espera a GPU, liga um marcador vazio no fim da OT do quadro (fora da partida ninguém mais liga marcadores; nos
 * menus a OT é a mesma da partida). Só quando o contador de quadros anda: no vídeo de abertura a OT não é usada. */
static int s_evict = 1;              /* heat_evict 0: nada descarrega (comparação com a fase 4) */
/* Engasgos (heat_hitch): intervalo entre vblanks do jogo (thread do jogo), que o relatório por segundo esconde (o
 * pacer recupera os quadros perdidos dentro do mesmo segundo). Tempo de cada passo de preparo GL dos personagens
 * (prep_note, thread de render): o último passo é o GenerateMipmap do array inteiro. */
static double s_hitch_max_us, s_hitch_t0, s_prep_max_us, s_prep_mip_us, s_prep_mip_max_us;
static uint32_t s_hitch_n, s_prep_n;
static struct { float t_s, ms; } s_hitch_last[8];
static void prep_note(double us, int ready) {
    s_prep_n++;
    if (us > s_prep_max_us) s_prep_max_us = us;
    if (ready) { s_prep_mip_us = us; if (us > s_prep_mip_max_us) s_prep_mip_max_us = us; }
}
/* thread de render: intervalo entre desenhos de host consecutivos (o jogo desenha 30x/s por personagem: ~33 ms é o
 * normal com um só; com dois, ~16 ms) e duração do maior desenho; separa engasgo do jogo de engasgo do render */
static double s_rt_last_us, s_rt_gap_max_us, s_rt_draw_max_us;
static uint32_t s_rt_gap_n;
static void rt_gap_note(double now, double draw_us) {
    if (s_rt_last_us > 0) {
        const double gap = now - s_rt_last_us;
        if (gap > s_rt_gap_max_us) s_rt_gap_max_us = gap;
        if (gap > 45e3) s_rt_gap_n++;
    }
    if (draw_us > s_rt_draw_max_us) s_rt_draw_max_us = draw_us;
    s_rt_last_us = now;
}
static void hitch_note(double now, double dt_us) {
    if (dt_us > s_hitch_max_us) s_hitch_max_us = dt_us;
    if (dt_us > 25e3) {
        s_hitch_last[s_hitch_n % 8].t_s = (float)((now - s_hitch_t0) / 1e6);
        s_hitch_last[s_hitch_n % 8].ms = (float)(dt_us / 1e3);
        s_hitch_n++;
    }
}
static int heat_hitch_cmd(const char* args, char* reply, int cap) {
    int n = snprintf(reply, cap, "hitch_max_ms=%.1f hitch_n=%u rt_gap_max_ms=%.1f rt_gap_n=%u rt_draw_max_ms=%.1f prep_n=%u "
                     "prep_max_ms=%.1f prep_mip_ms=%.1f prep_mip_max_ms=%.1f ultimos:",
                     s_hitch_max_us / 1e3, s_hitch_n, s_rt_gap_max_us / 1e3, s_rt_gap_n, s_rt_draw_max_us / 1e3, s_prep_n,
                     s_prep_max_us / 1e3, s_prep_mip_us / 1e3, s_prep_mip_max_us / 1e3);
    for (uint32_t i = s_hitch_n > 8 ? s_hitch_n - 8 : 0; i < s_hitch_n && n < cap - 32; i++)
        n += snprintf(reply + n, cap - n, " %.1fs:%.0fms", s_hitch_last[i % 8].t_s, s_hitch_last[i % 8].ms);
    if (args && args[0] == 'z') { s_hitch_max_us = 0; s_hitch_n = 0; s_prep_max_us = 0; s_prep_mip_max_us = 0; s_prep_n = 0;
                                   s_rt_gap_max_us = 0; s_rt_gap_n = 0; s_rt_draw_max_us = 0; }
    return 0;
}
static void heat_vblank(void) {
    {
        static double last_vb;
        const double t = now_us();
        if (!s_hitch_t0) s_hitch_t0 = t;
        if (last_vb > 0) hitch_note(t, t - last_vb);
        last_vb = t;
    }
    if (!s_nch) return;
    static uint16_t last_fc;
    static double last_check;
    const uint16_t fc = psx_mod_read_half(GAME_FRAME);
    const int alive = fc != last_fc;
    last_fc = fc;
    const double now = now_us();
    if (s_evict && now - last_check >= 250e3) {
        last_check = now;
        for (int i = 0; i < s_nch; i++) {
            Chr* c = &s_ch[i];
            if (c->state == 2 && !keep_loaded(i, now)) start_evict(i);
            if (c->state == 3 && (!c->marked || c->rt_freed)) finish_evict(c);
        }
    }
    int pending = 0;
    for (int i = 0; i < s_nch && !pending; i++) pending = s_ch[i].state == 3 && s_ch[i].marked && !s_ch[i].rt_freed;
    if (pending && alive && ram_ptr(rd32(OT_PTR)) && emit_marker(OT_MAX, HD_FLAG_FREE, NULL, NULL, NULL))
        psx_mod_counter_add("bag2_heat.free_marks", 1);
}

/* ================================================================== pacotes GT3 (fase 0, fallback) */
static uint16_t s_white_bank;
static int s_banks_state;            /* 0 ainda não, 1 prontos, -1 falhou */

/* camadas HD em 15 bits, cortadas em bancos de 1024x512 com início de linha em k_bank_rows, para que
 * qualquer janela de 256x256 alinhada a 64 caiba num banco (UV do GT3 tem 8 bits). */
static int ensure_banks(void) {
    if (s_banks_state) return s_banks_state > 0;
    if (!psx_mod_texture_banks_supported()) return 0;   /* tenta de novo no próximo quadro */
    int ok = A.tex_size == 1024;
    uint16_t* band = (uint16_t*)malloc(1024u * 512u * 2u);
    uint16_t* rows = (uint16_t*)malloc(1024u * 1024u * 2u);
    ok = ok && band && rows;
    for (uint32_t l = 0; ok && l < A.n_layers; l++) {
        const uint8_t* px = A.layers + (size_t)1024u * 1024u * 4u * l;
        for (uint32_t i = 0; i < 1024u * 1024u; i++) {
            const uint8_t* p = &px[4 * i];
            uint32_t v = (uint32_t)(p[0] >> 3) | (uint32_t)(p[1] >> 3) << 5 | (uint32_t)(p[2] >> 3) << 10;
            if (!v) v = 0x8000u;              /* preto opaco: bit STP, senão 0x0000 vira transparente */
            rows[i] = (uint16_t)(p[3] >= 128 ? v : 0u);
        }
        for (int k = 0; ok && k < BANKS_PER_LAYER; k++) {
            memset(band, 0, 1024u * 512u * 2u);
            const int r0 = k_bank_rows[k], n = 1024 - r0 < 512 ? 1024 - r0 : 512;
            memcpy(band, rows + (size_t)r0 * 1024u, (size_t)n * 1024u * 2u);
            ok = psx_mod_define_texture_bank((uint16_t)(1 + l * BANKS_PER_LAYER + k), 1024, 512, band);
        }
    }
    if (ok) {   /* banco branco para as faces só de cor (GT3 com texel branco = cor pura) */
        for (int i = 0; i < 64 * 256; i++) band[i] = 0x7FFF;
        s_white_bank = (uint16_t)(1 + A.n_layers * BANKS_PER_LAYER);
        ok = psx_mod_define_texture_bank(s_white_bank, 64, 256, band);
    }
    free(rows); free(band);
    s_banks_state = ok ? 1 : -1;
    psx_mod_counter_add(ok ? "bag2_heat.banks_ok" : "bag2_heat.banks_fail", 1);
    return ok;
}

#define LMAX 6
#define GRID_PTS ((LMAX + 1) * (LMAX + 2) / 2)
#define KIDS_ALL 91                  /* soma de L² para L = 1..6 (cache de cobertura) */

typedef struct { float sx, sy, z; float u, v; float lit[3], base[3], over[3]; } GridPt;
typedef struct { uint32_t tri; uint16_t a, b, c; uint8_t kid, L; float z; } Child;
/* Pacote pronto na memória do host; ligado na OT depois de ordenado por (slot, cor/textura, banco). */
typedef struct { uint32_t w[20]; uint32_t key; } Pkt;

static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 norm(V3 a) { float l = sqrtf(dot(a, a)); return l > 1e-12f ? mul(a, 1.0f / l) : v3(0, 0, -1); }

/* posição do ponto (i, j) na grade de uma face dividida L vezes por aresta */
static int gi(int L, int i, int j) { return i * (L + 1) - i * (i - 1) / 2 + j; }
static int kid_off(int L) { return (L - 1) * L * (2 * L - 1) / 6; }   /* soma de l² para l < L */

static V3* s_P; static V3* s_N;            /* por vértice (espaço da câmera) */
static GridPt* s_grid;                     /* GRID_PTS por face */
static Child* s_kids;
static uint8_t* s_vis;
static Pkt* s_pk;
static uint32_t* s_out;                    /* pacotes na ordem final, copiados de uma vez para a arena */
static int iround(float f) { return (int)(f >= 0 ? f + 0.5f : f - 0.5f); }   /* sem chamada à libm */
static uint32_t* s_order; static uint32_t* s_tmp;
static float s_tol = 0.25f;                /* desvio máximo da corda, em pixels do PS1 */
static uint32_t s_budget = 3300;           /* máximo de triângulos (filhos) por desenho do HEAT */
static float s_tol_used;                   /* tolerância efetiva do último quadro (sobe em closes) */
static float* s_dev; static uint8_t* s_luv;
/* Cobertura do overlay por filho e por nível (UV é fixo): bit 0 = algum texel opaco, bit 1 = todos.
 * Filho todo opaco dispensa a cor base; filho sem texel dispensa o overlay. */
static uint8_t* s_cover;
static const float s_hd_scale = 4.0f;      /* texels HD por texel do PS1 */

static void for_each_kid(int L, void (*fn)(void*, int kid, int a, int b, int c), void* user) {
    int kid = 0;
    for (int i = 0; i < L; i++)
        for (int j = 0; j < L - i; j++) {
            fn(user, kid++, gi(L, i, j), gi(L, i + 1, j), gi(L, i, j + 1));
            if (j < L - i - 1) fn(user, kid++, gi(L, i + 1, j), gi(L, i + 1, j + 1), gi(L, i, j + 1));
        }
}

static void grid_bary(int L, int id, float bb[3]) {
    int i = 0;
    while (gi(L, i + 1, 0) <= id) i++;
    int j = id - gi(L, i, 0);
    bb[1] = (float)i / L; bb[2] = (float)j / L; bb[0] = 1.0f - bb[1] - bb[2];
}

typedef struct { const HTri* t; const uint8_t* am; int L; uint8_t* out; } CoverCtx;
static void cover_kid(void* user, int kid, int a, int b, int c) {
    CoverCtx* cx = (CoverCtx*)user;
    const int ids[3] = {a, b, c};
    float uv[3][2];
    for (int k = 0; k < 3; k++) {
        float bb[3]; grid_bary(cx->L, ids[k], bb);
        uv[k][0] = uv[k][1] = 0;
        for (int q = 0; q < 3; q++) {
            uv[k][0] += bb[q] * (cx->t->uv[q][0] * s_hd_scale + s_hd_scale * 0.5f);
            uv[k][1] += bb[q] * (cx->t->uv[q][1] * s_hd_scale + s_hd_scale * 0.5f);
        }
    }
    int any = 0, all = 1;
    const int S = 6;
    for (int i = 0; i <= S; i++)
        for (int j = 0; j <= S - i; j++) {
            float w1 = (float)i / S, w2 = (float)j / S, w0 = 1.0f - w1 - w2;
            int x = (int)(w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]);
            int y = (int)(w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]);
            x = x < 0 ? 0 : x > 1023 ? 1023 : x; y = y < 0 ? 0 : y > 1023 ? 1023 : y;
            if (cx->am[y * 1024 + x]) any = 1; else all = 0;
        }
    cx->out[kid] = (uint8_t)(any | all << 1);
}

static void compute_cover(void) {
    s_cover = (uint8_t*)calloc((size_t)A.n_tris * KIDS_ALL, 1);
    for (uint32_t f = 0; f < A.n_tris; f++) {
        const HTri* t = &A.tris[f];
        if ((t->mode & 3) == 0 || t->layer < 0 || !s_amask[t->layer]) continue;
        for (int L = 1; L <= LMAX; L++) {
            CoverCtx cx = {t, s_amask[t->layer], L, s_cover + (size_t)f * KIDS_ALL + kid_off(L)};
            for_each_kid(L, cover_kid, &cx);
        }
    }
}

/* luz por vértice no espaço da câmera do GTE (x direita, y para baixo, z para dentro da tela):
 * principal quente de cima/esquerda/frente, preenchimento frio da direita, contraluz por trás */
static V3 s_lk, s_lf, s_lr;
static void shade(V3 n, V3 p, float ao, float out[3]) {
    static const float key[3] = {1.05f, 0.97f, 0.88f}, fill[3] = {0.30f, 0.36f, 0.50f};
    V3 vw = norm(mul(p, -1.0f));
    float dk = (dot(n, s_lk) + 0.25f) / 1.25f; if (dk < 0) dk = 0;
    float df = (dot(n, s_lf) + 0.5f) / 1.5f; if (df < 0) df = 0;
    float nv = dot(n, vw); if (nv < 0) nv = 0;
    float r1 = 1.0f - nv, rim = r1 * r1 * r1 * fmaxf(dot(n, s_lr) * 0.5f + 0.5f, 0.0f);
    float sh = fmaxf(dot(n, norm(add(s_lk, vw))), 0.0f);
    sh *= sh; sh *= sh; sh *= sh;                 /* ^8 */
    float spec = sh * sh * sh * 0.18f;            /* ^24 */
    float occ = 0.55f + 0.45f * ao;
    for (int i = 0; i < 3; i++) {
        float amb = (0.30f + 0.10f * (i == 2)) * occ;
        out[i] = amb + key[i] * dk * (0.75f + 0.25f * ao) + fill[i] * df + rim * 0.45f + spec;
    }
}

static uint32_t rgb(float r, float g, float b) {
    int R = (int)(r + 0.5f), G = (int)(g + 0.5f), B = (int)(b + 0.5f);
    R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
    return (uint32_t)R | (uint32_t)G << 8 | (uint32_t)B << 16;
}

/* GT3 com banco de textura e sufixo de precisão. Ordem: a mesma do jogo. Cada filho herda o slot da OT
 * da face original ((z0 + z1) >> 3 dos dois primeiros vértices da primitiva) e, dentro do slot, a ordem
 * de inserção do jogo (faces exportadas nessa ordem). O modelo tem camadas coplanares (decalques sobre a
 * calça e as mangas) que só ficam certas nessa ordem; ordenar pelo Z de cada triângulo embaralha as
 * camadas. Dentro de uma face: overlays ligados antes da cor base (o último ligado sai primeiro, então a
 * base é desenhada e a textura vai por cima, como o pacote G3+GT3 do jogo). Faces seguidas costumam usar
 * o mesmo banco, o que mantém os lotes do GL grandes. */
static void prepare(Pkt* pk, int oi, uint32_t face, const GridPt* g[3], int textured, uint16_t layer_bank0) {
    int x0 = 0, y0 = 0, ybit = 0; uint16_t bank = s_white_bank;
    if (textured) {
        float umin = fminf(fminf(g[0]->u, g[1]->u), g[2]->u), vmin = fminf(fminf(g[0]->v, g[1]->v), g[2]->v);
        x0 = ((int)umin / 64) * 64; if (x0 > 768) x0 = 768; if (x0 < 0) x0 = 0;
        y0 = ((int)vmin / 64) * 64; if (y0 > 960) y0 = 960; if (y0 < 0) y0 = 0;
        int o = (y0 % 512) < 256 ? y0 : y0 - 256;
        ybit = o != y0;
        int k = 0; while (k < BANKS_PER_LAYER - 1 && k_bank_rows[k] != o) k++;
        bank = (uint16_t)(layer_bank0 + k);
    }
    uint32_t* w = pk->w;
    const float sc = 128.0f / 255.0f;
    for (int i = 0; i < 3; i++) {
        int uu = textured ? iround(g[i]->u) - x0 : 8, vv = textured ? iround(g[i]->v) - y0 : 8;
        uint32_t u = (uint32_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu), v = (uint32_t)(vv < 0 ? 0 : vv > 255 ? 255 : vv);
        const float* c = textured ? g[i]->over : g[i]->base;
        const float s = textured ? 1.0f : sc;
        w[1 + i * 3] = rgb(c[0] * s * g[i]->lit[0], c[1] * s * g[i]->lit[1], c[2] * s * g[i]->lit[2]);
        int x = iround(g[i]->sx), y = iround(g[i]->sy);
        w[2 + i * 3] = (uint32_t)(uint16_t)(int16_t)x | (uint32_t)(uint16_t)(int16_t)y << 16;
        w[3 + i * 3] = v << 8 | u;
    }
    w[1] |= 0x34u << 24;
    w[4] |= (uint32_t)(bank & 0xFF) << 24;
    w[7] |= (uint32_t)(bank >> 8) << 24;
    w[6] |= ((uint32_t)(x0 / 64) | (uint32_t)ybit << 4 | 2u << 7) << 16;   /* tpage: 15 bits, opaco */
    w[10] = 0x48545031u;
    float fl[9] = {1.0f / g[0]->z, 1.0f / g[1]->z, 1.0f / g[2]->z,
                   g[0]->sx, g[0]->sy, g[1]->sx, g[1]->sy, g[2]->sx, g[2]->sy};
    memcpy(&w[11], fl, sizeof fl);
    if (s_flags & 2)   /* dentro do slot: overlays agrupados por banco, depois as bases (menos trocas de textura) */
        pk->key = (uint32_t)oi << 16 | (uint32_t)(!textured) << 15 | bank;
    else               /* dentro do slot: ordem de inserção do jogo */
        pk->key = (uint32_t)oi << 16 | (face & 0x7FFFu) << 1 | (uint32_t)(!textured);
}

static void radix_sort(uint32_t n) {   /* s_order por s_pk[].key, duas passadas de 16 bits */
    static uint32_t cnt[65536];
    uint32_t* src = s_order; uint32_t* dst = s_tmp;
    for (int pass = 0; pass < 2; pass++) {
        const int sh = pass * 16;
        memset(cnt, 0, sizeof cnt);
        for (uint32_t i = 0; i < n; i++) cnt[(s_pk[src[i]].key >> sh) & 0xFFFF]++;
        uint32_t sum = 0;
        for (int i = 0; i < 65536; i++) { uint32_t c = cnt[i]; cnt[i] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(s_pk[src[i]].key >> sh) & 0xFFFF]++] = src[i];
        uint32_t* t = src; src = dst; dst = t;
    }
}

typedef struct { uint32_t f; const GridPt* g; int L; uint32_t* nk; } KidCtx;
static void add_kid(void* user, int kid, int a, int b, int c) {
    KidCtx* cx = (KidCtx*)user;
    Child* ch = &s_kids[(*cx->nk)++];
    ch->tri = cx->f; ch->a = (uint16_t)a; ch->b = (uint16_t)b; ch->c = (uint16_t)c;
    ch->kid = (uint8_t)kid; ch->L = (uint8_t)cx->L;
    ch->z = (cx->g[a].z + cx->g[b].z + cx->g[c].z) / 3.0f;
}

static void draw_heat_packets(const struct CPUState* cpu, const TreeMap* tm, uint32_t mask) {
    const double t0 = now_us();
    RmkBone M[MAX_NODES];
    read_bones(&A, tm, mask, M);
    const RmkProj pr = gte_proj(cpu);
    const float ofx = pr.ofx, ofy = pr.ofy, H = pr.h;
    const int Lmax = s_level < 1 ? 1 : s_level > LMAX ? LMAX : s_level;
    if (!s_P) {
        s_P = (V3*)malloc(sizeof(V3) * A.n_verts); s_N = (V3*)malloc(sizeof(V3) * A.n_verts);
        s_grid = (GridPt*)malloc(sizeof(GridPt) * A.n_tris * GRID_PTS);
        s_kids = (Child*)malloc(sizeof(Child) * A.n_tris * LMAX * LMAX);
        s_vis = (uint8_t*)malloc(A.n_tris);
        s_dev = (float*)malloc(sizeof(float) * A.n_tris);
        s_luv = (uint8_t*)malloc(A.n_tris);
        s_pk = (Pkt*)malloc(sizeof(Pkt) * MAX_PKTS);
        s_order = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_tmp = (uint32_t*)malloc(sizeof(uint32_t) * MAX_PKTS);
        s_out = (uint32_t*)malloc(PKT_BYTES * MAX_PKTS);
        s_lk = norm(v3(-0.45f, -0.80f, -0.55f)); s_lf = norm(v3(0.75f, -0.15f, -0.55f)); s_lr = norm(v3(0.25f, -0.45f, 1.0f));
        compute_cover();
    }
    rmk_skin(&A, M, A.n_bones, mask, (float*)s_P);
    for (uint32_t i = 0; i < A.n_verts; i++) s_N[i] = v3(0, 0, 0);
    /* visibilidade (NCLIP > 0, como FUN_80036230) e normais por vértice */
    float orient = 0;
    for (uint32_t f = 0; f < A.n_tris; f++) {
        const HTri* t = &A.tris[f];
        s_vis[f] = 0;
        if (!RMK_FACE_ON(t, mask)) continue;
        V3 a = s_P[t->v[0]], b = s_P[t->v[1]], c = s_P[t->v[2]];
        V3 fn = cross(sub(b, a), sub(c, a));
        for (int k = 0; k < 3; k++) s_N[t->v[k]] = add(s_N[t->v[k]], fn);
        if (a.z < 64 || b.z < 64 || c.z < 64) continue;
        float ax = a.x / a.z, ay = a.y / a.z, bx = b.x / b.z, by = b.y / b.z, cx = c.x / c.z, cy = c.y / c.z;
        if ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) > 0) { s_vis[f] = 1; orient += dot(fn, mul(add(add(a, b), c), -1.0f)); }
    }
    const float sgn = orient < 0 ? -1.0f : 1.0f;   /* normais da face visível apontam para a câmera */
    for (uint32_t i = 0; i < A.n_verts; i++) s_N[i] = norm(mul(s_N[i], sgn));
    /* grade de Phong por face visível, com nível adaptativo: divide só onde a curva de Phong se afasta da
     * corda mais que s_tol pixels (o desvio cai com L², daí a raiz). Arestas vizinhas com níveis diferentes
     * deixam uma junção em T com fresta menor que s_tol. */
    /* Passo A: desvio de Phong (pixels do PS1) e mínimo de divisões pelo UV de cada face visível. */
    uint32_t n_uvforced = 0, n_vis = 0, n_tex = 0, n_base = 0;
    for (uint32_t f = 0; f < A.n_tris; f++) {
        if (!s_vis[f]) continue;
        n_vis++;
        const HTri* t = &A.tris[f];
        V3 P[3], N[3];
        for (int k = 0; k < 3; k++) { P[k] = s_P[t->v[k]]; N[k] = s_N[t->v[k]]; }
        float dev = 0;
        for (int e = 0; e < 4; e++) {   /* meios das 3 arestas e o centro */
            V3 m = e < 3 ? mul(add(P[e], P[(e + 1) % 3]), 0.5f) : mul(add(add(P[0], P[1]), P[2]), 1.0f / 3.0f);
            V3 ph = v3(0, 0, 0);
            const int cnt = e < 3 ? 2 : 3;
            for (int k = 0; k < cnt; k++) {
                const int vi = e < 3 ? (e + k) % 3 : k;
                ph = add(ph, sub(m, mul(N[vi], dot(sub(m, P[vi]), N[vi]))));
            }
            V3 d = mul(sub(mul(ph, 1.0f / cnt), m), s_alpha);
            float px = sqrtf(dot(d, d)) * H / (m.z > 64 ? m.z : 64);
            if (px > dev) dev = px;
        }
        s_dev[f] = dev;
        s_luv[f] = 1;
        if ((t->mode & 3) != 0 && t->layer >= 0) {
            /* o UV do GT3 tem 8 bits: cada pedaço tem de caber numa janela de 256 texels HD alinhada a 64,
             * ou seja, extensão <= 192; senão o UV satura e a textura sai em cacos */
            float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
            for (int k = 0; k < 3; k++) {
                umin = fminf(umin, t->uv[k][0]); umax = fmaxf(umax, t->uv[k][0]);
                vmin = fminf(vmin, t->uv[k][1]); vmax = fmaxf(vmax, t->uv[k][1]);
            }
            int Luv = (int)ceilf(fmaxf(umax - umin, vmax - vmin) * s_hd_scale / 192.0f);
            s_luv[f] = (uint8_t)(Luv < 1 ? 1 : Luv > LMAX ? LMAX : Luv);
        }
    }
    /* Orçamento: o custo do quadro cresce com o nº de pacotes (a GPU emulada gasta ~0,2 us por palavra de
     * comando), e num close o mesmo s_tol pediria muito mais divisões. Se a soma de L² passar do orçamento,
     * a tolerância sobe (L² ~ desvio/tol) até caber. */
    float tol = s_tol;
    for (int it = 0; it < 6; it++) {
        uint32_t total = 0;
        for (uint32_t f = 0; f < A.n_tris; f++) {
            if (!s_vis[f]) continue;
            int L = (int)ceilf(sqrtf(s_dev[f] / tol));
            L = L < 1 ? 1 : L > Lmax ? Lmax : L;
            if (s_luv[f] > L) L = s_luv[f];
            total += (uint32_t)(L * L);
        }
        if (total <= s_budget) break;
        tol *= (float)total / (float)s_budget * 1.05f;
    }
    s_tol_used = tol;
    uint32_t nk = 0;
    float bb4[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (uint32_t f = 0; f < A.n_tris; f++) {
        if (!s_vis[f]) continue;
        const HTri* t = &A.tris[f];
        V3 P[3], N[3]; float ao[3];
        for (int k = 0; k < 3; k++) { P[k] = s_P[t->v[k]]; N[k] = s_N[t->v[k]]; ao[k] = A.verts[t->v[k]].ao / 255.0f; }
        int L = (int)ceilf(sqrtf(s_dev[f] / tol));
        L = L < 1 ? 1 : L > Lmax ? Lmax : L;
        if (s_luv[f] > L) { L = s_luv[f]; n_uvforced++; }
        GridPt* g = &s_grid[(size_t)f * GRID_PTS];
        int idx = 0;
        for (int i = 0; i <= L; i++)
            for (int j = 0; j <= L - i; j++, idx++) {
                float b1 = (float)i / L, b2 = (float)j / L, b0 = 1.0f - b1 - b2, bb[3] = {b0, b1, b2};
                V3 q = add(add(mul(P[0], b0), mul(P[1], b1)), mul(P[2], b2));
                V3 ph = v3(0, 0, 0);
                for (int k = 0; k < 3; k++) ph = add(ph, mul(sub(q, mul(N[k], dot(sub(q, P[k]), N[k]))), bb[k]));
                V3 p = add(mul(q, 1.0f - s_alpha), mul(ph, s_alpha));
                V3 nn = norm(add(add(mul(N[0], b0), mul(N[1], b1)), mul(N[2], b2)));
                if (p.z < 32) p.z = 32;
                GridPt* gp = &g[idx];
                gp->z = p.z; gp->sx = ofx + H * p.x / p.z + s_shift; gp->sy = ofy + H * p.y / p.z;
                if (gp->sx < bb4[0]) bb4[0] = gp->sx; if (gp->sy < bb4[1]) bb4[1] = gp->sy;
                if (gp->sx > bb4[2]) bb4[2] = gp->sx; if (gp->sy > bb4[3]) bb4[3] = gp->sy;
                gp->u = gp->v = 0;
                float a = 0;
                for (int q2 = 0; q2 < 3; q2++) gp->base[q2] = gp->over[q2] = 0;
                for (int k = 0; k < 3; k++) {
                    gp->u += bb[k] * (t->uv[k][0] * s_hd_scale + s_hd_scale * 0.5f);
                    gp->v += bb[k] * (t->uv[k][1] * s_hd_scale + s_hd_scale * 0.5f);
                    a += bb[k] * ao[k];
                    for (int q2 = 0; q2 < 3; q2++) { gp->base[q2] += bb[k] * t->base[k][q2]; gp->over[q2] += bb[k] * t->over[k][q2]; }
                }
                shade(nn, p, a, gp->lit);
            }
        KidCtx kc = {f, g, L, &nk};
        for_each_kid(L, add_kid, &kc);
    }
    memcpy(s_bbox, bb4, sizeof bb4);
    const double t1 = now_us();
    uint32_t np = 0;
    for (uint32_t k = 0; k < nk && np + 2 <= MAX_PKTS; k++) {
        const Child* ch = &s_kids[k];
        const HTri* t = &A.tris[ch->tri];
        const GridPt* gg = &s_grid[(size_t)ch->tri * GRID_PTS];
        const GridPt* g[3] = {&gg[ch->a], &gg[ch->b], &gg[ch->c]};
        int oi = ((int)s_P[t->zp[0]].z + (int)s_P[t->zp[1]].z) >> 3;   /* como FUN_80036230 */
        oi = oi < OT_MIN ? OT_MIN : oi > OT_MAX ? OT_MAX : oi;
        const uint8_t cov = ((t->mode & 3) != 0 && t->layer >= 0) ? s_cover[(size_t)ch->tri * KIDS_ALL + kid_off(ch->L) + ch->kid] : 0;
        if (cov & 1) {                                        /* textura (sobre a cor base no modo 1) */
            prepare(&s_pk[np], oi, ch->tri, g, 1, (uint16_t)(1 + t->layer * BANKS_PER_LAYER)); s_order[np] = np; np++; n_tex++;
        }
        if ((t->mode & 3) == 0 || ((t->mode & 3) == 1 && !(cov & 2) && !(s_flags & 1))) {   /* cor do material: GT3 com texel branco */
            prepare(&s_pk[np], oi, ch->tri, g, 0, 0); s_order[np] = np; np++; n_base++;
        }
    }
    radix_sort(np);
    const double t15 = now_us();
    uint32_t parity = rd32(FRAME_PARITY) & 1u;
    if (parity != s_last_parity) { s_last_parity = parity; s_arena_off = 0; }
    const uint32_t half = parity * MAX_PKTS * PKT_BYTES;
    const uint32_t ot_base = rd32(OT_PTR) - 0x180u;
    /* Liga na ordem ordenada: pacotes do mesmo slot ficam seguidos, então a corrente de cada slot é montada
     * no host e a OT do jogo é lida e escrita uma vez por slot; os pacotes vão para a arena numa cópia só
     * (psx_mod_write_gpu_dma, patch local do framework), em vez de 20 stores por pacote. */
    uint32_t emitted = (MAX_PKTS * PKT_BYTES - s_arena_off) / PKT_BYTES;
    if (emitted > np) emitted = np;
    const uint32_t base_addr = s_arena + half + s_arena_off;
    for (uint32_t k = 0; k < emitted;) {
        const uint32_t slot = s_pk[s_order[k]].key >> 16, ot = ot_base + 4u * slot;
        uint32_t prev = rd32(ot) & 0xFFFFFFu;
        for (; k < emitted && (s_pk[s_order[k]].key >> 16) == slot; k++) {
            uint32_t* w = &s_out[(size_t)k * (PKT_BYTES / 4u)];
            memcpy(w, s_pk[s_order[k]].w, PKT_BYTES);
            w[0] = 9u << 24 | prev;
            prev = (base_addr + k * PKT_BYTES) & 0xFFFFFFu;
        }
        wr32(ot, prev);
    }
    if (emitted) write_arena(base_addr, s_out, emitted * PKT_BYTES);
    s_arena_off += emitted * PKT_BYTES;
    const double t2 = now_us();
    psx_mod_counter_add("bag2_heat.us_geometry", (uint32_t)(t1 - t0));
    psx_mod_counter_add("bag2_heat.us_prepare", (uint32_t)(t15 - t1));
    psx_mod_counter_add("bag2_heat.us_emit", (uint32_t)(t2 - t15));
    psx_mod_counter_add("bag2_heat.packets", emitted);
    psx_mod_counter_add("bag2_heat.children", nk);
    psx_mod_counter_add("bag2_heat.faces_visible", n_vis);
    psx_mod_counter_add("bag2_heat.faces_uvforced", n_uvforced);
    psx_mod_counter_add("bag2_heat.pk_textured", n_tex);
    psx_mod_counter_add("bag2_heat.pk_base", n_base);
    psx_mod_counter_add("bag2_heat.frames", 1);
}

/* ------------------------------------------------------------------ hook */
static void dump_tree(struct CPUState* cpu) {
    FILE* f = fopen(s_dump_path, "a");
    if (!f) return;
    uint32_t nodes[MAX_NODES];
    int n = collect(cpu->gpr[4], nodes, 0);
    uint32_t mask = 0;
    TreeMap tm;
    const int ch = match_tree(nodes, n, &mask, &tm);
    fprintf(f, "=== draw_tree root=%08X ra=%08X s2=%08X s2+4=%02X char=%s grupos=%X\ngte_ctrl:", cpu->gpr[4], cpu->gpr[31],
            cpu->gpr[18], psx_mod_read_byte(cpu->gpr[18] + 4), ch >= 0 ? s_ch[ch].t.name : "-", mask);
    for (int i = 0; i < 32; i++) fprintf(f, " %d:%08X", i, cpu->gte_ctrl[i]);
    fprintf(f, "\n");
    for (int i = 0; i < n; i++) {
        uint32_t mesh = rd32(nodes[i] + 0x8C);
        fprintf(f, "node %d addr=%08X mesh=%08X nverts=%u base=%u\n  M=", i, nodes[i], mesh, rd32(mesh + 0x94), rd32(mesh + 0x98));
        for (int k = 0; k < 9; k++) fprintf(f, " %d", rd16(nodes[i] + 0x6C + k * 2));
        fprintf(f, " T= %d %d %d\n  L=", (int32_t)rd32(nodes[i] + 0x80), (int32_t)rd32(nodes[i] + 0x84), (int32_t)rd32(nodes[i] + 0x88));
        for (int k = 0; k < 9; k++) fprintf(f, " %d", rd16(nodes[i] + 0x4C + k * 2));   /* matriz de luz do nó */
        fprintf(f, "\n");
    }
    fclose(f);
}

static const Chr* heat_chr(void) {
    for (int i = 0; i < s_nch; i++) if (s_ch[i].heat) return &s_ch[i];
    return NULL;
}

/* Teste: faz o que o script da dança faz num gesto (liga o nó da mão do gesto como irmão do antebraço,
 * nó 15/18 +0x98), na thread do jogo, antes de desenhar. Os nós do HEAT ficam num vetor de 0xB0 bytes a
 * partir da raiz (nó 0). Só na árvore do HEAT (MeshHeader da raiz e do nó 16 conferidos). */
static void force_gesture(uint32_t root) {
    static const int fore[2] = {15, 18}, hand[2][3] = {{16, 20, 21}, {19, 22, 23}};
    const Chr* h = heat_chr();
    if (!h) return;
    const uint32_t m0 = rd32(root + 0x8C), m16 = rd32(root + 16u * NODE_SIZE + 0x8C);
    if (!ram_ptr(m0) || rd32(m0 + 0x98) != h->t.node_base[0] || !ram_ptr(m16) || rd32(m16 + 0x98) != h->t.node_base[16]) return;
    for (int s = 0; s < 2; s++) {
        if (s_force[s] < 0 || s_force[s] > 2) continue;
        wr32(root + (uint32_t)fore[s] * NODE_SIZE + 0x98u, root + (uint32_t)hand[s][s_force[s]] * NODE_SIZE);
    }
}

/* Desenha o personagem `ch` (árvore `nodes`, raiz `root`) pelo remake. 1 = pular o original. d4c: veio de
 * FUN_80029D4C (o caminho por pacotes e o modo 7 só existem aí, para o HEAT). */
static int draw_char(struct CPUState* cpu, uint32_t root, const TreeMap* tm, int ch, uint32_t mask, int d4c) {
    Chr* c = &s_ch[ch];
    const int heat = c->heat;
    c->last_use_us = now_us();
    if (c->state == 0) request_load(ch, "bag2_heat.load_draw");
    if (c->state != 2) return 0;
    c->drawn = 1;
    if (marks_blocked()) return 0;   /* quadro do savestate carregado: o original */
    const int host = use_host_path() && !c->rt_failed;
    /* Recursos GL do desenho de host: criados aos poucos, por marcadores de preparo deste personagem; até ficarem
     * prontos, o original continua. */
    if (host && !c->rt_ready) {
        if (emit_marker(OT_MAX, (uint16_t)(HD_FLAG_PREPARE | ch << HD_CHAR_SHIFT), NULL, NULL, NULL))
            psx_mod_counter_add("bag2_heat.prepare_marks", 1);
        return 0;
    }
    if ((s_flags & 2048) && !heat) return 0;   /* só o HEAT (comparação): os outros prontos, mas originais */
    const int packets = (!host || s_mode == 7) && heat && d4c;
    if (!host && !packets) return 0;
    if (packets) {
        if (!c->full->layers) {      /* a cópia de CPU saiu depois da subida: carrega de novo mantendo as camadas */
            c->keep_cpu = 1;
            start_evict(ch);
            return 0;
        }
        packets_asset();
    }
    Inst* in = inst_for(root, ch);
    if (!in) return 0;
    const uint32_t fc = psx_mod_read_half(GAME_FRAME);
    in->seen_fc = fc;
    in->mask = mask;
    const RmkAsset* a = c->full;
    if (heat) { count_groups(mask); s_heat_root = root; s_last_mask = mask; }
    read_stage_light(cpu, tm->pos[0], in);
    read_ground(tm->pos[0], in);
    if (in->pages_ok != (1u << a->n_pages) - 1u && (uint16_t)(fc - in->pages_try_fc) >= 30u) {
        in->pages_try_fc = fc;
        find_pages(in, a, tm->pos, a->n_drawn);
    }
    if (packets && (!s_arena || !ensure_banks())) return 0;
    const float w = (in->bbox[2] > in->bbox[0] ? in->bbox[2] - in->bbox[0] : 60.0f) * 1.05f;
    RmkBone M[MAX_NODES];
    read_bones(a, tm, mask, M);
    rmk_skin(a, M, a->n_bones, mask, in->pos);
    const RmkProj pr = gte_proj(cpu);
    if (s_mode == 7) {   /* comparação de 3: original, pacotes (fase 0) à direita, desenho de host mais à direita */
        s_shift = w;
        draw_heat_packets(cpu, tm, mask);
        s_shift = 0;
        if (host) host_char(in, ch, &pr, mask, 2.0f * w, 0);
        memcpy(s_bbox, in->bbox, sizeof s_bbox);
        return 0;
    }
    const int copies = s_mode >= 4 ? s_mode - 3 : 0;   /* teste de carga: cópias do remake ao lado */
    s_shift = s_mode == 3 ? w : 0.0f;
    in->shift = s_shift;
    if (heat) s_last_shift = s_shift;
    if (host) {
        if (heat && a->n_drawn > 19) {   /* para recortar as mãos (heat_mode) e o rosto (heat_face) nas capturas */
            for (int k = 0; k < 3; k++) {
                const RmkBone* b = &M[k == 2 ? 13 : k ? 19 : 16];
                float* o = k == 2 ? s_head_xy : s_hand_xy[k];
                const float z = b->t[2] > 32.0f ? b->t[2] : 32.0f;
                o[0] = pr.ofx + pr.h * b->t[0] / z; o[1] = pr.ofy + pr.h * b->t[1] / z;
                o[2] = z;
            }
            s_head_xy[3] = pr.h;
        }
        {   /* cabeça (o osso com o maior mesh) na tela: centro e raio dos vértices dela, para recortar o rosto (heat_chars) */
            int hp = 0;
            for (int q = 1; q < a->n_drawn; q++) if (a->node_nverts[q] > a->node_nverts[hp]) hp = q;
            float c[3] = {0, 0, 0}, r = 0.0f;
            uint32_t k = 0;
            for (uint32_t i = 0; i < a->n_body; i++)
                if (a->verts[i].owner == hp) { c[0] += in->pos[3 * i]; c[1] += in->pos[3 * i + 1]; c[2] += in->pos[3 * i + 2]; k++; }
            if (k) {
                for (int j = 0; j < 3; j++) c[j] /= (float)k;
                for (uint32_t i = 0; i < a->n_body; i++)
                    if (a->verts[i].owner == hp) {
                        const float dx = in->pos[3 * i] - c[0], dy = in->pos[3 * i + 1] - c[1];
                        if (dx * dx + dy * dy > r) r = dx * dx + dy * dy;
                    }
                const float z = c[2] > 32.0f ? c[2] : 32.0f;
                in->head_xy[0] = pr.ofx + pr.h * c[0] / z + s_shift; in->head_xy[1] = pr.ofy + pr.h * c[1] / z;
                in->head_xy[2] = z; in->head_xy[3] = pr.h * sqrtf(r) / z;
            }
        }
        if (in->ground.ok) {         /* altura do pé (teste do plano do chão, heat_floor) */
            float f = 1e30f;
            for (uint32_t i = 0; i < a->n_body; i++) {
                const float* q = &in->pos[3 * i];
                const float hgt = (q[0] - in->ground.p[0]) * in->ground.n[0] + (q[1] - in->ground.p[1]) * in->ground.n[1] +
                                  (q[2] - in->ground.p[2]) * in->ground.n[2];
                if (hgt < f) f = hgt;
            }
            in->ground.foot = f;
        }
        host_char(in, ch, &pr, mask, s_shift, 0);
        for (int k = 1; k <= copies; k++) host_char(in, ch, &pr, mask, w * k, 1);
        if (heat) memcpy(s_bbox, in->bbox, sizeof s_bbox);
    } else {
        draw_heat_packets(cpu, tm, mask);
        for (int k = 1; k <= copies; k++) { s_shift = w * k; draw_heat_packets(cpu, tm, mask); }
        memcpy(in->bbox, s_bbox, sizeof in->bbox);
    }
    s_shift = 0;
    return s_mode == 1 || s_mode >= 4;   /* 1 e 4+ pulam o original; 2 e 3 desenham os dois (comparar) */
}

/* FUN_80029D4C: a árvore inteira do personagem de tipo 0 (HEAT). */
static int heat_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    psx_mod_counter_add("bag2_heat.draw_tree", 1);
    if (s_dump_request) { dump_tree(cpu); s_dump_request--; }
    const uint32_t root = cpu->gpr[4];
    s_d4c_root = root;               /* se o original seguir, FUN_80029588 não refaz esta árvore */
    s_n_skip = 0;
    if (s_mode == 0 || !s_nch) return 0;
    if (s_force[0] >= 0 || s_force[1] >= 0) force_gesture(root);
    uint32_t nodes[MAX_NODES];
    const int n = collect(root, nodes, 0);
    uint32_t mask = 0;
    TreeMap tm;
    int ch = match_tree(nodes, n, &mask, &tm);
    if (ch < 0) return 0;
    ch = pick_costume(root, ch);
    const int r = draw_char(cpu, root, &tm, ch, mask, 1);
    if (r) s_d4c_root = 0;
    return r;
}

/* FUN_80029588: um osso, chamado pelas funções de desenho dos overlays dos personagens. Na raiz de uma árvore
 * reconhecida desenha o remake e pula o osso; os outros ossos da mesma árvore são pulados até a próxima raiz. */
static int node_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t node = cpu->gpr[6] - 0x2Cu, mesh = cpu->gpr[4];
    int root = 0;
    if (s_nch && ram_ptr(mesh) && node != s_d4c_root) {
        const uint32_t nv = rd32(mesh + 0x94), base = rd32(mesh + 0x98);
        for (int i = 0; i < s_nch && !root; i++) root = s_ch[i].t.node_nverts[0] == nv && s_ch[i].t.node_base[0] == base;
        if (root) {   /* o índice-base do nó 0 é sempre 0: confere também o 2º osso com mesh na ordem da árvore (juntas
                         sem mesh no caminho, como no CHICHI&SALLY, no COLUMBO e no SUSHI BOY, não contam) */
            const uint32_t b1 = second_bone_base(node);
            root = 0;
            for (int i = 0; i < s_nch && !root; i++)
                root = s_ch[i].t.node_nverts[0] == nv && s_ch[i].t.node_base[1] == b1;
        }
    }
    if (!root) {
        for (uint32_t i = 0; i < s_n_skip; i++)
            if (s_skip[i] == node) return 1;
        return 0;
    }
    s_n_skip = 0;
    if (s_mode == 0) return 0;
    psx_mod_counter_add("bag2_heat.node_roots", 1);
    uint32_t nodes[MAX_NODES];
    const int n = collect(node, nodes, 0);
    uint32_t mask = 0;
    TreeMap tm;
    int ch = match_tree(nodes, n, &mask, &tm);
    if (ch >= 0) ch = pick_costume(node, ch);
    if (ch < 0 || !draw_char(cpu, node, &tm, ch, mask, 0)) return 0;
    for (int i = 1; i < n; i++) s_skip[s_n_skip++] = nodes[i];
    return 1;
}

/* Sombra do jogo (FUN_8002A46C): pulada para o personagem com remake quando a sombra nova está ligada; a raiz da
 * sombra é um nó do mesmo vetor de nós do personagem (0xB0 bytes cada). */
static int heat_shadow_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t r = cpu->gpr[4];
    const uint16_t fc = psx_mod_read_half(GAME_FRAME);
    for (int i = 0; i < MAX_INST; i++) {
        Inst* in = &s_inst[i];
        if (in->ch < 0 || !in->floor_valid || r < in->root || r >= in->root + 64u * NODE_SIZE) continue;
        if (!floor_on(in) || !s_ch[in->ch].rt_ready || (uint16_t)(fc - in->floor_fc) > 2u) return 0;
        psx_mod_counter_add("bag2_heat.shadow_skipped", 1);
        /* ordem no quadro: a sombra depois do corpo (pose deste quadro) ou antes (pose do quadro anterior) */
        psx_mod_counter_add(fc == in->floor_fc ? "bag2_heat.floor_same_frame" : "bag2_heat.floor_prev_frame", 1);
        RmkFrame fr = in->floor_fr;
        fr.flags |= RMK_FLAG_FLOOR;
        if (emit_marker(SHADOW_SLOT, (uint16_t)(in->ch << HD_CHAR_SHIFT), s_ch[in->ch].full, &fr, in->pos)) {
            psx_mod_counter_add("bag2_heat.floor_marks", 1);
            in->floor_mark_fc = fc;
        }
        return 1;
    }
    return 0;
}

/* O personagem dos comandos (capturas, testes): o HEAT desenhado por último; senão o último desenhado. */
static Inst* cmd_inst(void) {
    Inst* best = NULL;
    for (int i = 0; i < MAX_INST; i++) {
        Inst* in = &s_inst[i];
        if (in->ch < 0) continue;
        const int heat = s_ch[in->ch].heat;
        if (!best || (heat && !s_ch[best->ch].heat) ||
            (heat == s_ch[best->ch].heat && (int16_t)(in->seen_fc - best->seen_fc) > 0))
            best = in;
    }
    return best;
}

static int heat_floor_cmd(const char* args, char* reply, int cap) {
    float v[3] = {-1, -1, -1};
    if (args) sscanf(args, "%f %f %f", &v[0], &v[1], &v[2]);
    for (int i = 0; i < 3; i++) if (v[i] >= 0) s_floor[i] = v[i];
    static Inst none;
    const Inst* in = cmd_inst();
    if (!in) in = &none;
    snprintf(reply, cap, "opacidade=%.2f maciez=%.2f pulso_luz=%.2f on=%d chao_ok=%d n=%.3f,%.3f,%.3f p=%.0f,%.0f,%.0f "
             "l=%.3f,%.3f,%.3f quadril=%.0f pe=%.0f pulso=%.2f", s_floor[0], s_floor[1], s_floor[2], floor_on(in), in->ground.ok,
             in->ground.n[0], in->ground.n[1], in->ground.n[2], in->ground.p[0], in->ground.p[1], in->ground.p[2], in->ground.l[0],
             in->ground.l[1], in->ground.l[2], in->ground.hip, in->ground.foot, bag2_stage_pulse());
    return 0;
}

static int heat_dump_cmd(const char* args, char* reply, int cap) {
    if (args && *args) snprintf(s_dump_path, sizeof s_dump_path, "%s", args);
    s_dump_request = 2;
    snprintf(reply, cap, "dump armado -> %s", s_dump_path);
    return 0;
}

static int heat_mode_cmd(const char* args, char* reply, int cap) {
    int m = -1, lv = -1, fl = -1, bud = -1, cx = -1; float al = -1, tol = -1;
    if (args) sscanf(args, "%d %d %f %f %d %d %d", &m, &lv, &al, &tol, &fl, &bud, &cx);
    if (bud > 0) s_budget = (uint32_t)bud;
    if (cx >= 0) s_copy_xor = cx;
    if (fl >= 0) s_flags = fl;
    if (tol > 0) s_tol = tol;
    if (m >= 0 && m <= 7) s_mode = m;
    if (lv >= 1 && lv <= 6) s_level = lv;
    if (al >= 0 && al <= 1) s_alpha = al;
    const Chr* h = heat_chr();
    snprintf(reply, cap, "mode=%d level=%d alpha=%.2f tol=%.3f (efetiva %.3f) budget=%u flags=%d loaded=%d host=%d "
             "rt_ready=%d rt_failed=%d arena=%08X z=%.0f..%.0f bbox=%.1f,%.1f,%.1f,%.1f mask=%X maos=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f shift=%.1f",
             s_mode, s_level, s_alpha, s_tol, s_tol_used, s_budget, s_flags, h ? h->state == 2 : 0, use_host_path(),
             h ? h->rt_ready : 0, h ? h->rt_failed : 0, s_arena, s_zr[0], s_zr[1],
             s_bbox[0], s_bbox[1], s_bbox[2], s_bbox[3], s_last_mask, s_hand_xy[0][0], s_hand_xy[0][1], s_hand_xy[0][2],
             s_hand_xy[1][0], s_hand_xy[1][1], s_hand_xy[1][2], s_last_shift);
    return 0;
}

/* LoadImage que bateram com um TIM de asset (os últimos 8): de onde veio cada carga. */
static int heat_hits_cmd(const char* args, char* reply, int cap) {
    (void)args;
    int n = snprintf(reply, cap, "tim_hits=%u", s_nhits);
    for (uint32_t k = 0; k < 8 && k < s_nhits && n > 0 && n < cap; k++) {
        const uint32_t j = (s_nhits - 1u - k) % 8u;
        n += snprintf(reply + n, cap - n, " %d,%d,%dx%d=%s/p%d@%.0fs", s_hits[j].x, s_hits[j].y, s_hits[j].w, s_hits[j].h,
                      s_ch[s_hits[j].ch].t.name, s_hits[j].page, s_hits[j].t);
    }
    return 0;
}

/* Memória: todos os assets que já carregaram, com cargas, descargas e o que pedem agora. */
static int heat_mem_cmd(const char* args, char* reply, int cap) {
    (void)args;
    const double now = now_us();
    int n = snprintf(reply, cap, "descarga=%d gl_apagados=%u", s_evict, s_gl_frees);
    for (int i = 0; i < s_nch && n > 0 && n < cap; i++) {
        const Chr* c = &s_ch[i];
        if (!c->state && !c->loads) continue;
        n += snprintf(reply + n, cap - n, " | %s estado=%d cargas=%u descargas=%u gpu_mb=%.0f ocioso_s=%.0f", c->t.name, c->state,
                      c->loads, c->evictions, c->gl_bytes / 1048576.0, (now - c->last_use_us) / 1e6);
    }
    return 0;
}

static int heat_evict_cmd(const char* args, char* reply, int cap) {
    int v = -1;
    if (args) sscanf(args, "%d", &v);
    if (v == 0 || v == 1) s_evict = v;
    snprintf(reply, cap, "descarga=%d (0 = todos ficam carregados, como na fase 4)", s_evict);
    return 0;
}

static int heat_force_cmd(const char* args, char* reply, int cap) {
    int l = -1, r = -1;
    if (args) sscanf(args, "%d %d", &l, &r);
    s_force[0] = l; s_force[1] = r;
    snprintf(reply, cap, "gesto forçado: esquerda %d direita %d (-1 = o do jogo)", l, r);
    return 0;
}

static int heat_light_cmd(const char* args, char* reply, int cap) {
    float v[7] = {-1, -1, -1, -1, -1, -1, -1};
    if (args) sscanf(args, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    for (int i = 0; i < 7; i++) if (v[i] >= 0) s_light[i] = v[i];
    static Inst none;
    const Inst* in = cmd_inst();
    if (!in) in = &none;
    snprintf(reply, cap, "expo=%.2f key=%.2f fill=%.2f rim=%.2f amb=%.2f rough=%.2f zbias=%.4f | palco: l0=%.2f,%.2f,%.2f "
             "c0=%.2f,%.2f,%.2f l1=%.2f,%.2f,%.2f c1=%.2f,%.2f,%.2f l2=%.2f,%.2f,%.2f c2=%.2f,%.2f,%.2f bk=%.2f,%.2f,%.2f",
             s_light[0], s_light[1], s_light[2], s_light[3], s_light[4], s_light[5], s_light[6],
             in->stage.ldir[0][0], in->stage.ldir[0][1], in->stage.ldir[0][2], in->stage.lcol[0][0], in->stage.lcol[0][1], in->stage.lcol[0][2],
             in->stage.ldir[1][0], in->stage.ldir[1][1], in->stage.ldir[1][2], in->stage.lcol[1][0], in->stage.lcol[1][1], in->stage.lcol[1][2],
             in->stage.ldir[2][0], in->stage.ldir[2][1], in->stage.ldir[2][2], in->stage.lcol[2][0], in->stage.lcol[2][1], in->stage.lcol[2][2],
             in->stage.bk[0], in->stage.bk[1], in->stage.bk[2]);
    return 0;
}

/* Expressão do rosto do HEAT: casas remapeadas agora (página: casa x0,y0-x1,y1 em texels -> deslocamento). A tabela
 * é da thread do jogo; aqui é só leitura para os testes (pode sair de um quadro no meio da troca). */
static int heat_face_cmd(const char* args, char* reply, int cap) {
    (void)args;
    RmkRemap rm[RMK_MAX_REMAP];
    const Inst* in = cmd_inst();
    const uint32_t nr = in ? inst_remaps(in, &s_ch[in->ch].t, rm) : 0;
    int n = snprintf(reply, cap, "cabeca=%.1f,%.1f,%.1f,%.0f shift=%.1f rm=%u", s_head_xy[0], s_head_xy[1], s_head_xy[2],
                     s_head_xy[3], s_last_shift, nr);
    for (uint32_t i = 0; i < nr && n > 0 && n < cap; i++) {
        const RmkRemap* r = &rm[i];
        n += snprintf(reply + n, cap - n, " %u:%u,%u-%u,%u>%d,%d", r->page, r->x0, r->y0, r->x1, r->y1, r->du, r->dv);
    }
    return 0;
}

/* Personagens: assets (estado, carga, GL) e os da partida (raiz, páginas na VRAM, retângulo), casas da VRAM e os
 * últimos LoadImage com a impressão digital. */
static int heat_chars_cmd(const char* args, char* reply, int cap) {
    (void)args;
    /* memória: assets carregados (estado 1..3), GPU pelos tamanhos pedidos, CPU que sobrou (malha; camadas se mantidas) */
    int nl = 0;
    uint64_t gpu = 0, cpu = 0;
    const double now = now_us();
    for (int i = 0; i < s_nch; i++) {
        const Chr* c = &s_ch[i];
        if (c->state <= 0) continue;
        nl++;
        gpu += c->gl_bytes;
        if (c->state >= 2 && c->full) {
            const RmkAsset* a = c->full;
            const uint64_t l = (uint64_t)a->tex_size * a->tex_size * 4u * a->n_layers;
            cpu += (uint64_t)a->n_verts * sizeof(RmkVert) + (uint64_t)a->n_tris * sizeof(RmkTri) + (a->layers ? l : 0u) +
                   (a->nmaps ? l : 0u);
        }
    }
    int n = snprintf(reply, cap, "assets=%d carregados=%d gpu_mb=%.0f cpu_mb=%.0f gl_apagados=%u", s_nch, nl, gpu / 1048576.0,
                     cpu / 1048576.0, s_gl_frees);
    for (int i = 0; i < s_nch && n > 0 && n < cap; i++) {   /* só os carregados agora (o histórico: heat_mem) */
        const Chr* c = &s_ch[i];
        if (!c->state) continue;
        n += snprintf(reply + n, cap - n, " | %s estado=%d carga_ms=%.0f gl=%d/%d preparos=%u gpu_mb=%.0f cargas=%u descargas=%u "
                      "ocioso_s=%.0f vram=%d", c->t.name, c->state, c->load_ms, c->rt_ready, c->rt_failed, c->rt_prepares,
                      c->gl_bytes / 1048576.0, c->loads, c->evictions, (now - c->last_use_us) / 1e6, vram_has(i));
    }
    const uint32_t fc = psx_mod_read_half(GAME_FRAME);
    for (int i = 0; i < MAX_INST && n > 0 && n < cap; i++) {
        const Inst* in = &s_inst[i];
        if (in->ch < 0) continue;
        n += snprintf(reply + n, cap - n, " || jogo: %s raiz=%08X ha=%u quadros grupos=%X paginas=%X", s_ch[in->ch].t.name, in->root,
                      (unsigned)(uint16_t)(fc - in->seen_fc), in->mask, in->pages_ok);
        for (uint32_t p = 0; p < s_ch[in->ch].t.n_pages && n > 0 && n < cap; p++)
            if ((in->pages_ok >> p) & 1u) n += snprintf(reply + n, cap - n, " p%u=%d,%d", p, in->page_vx[p], in->page_vy[p]);
        RmkRemap rm[RMK_MAX_REMAP];
        if (n > 0 && n < cap)
            n += snprintf(reply + n, cap - n, " cabeca=%.1f,%.1f,%.1f desl=%.1f", in->head_xy[0], in->head_xy[1], in->head_xy[3],
                          in->shift);
        if (n > 0 && n < cap)
            n += snprintf(reply + n, cap - n, " bbox=%.1f,%.1f,%.1f,%.1f casas=%u", in->bbox[0], in->bbox[1], in->bbox[2],
                          in->bbox[3], inst_remaps(in, &s_ch[in->ch].t, rm));
    }
    if (n > 0 && n < cap) n += snprintf(reply + n, cap - n, " || casas=%u", s_nvr);
    for (uint32_t i = 0; i < s_nvr && n > 0 && n < cap; i++)
        n += snprintf(reply + n, cap - n, " %d,%d-%d,%d>%d,%d", s_vr[i].x0, s_vr[i].y0, s_vr[i].x1, s_vr[i].y1, s_vr[i].dx, s_vr[i].dy);
    if (n > 0 && n < cap) n += snprintf(reply + n, cap - n, " || vram_tim=%u", s_n_vram_tim);
    for (uint32_t k = 0; k < s_n_vram_tim && n > 0 && n < cap; k++)
        n += snprintf(reply + n, cap - n, " %d,%d,%dx%d=%s", s_vram_tim[k].x, s_vram_tim[k].y, s_vram_tim[k].w, s_vram_tim[k].h,
                      s_ch[s_vram_tim[k].ch].t.name);
    if (n > 0 && n < cap) n += snprintf(reply + n, cap - n, " vram_clut=%u", s_n_vram_clut);
    for (uint32_t k = 0; k < s_n_vram_clut && n > 0 && n < cap; k++)
        n += snprintf(reply + n, cap - n, " %d,%d,%dx%d=%s", s_vram_clut[k].x, s_vram_clut[k].y, s_vram_clut[k].w,
                      s_vram_clut[k].h, s_ch[s_vram_clut[k].ch].t.name);
    if (n > 0 && n < cap) n += snprintf(reply + n, cap - n, " || loads=%u", s_nloads);
    for (uint32_t k = 0; k < 8 && k < s_nloads && n > 0 && n < cap; k++) {
        const uint32_t j = (s_nloads - 1u - k) % 16u;
        n += snprintf(reply + n, cap - n, " %d,%d,%dx%d:%08X%s%s", s_loads[j].x, s_loads[j].y, s_loads[j].w, s_loads[j].h,
                      s_loads[j].hash, s_loads[j].ch >= 0 ? "=" : "", s_loads[j].ch >= 0 ? s_ch[s_loads[j].ch].t.name : "");
    }
    return 0;
}

/* Desde a última chamada: desenhos de host e tempo médio de CPU na thread de render; do renderizador
 * (patch local), desenhos com alvo, espelhos na superfície larga e marcadores sem nada visível. */
extern uint64_t gl_renderer_host_draw_stats(uint64_t* mirrors, uint64_t* skipped);
extern void gl_renderer_host_draw_times(double us[5]);
static int heat_stats_cmd(const char* args, char* reply, int cap) {
    (void)args;
    static uint32_t d0;
    static double u0;
    static uint64_t c0, m0, k0;
    const uint32_t d = s_rt_draws;
    const double u = s_rt_us;
    uint64_t m = 0, k = 0;
    const uint64_t c = gl_renderer_host_draw_stats(&m, &k);
    static uint64_t b_ns, b_n, f_ns, f_n;
    uint64_t bc = 0, fc = 0;
    const double bu = gt_take(&s_gt_body, &b_ns, &b_n, &bc), fu = gt_take(&s_gt_floor, &f_ns, &f_n, &fc);
    /* tempo de parede de cada desenho de host no framework (thread de render), por chamada e somado */
    static double tm0[5];
    double tm[5];
    gl_renderer_host_draw_times(tm);
    const double nc = c > c0 ? (double)(c - c0) : 1.0;
    char hd[200];
    snprintf(hd, sizeof hd, "hd_us lotes=%.1f salvar=%.1f cb0=%.1f rest_esp=%.1f total=%.1f soma_ms=%.0f | ",
             (tm[0] - tm0[0]) / nc, (tm[1] - tm0[1]) / nc, (tm[2] - tm0[2]) / nc, (tm[3] - tm0[3]) / nc,
             (tm[4] - tm0[4]) / nc, (tm[4] - tm0[4]) / 1000.0);
    memcpy(tm0, tm, sizeof tm0);
    const Chr* h = heat_chr();
    int rr = 0, rf = 0;
    uint32_t prep = 0;
    for (int i = 0; i < s_nch; i++) { rr += s_ch[i].rt_ready; rf += s_ch[i].rt_failed; prep += s_ch[i].rt_prepares; }
    snprintf(reply, cap, "%sgpu_corpo_us=%.0f (%llu) gpu_chao_us=%.0f (%llu) rt_draws=%u rt_us_per_draw=%.1f gl_calls=%llu gl_mirrors=%llu gl_skipped=%llu rt_prepares=%u "
             "rt_ready=%d rt_failed=%d err=%s", hd, bu, (unsigned long long)bc, fu, (unsigned long long)fc, d - d0, d > d0 ? (u - u0) / (double)(d - d0) : 0.0,
             (unsigned long long)(c - c0), (unsigned long long)(m - m0), (unsigned long long)(k - k0), prep,
             rr, rf, h && h->gl ? rmk_gl_error(h->gl) : "");
    d0 = d; u0 = u; c0 = c; m0 = m; k0 = k;
    return 0;
}

PSX_MOD_CONSTRUCTOR(bag2_heat_register) {
    (void)psx_mod_register_activation_plugin("bag2.heat_remake", heat_activate);
    (void)psx_mod_register_function_filter_plugin("bag2.heat_remake", DRAW_TREE_FN, heat_filter);
    (void)psx_mod_register_function_filter_plugin("bag2.heat_remake", NODE_FILL_FN, node_filter);
    (void)psx_mod_register_function_filter_plugin("bag2.heat_remake", SHADOW_FN, heat_shadow_filter);
    (void)psx_mod_register_function_entry_plugin("bag2.heat_remake", MOVE_IMAGE_FN, heat_move_image);
    (void)psx_mod_register_function_entry_plugin("bag2.heat_remake", LOAD_IMAGE_FN, heat_load_image);
    (void)psx_mod_register_savestate_plugin("bag2.heat_remake", heat_savestate_loaded);
    (void)psx_mod_register_vblank_plugin("bag2.heat_remake", heat_vblank);
    (void)psx_mod_register_host_draw(HOST_DRAW_ID, char_host_draw);
    (void)psx_mod_register_command("heat_dump", heat_dump_cmd);
    (void)psx_mod_register_command("heat_mode", heat_mode_cmd);
    (void)psx_mod_register_command("heat_light", heat_light_cmd);
    (void)psx_mod_register_command("heat_force", heat_force_cmd);
    (void)psx_mod_register_command("heat_stats", heat_stats_cmd);
    (void)psx_mod_register_command("heat_hitch", heat_hitch_cmd);
    (void)psx_mod_register_command("heat_face", heat_face_cmd);
    (void)psx_mod_register_command("heat_floor", heat_floor_cmd);
    (void)psx_mod_register_command("heat_chars", heat_chars_cmd);
    (void)psx_mod_register_command("heat_evict", heat_evict_cmd);
    (void)psx_mod_register_command("heat_hits", heat_hits_cmd);
    (void)psx_mod_register_command("heat_mem", heat_mem_cmd);
}
