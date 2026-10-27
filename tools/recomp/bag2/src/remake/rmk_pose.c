/* Pose do remake: matrizes dos ossos do GTE -> vértices no espaço da câmera, resumo do quadro, normais.
 * Visibilidade e slot da OT seguem o jogo (FUN_80036230): NCLIP > 0 e (z0 + z1) >> 3. */
#include "rmk.h"

#include <math.h>
#include <string.h>

void rmk_stage_light(const int16_t L[9], const int16_t M[9], const int16_t lcm[9], const int32_t bk[3],
                     float ldir[3][3], float lcol[3][3], float bk_out[3]) {
    for (int i = 0; i < 3; i++) {
        float d[3];
        for (int r = 0; r < 3; r++)
            d[r] = (float)M[3 * r] * L[3 * i] + (float)M[3 * r + 1] * L[3 * i + 1] + (float)M[3 * r + 2] * L[3 * i + 2];
        const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int r = 0; r < 3; r++) ldir[i][r] = l > 1e-6f ? d[r] / l : (r == 2 ? -1.0f : 0.0f);
        for (int c = 0; c < 3; c++) lcol[i][c] = (float)lcm[3 * c + i] / 4096.0f;
        bk_out[i] = (float)bk[i] / 4096.0f;
    }
}

void rmk_bone_from_gte(RmkBone* b, const int16_t m[9], const int32_t t[3]) {
    for (int k = 0; k < 9; k++) b->m[k] = (float)m[k] / 4096.0f;
    for (int k = 0; k < 3; k++) b->t[k] = (float)t[k];
}

uint32_t rmk_active_verts(const RmkAsset* a, uint32_t mask) {
    uint32_t n = a->n_body;
    for (uint32_t g = 1; g <= a->n_groups; g++)
        if ((mask >> g) & 1u) n += a->groups[g].nv;
    return n;
}

void rmk_compact(const RmkAsset* a, uint32_t mask, const float* full, float* compact) {
    memcpy(compact, full, 12u * a->n_body);
    uint32_t o = a->n_body;
    for (uint32_t g = 1; g <= a->n_groups; g++) {
        if (!((mask >> g) & 1u)) continue;
        memcpy(compact + 3u * o, full + 3u * a->groups[g].v0, 12u * a->groups[g].nv);
        o += a->groups[g].nv;
    }
}

void rmk_expand(const RmkAsset* a, uint32_t mask, const float* compact, float* full) {
    memcpy(full, compact, 12u * a->n_body);
    uint32_t o = a->n_body;
    for (uint32_t g = 1; g <= a->n_groups; g++) {
        if (!((mask >> g) & 1u)) continue;
        memcpy(full + 3u * a->groups[g].v0, compact + 3u * o, 12u * a->groups[g].nv);
        o += a->groups[g].nv;
    }
}

static void skin_range(const RmkAsset* a, const RmkBone* bones, int n_bones, uint32_t i0, uint32_t i1, float* pos) {
    for (uint32_t i = i0; i < i1; i++) {
        const RmkVert* v = &a->verts[i];
        const RmkBone* b = &bones[v->owner < n_bones ? v->owner : 0];
        const float x = v->x, y = v->y, z = v->z;
        float* p = &pos[3 * i];
        p[0] = b->m[0] * x + b->m[1] * y + b->m[2] * z + b->t[0];
        p[1] = b->m[3] * x + b->m[4] * y + b->m[5] * z + b->t[1];
        p[2] = b->m[6] * x + b->m[7] * y + b->m[8] * z + b->t[2];
    }
}

void rmk_skin(const RmkAsset* a, const RmkBone* bones, int n_bones, uint32_t mask, float* pos) {
    skin_range(a, bones, n_bones, 0, a->n_body, pos);
    for (uint32_t g = 1; g <= a->n_groups; g++)
        if ((mask >> g) & 1u) skin_range(a, bones, n_bones, a->groups[g].v0, a->groups[g].v0 + a->groups[g].nv, pos);
}

static void face_normal(const float* a, const float* b, const float* c, float n[3]) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    n[0] = u[1] * w[2] - u[2] * w[1];
    n[1] = u[2] * w[0] - u[0] * w[2];
    n[2] = u[0] * w[1] - u[1] * w[0];
}

