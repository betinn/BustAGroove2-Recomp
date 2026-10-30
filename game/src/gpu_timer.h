/* Tempo de GPU de um trecho de comandos GL (adaptadores do PC, build-debug e normal; fora do núcleo portátil,
 * porque o GLES 3.0 não tem consultas de tempo). Usa dois carimbos GL_TIMESTAMP (glQueryCounter, GL 3.3) em vez
 * de GL_TIME_ELAPSED: o framework já tem uma consulta TIME_ELAPSED aberta em volta da cena (frame_perf) e duas
 * do mesmo alvo não podem estar ativas juntas. Resultados lidos GT_RING chamadas depois, sem parar o pipeline.
 * Só a thread de render chama gt_begin/gt_end; gt_take lê as somas de qualquer thread (aproximado).
 * Desligado por padrão (comando gpu_timers 1): com o driver da NVIDIA em modo de thread toda leitura de volta
 * (glGetQueryObject*) pode esperar a fila do driver, o mesmo custo que o framework tinha com glGetIntegerv. */
#ifndef BAG2_GPU_TIMER_H
#define BAG2_GPU_TIMER_H

#include <stdint.h>

#if defined(_WIN32) && !defined(_WIN64)
#define GT_API __stdcall
#else
#define GT_API
#endif

#define GT_RING 8
extern volatile int bag2_gpu_timers;   /* bag2_stage.c, comando gpu_timers */
typedef struct {
    void (GT_API* GenQueries)(int, unsigned*);
    void (GT_API* QueryCounter)(unsigned, unsigned);
    void (GT_API* GetQueryObjectiv)(unsigned, unsigned, int*);
    void (GT_API* GetQueryObjectui64v)(unsigned, unsigned, uint64_t*);
    unsigned q[GT_RING][2];
    int pending[GT_RING], slot, state, open;   /* state: 0 ainda não, 1 pronto, -1 sem consultas */
    volatile uint64_t ns, n;
} GpuTimer;

static void gt_init(GpuTimer* t, void* (*get_proc)(const char*)) {
    if (t->state) return;
    t->GenQueries = (void (GT_API*)(int, unsigned*))get_proc("glGenQueries");
    t->QueryCounter = (void (GT_API*)(unsigned, unsigned))get_proc("glQueryCounter");
    t->GetQueryObjectiv = (void (GT_API*)(unsigned, unsigned, int*))get_proc("glGetQueryObjectiv");
    t->GetQueryObjectui64v = (void (GT_API*)(unsigned, unsigned, uint64_t*))get_proc("glGetQueryObjectui64v");
    if (!t->GenQueries || !t->QueryCounter || !t->GetQueryObjectiv || !t->GetQueryObjectui64v) { t->state = -1; return; }
    t->GenQueries(2 * GT_RING, &t->q[0][0]);
    t->state = 1;
}

static void gt_begin(GpuTimer* t) {
    t->open = 0;
    if (t->state != 1 || !bag2_gpu_timers) return;
    const int s = t->slot;
    if (t->pending[s]) {                 /* o carimbo de GT_RING chamadas atrás: lê só se já chegou */
        int ok = 0;
        t->GetQueryObjectiv(t->q[s][1], 0x8867 /* GL_QUERY_RESULT_AVAILABLE */, &ok);
        if (ok) {
            uint64_t a = 0, b = 0;
            t->GetQueryObjectui64v(t->q[s][0], 0x8866 /* GL_QUERY_RESULT */, &a);
            t->GetQueryObjectui64v(t->q[s][1], 0x8866, &b);
            if (b > a) { t->ns += b - a; t->n++; }
        }
        t->pending[s] = 0;
    }
    t->QueryCounter(t->q[s][0], 0x8E28 /* GL_TIMESTAMP */);
    t->open = 1;
}

static void gt_end(GpuTimer* t) {
    if (!t->open) return;
    t->open = 0;
    t->QueryCounter(t->q[t->slot][1], 0x8E28);
    t->pending[t->slot] = 1;
    t->slot = (t->slot + 1) % GT_RING;
}

/* Microssegundos de GPU por chamada desde a última leitura (e quantas chamadas entraram). */
static double gt_take(GpuTimer* t, uint64_t* ns0, uint64_t* n0, uint64_t* count) {
    const uint64_t ns = t->ns, n = t->n;
    const double us = n > *n0 ? (double)(ns - *ns0) / 1000.0 / (double)(n - *n0) : 0.0;
    if (count) *count = n - *n0;
    *ns0 = ns; *n0 = n;
    return us;
}

#endif