/* NCLIP > 0 na tela equivale a det[a b c] > 0 com z > 0, ou seja, normal de face . a > 0. */
static int face_visible(const float* a, const float* b, const float* c, const float n[3]) {
    if (a[2] < 64.0f || b[2] < 64.0f || c[2] < 64.0f) return 0;
    return n[0] * a[0] + n[1] * a[1] + n[2] * a[2] > 0.0f;
}

void rmk_frame_info(const RmkAsset* a, const float* pos, uint32_t mask, const RmkProj* pr, float shift_x,
                    RmkFrameInfo* out) {
    float bb[4] = {1e9f, 1e9f, -1e9f, -1e9f}, zmin = 1e9f, zmax = -1e9f;
    int ot = 1 << 30, vis = 0;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        if (!RMK_FACE_ON(t, mask)) continue;
        const float *p0 = &pos[3 * t->v[0]], *p1 = &pos[3 * t->v[1]], *p2 = &pos[3 * t->v[2]];
        float n[3];
        face_normal(p0, p1, p2, n);
        const float* ps[3] = {p0, p1, p2};
        for (int k = 0; k < 3; k++) {
            const float z = ps[k][2] > 32.0f ? ps[k][2] : 32.0f;
            const float sx = pr->ofx + pr->h * ps[k][0] / z + shift_x, sy = pr->ofy + pr->h * ps[k][1] / z;
            if (sx < bb[0]) bb[0] = sx;
            if (sy < bb[1]) bb[1] = sy;
            if (sx > bb[2]) bb[2] = sx;
            if (sy > bb[3]) bb[3] = sy;
            if (ps[k][2] < zmin) zmin = ps[k][2];
            if (ps[k][2] > zmax) zmax = ps[k][2];
        }
        if (!face_visible(p0, p1, p2, n)) continue;
        vis++;
        const int oi = ((int)pos[3 * t->zp[0] + 2] + (int)pos[3 * t->zp[1] + 2]) >> 3;
        if (oi < ot) ot = oi;
    }
    /* margem: a superfície de Phong sai um pouco da casca da malha original */
    const float mx = (bb[2] - bb[0]) * 0.04f + 2.0f, my = (bb[3] - bb[1]) * 0.04f + 2.0f;
    out->bbox[0] = bb[0] - mx; out->bbox[1] = bb[1] - my; out->bbox[2] = bb[2] + mx; out->bbox[3] = bb[3] + my;
    out->zmin = zmin; out->zmax = zmax;
    out->ot_near = vis ? ot : 0;
    out->visible = vis;
}

void rmk_pack_geometry(const RmkAsset* a, const float* pos, uint32_t mask, float* out) {
    memset(out, 0, sizeof(float) * 8u * a->n_verts);
    float orient = 0.0f;
    for (uint32_t f = 0; f < a->n_tris; f++) {
        const RmkTri* t = &a->tris[f];
        if (!RMK_FACE_ON(t, mask)) continue;
        const float *p0 = &pos[3 * t->v[0]], *p1 = &pos[3 * t->v[1]], *p2 = &pos[3 * t->v[2]];
        float n[3];
        face_normal(p0, p1, p2, n);
        for (int k = 0; k < 3; k++) {
            float* o = &out[8u * t->v[k] + 4u];
            o[0] += n[0]; o[1] += n[1]; o[2] += n[2];
        }
        if (face_visible(p0, p1, p2, n))
            orient -= n[0] * (p0[0] + p1[0] + p2[0]) + n[1] * (p0[1] + p1[1] + p2[1]) + n[2] * (p0[2] + p1[2] + p2[2]);
    }
    const float sgn = orient < 0.0f ? -1.0f : 1.0f;   /* normais da face visível apontam para a câmera */
    for (uint32_t g = 0; g <= a->n_groups; g++) {
        if (g && !((mask >> g) & 1u)) continue;
        const uint32_t i0 = g ? a->groups[g].v0 : 0, i1 = g ? i0 + a->groups[g].nv : a->n_body;
        for (uint32_t i = i0; i < i1; i++) {
            float* o = &out[8u * i];
            o[0] = pos[3 * i]; o[1] = pos[3 * i + 1]; o[2] = pos[3 * i + 2];
            o[3] = a->verts[i].ao / 255.0f;
            const float l = sqrtf(o[4] * o[4] + o[5] * o[5] + o[6] * o[6]);
            if (l > 1e-12f) { o[4] *= sgn / l; o[5] *= sgn / l; o[6] *= sgn / l; }
            else { o[4] = 0.0f; o[5] = 0.0f; o[6] = -1.0f; }
        }
    }
}
