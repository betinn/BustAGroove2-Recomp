"""Renderização offscreen (moderngl, OpenGL 3.3) em dois estilos.

- `ps1`: imitação do PS1 — luz por vértice (Gouraud, como o GTE), textura sem filtro e sem correção de
  perspectiva (afim), cor em 15 bits com dithering 4x4, resolução baixa.
- `modern`: luz por pixel — três luzes (principal quente, preenchimento frio, contraluz), GGX, luz
  ambiente de hemisfério, sombra por shadow map com PCF, textura filtrada com mipmaps, tonemap ACES,
  MSAA 8x + supersampling.
- `toon`: mesmo pipeline do `modern`, com sombreamento em faixas e contorno (estilo anime).
"""
from __future__ import annotations

import numpy as np
import moderngl
from PIL import Image

VS_COMMON = """
#version 330
uniform mat4 u_vp;
uniform mat4 u_light_vp;
in vec3 in_pos; in vec3 in_nrm; in vec3 in_base; in vec3 in_over; in vec2 in_uv; in float in_layer; in float in_mode;
in float in_ao;
"""

PS1_VS = VS_COMMON + """
uniform vec3 u_ldir; uniform float u_amb;
uniform int u_stage; uniform vec3 u_sdir[3]; uniform vec3 u_scol[3]; uniform vec3 u_sbk;
out vec3 v_base; out vec3 v_over;
noperspective out vec2 v_uv;
flat out float v_layer; flat out float v_mode;
void main() {
  gl_Position = u_vp * vec4(in_pos, 1.0);
  vec3 l = vec3(u_amb + max(dot(normalize(in_nrm), -u_ldir), 0.0) * (1.0 - u_amb) * 1.1);
  if (u_stage != 0) {   // a conta do GTE: BK + LCM * max(L * n, 0)
    l = u_sbk;
    for (int i = 0; i < 3; i++) l += u_scol[i] * max(dot(normalize(in_nrm), u_sdir[i]), 0.0);
    l = max(l, 0.0);
  }
  v_base = in_base / 255.0 * l;
  v_over = in_over / 128.0 * l;
  v_uv = in_uv; v_layer = in_layer; v_mode = in_mode;
}
"""
PS1_FS = """
#version 330
uniform sampler2DArray u_tex;
in vec3 v_base; in vec3 v_over;
noperspective in vec2 v_uv;
flat in float v_layer; flat in float v_mode;
out vec4 f_color;
const float bayer[16] = float[](-4.,0.,-3.,1., 2.,-2.,3.,-1., -3.,1.,-4.,0., 3.,-1.,2.,-2.);
void main() {
  vec3 c = v_base;
  float md = mod(v_mode, 4.0);   // bit 4 do modo = sem mapa de normal (só o remake usa)
  if (md > 0.5) {
    vec4 t = texelFetch(u_tex, ivec3(ivec2(floor(v_uv)) & 255, int(v_layer)), 0);
    if (t.a < 0.5) { if (md > 1.5) discard; }
    else c = t.rgb * v_over;
  }
  ivec2 p = ivec2(gl_FragCoord.xy) & 3;
  c = clamp(c * 255.0 + bayer[p.y * 4 + p.x], 0.0, 255.0);
  c = floor(c / 8.0) * 8.0 / 255.0;          // 15 bits
  f_color = vec4(c, 1.0);
}
"""

MODERN_VS = VS_COMMON + """
out vec3 v_pos; out vec3 v_nrm; out vec3 v_base; out vec3 v_over; out vec2 v_uv; out vec4 v_lpos; out float v_ao;
flat out float v_layer; flat out float v_mode;
void main() {
  gl_Position = u_vp * vec4(in_pos, 1.0);
  v_pos = in_pos; v_nrm = in_nrm; v_base = in_base / 255.0; v_over = in_over / 128.0; v_ao = in_ao;
  v_uv = in_uv; v_layer = in_layer; v_mode = in_mode;
  v_lpos = u_light_vp * vec4(in_pos, 1.0);
}
"""
MODERN_FS = """
#version 330
uniform sampler2DArray u_tex;
uniform sampler2DArray u_nmap;
uniform float u_nmap_on;
uniform sampler2DShadow u_shadow;
uniform vec3 u_eye;
uniform vec3 u_key_dir; uniform vec3 u_key_col;
uniform vec3 u_fill_dir; uniform vec3 u_fill_col;
uniform vec3 u_rim_dir; uniform vec3 u_rim_col;
uniform vec3 u_sky; uniform vec3 u_ground;
uniform float u_rough; uniform float u_toon; uniform float u_shadow_texel;
uniform int u_stage; uniform vec3 u_sdir[3]; uniform vec3 u_scol[3]; uniform vec3 u_sbk;
// expressão do rosto (tools/remake/faces.py): casa (x0, y0, x1, y1 em texels), deslocamento até a variante e
// máscara das camadas da página; como src/remake/rmk_gl.c
uniform int u_nrm; uniform vec4 u_rm_rect[8]; uniform vec2 u_rm_off[8]; uniform int u_rm_mask[8];
in vec3 v_pos; in vec3 v_nrm; in vec3 v_base; in vec3 v_over; in vec2 v_uv; in vec4 v_lpos; in float v_ao;
flat in float v_layer; flat in float v_mode;
out vec4 f_color;

vec3 srgb2lin(vec3 c) { return pow(c, vec3(2.2)); }
vec2 remap(vec2 t) {   // t em texels da página; dentro da casa lê a variante (sem sangrar a borda)
  for (int i = 0; i < 8; i++) {
    if (i >= u_nrm) break;
    if (((u_rm_mask[i] >> int(v_layer + 0.5)) & 1) == 0) continue;
    vec4 r = u_rm_rect[i];
    if (all(greaterThanEqual(t, r.xy)) && all(lessThan(t, r.zw))) return clamp(t, r.xy + 0.125, r.zw - 0.125) + u_rm_off[i];
  }
  return t;
}
// base tangente pelas derivadas da tela (Schüler): sem tangente por vértice, vale com UV espelhado
vec3 perturb(vec3 n, vec3 p, vec2 uv, vec3 t) {
  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  vec3 dp2p = cross(dp2, n), dp1p = cross(n, dp1);
  vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;
  float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
  return normalize(T * im * t.x + B * im * t.y + n * t.z);
}
float D_ggx(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (3.14159 * d * d); }
float shadow() {
  vec3 p = v_lpos.xyz / v_lpos.w * 0.5 + 0.5;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
  float s = 0.0;
  for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++)
    s += texture(u_shadow, vec3(p.xy + vec2(x, y) * u_shadow_texel, p.z - 0.0015));
  return s / 25.0;
}
vec3 light(vec3 n, vec3 v, vec3 l, vec3 col, vec3 alb, float sh, float wrap) {
  float nl = dot(n, l);
  float d = max((nl + wrap) / (1.0 + wrap), 0.0);
  vec3 h = normalize(l + v);
  float nh = max(dot(n, h), 0.0), nv = max(dot(n, v), 1e-3);
  float F = 0.04 + 0.96 * pow(1.0 - max(dot(h, v), 0.0), 5.0);
  float a = u_rough * u_rough;
  float spec = D_ggx(nh, a) * F * 0.25 / max(nv, 0.2);
  return col * sh * (alb * d + spec * max(nl, 0.0));
}
vec3 toon(vec3 n, vec3 v, vec3 alb, float sh) {
  // duas faixas (luz/sombra), sombra puxada para o violeta, brilho e contraluz recortados
  float nl = dot(n, -u_key_dir);
  float lit = smoothstep(0.0, 0.06, nl) * smoothstep(0.35, 0.65, sh);
  float hi = smoothstep(0.93, 0.95, max(dot(n, normalize(-u_key_dir + v)), 0.0));
  vec3 shade = alb * vec3(0.42, 0.40, 0.55);
  vec3 c = mix(shade, alb * 1.05, lit) * mix(0.75, 1.0, v_ao);
  float rim = smoothstep(0.62, 0.66, 1.0 - max(dot(n, v), 0.0)) * max(dot(n, -u_rim_dir) * 0.5 + 0.5, 0.0);
  return c + hi * 0.25 * lit + rim * 0.22;
}
void main() {
  vec3 base = v_base;
  float isTex = 0.0;
  float md = mod(v_mode, 4.0);   // bit 4 do modo = sem mapa de normal
  vec2 uvn = v_uv / 256.0, gx = dFdx(uvn), gy = dFdy(uvn), uvr = remap(v_uv) / 256.0;
  if (md > 0.5) {
    vec4 t = textureGrad(u_tex, vec3(uvr, v_layer), gx, gy);
    if (t.a < 0.5) { if (md > 1.5) discard; }
    else { base = t.rgb * v_over; isTex = 1.0; }
  }
  vec3 alb = srgb2lin(clamp(base, 0.0, 1.0));
  vec3 n = normalize(v_nrm);
  vec3 v = normalize(u_eye - v_pos);
  if (dot(n, v) < 0.0) n = -n;
  if (isTex > 0.5 && u_nmap_on > 0.5 && v_mode < 3.5) {
    vec3 t = textureGrad(u_nmap, vec3(uvr, v_layer), gx, gy).xyz * 2.0 - 1.0;
    n = perturb(n, v_pos, uvn, t);
  }
  float sh = shadow();
  if (u_toon > 0.5) { f_color = vec4(pow(toon(n, v, alb, sh), vec3(1.0 / 2.2)), 1.0); return; }
  vec3 c = vec3(0.0);
  if (u_stage != 0) {   // luzes do palco: a soma do GTE por pixel, em gama, como src/remake/rmk_gl.c
    vec3 I = u_sbk * mix(0.6, 1.0, v_ao), sp = vec3(0.0);
    for (int i = 0; i < 3; i++) {
      float nl = dot(n, u_sdir[i]);
      vec3 pos = max(u_scol[i], 0.0), ng = min(u_scol[i], 0.0);
      float s = (i == 0) ? sh : 1.0;
      I += pos * max((nl + 0.2) / 1.2, 0.0) * s + ng * max(nl, 0.0);
      sp += light(n, v, u_sdir[i], pos, vec3(0.0), s, 0.0);
    }
    c = alb * pow(max(I, 0.0), vec3(2.2)) + sp * 1.6;
  } else {
    c += light(n, v, -u_key_dir, u_key_col, alb, sh, 0.25);
    c += light(n, v, -u_fill_dir, u_fill_col, alb, 1.0, 0.5);
    vec3 amb = mix(u_ground, u_sky, n.y * 0.5 + 0.5);
    c += amb * alb * v_ao;
  }
  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * max(dot(n, -u_rim_dir), 0.0);
  c += u_rim_col * rim * (0.3 + alb) * v_ao;
  c *= mix(0.55, 1.0, v_ao);          // oclusão também nas luzes diretas (contato), mais leve
  // exposição + ACES (Narkowicz) + gamma
  c *= (u_stage != 0) ? 1.0 : 0.7;
  c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
  f_color = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
"""

DEPTH_VS = """
#version 330
uniform mat4 u_light_vp;
in vec3 in_pos;
void main() { gl_Position = u_light_vp * vec4(in_pos, 1.0); }
"""
DEPTH_FS = "#version 330\nvoid main() {}\n"

OUTLINE_VS = """
#version 330
uniform mat4 u_vp; uniform float u_width;
in vec3 in_pos; in vec3 in_nrm;
void main() { gl_Position = u_vp * vec4(in_pos + in_nrm * u_width, 1.0); }
"""
OUTLINE_FS = "#version 330\nout vec4 f_color;\nvoid main() { f_color = vec4(0.06, 0.04, 0.05, 1.0); }\n"

FLOOR_VS = """
#version 330
uniform mat4 u_vp; uniform mat4 u_light_vp; uniform float u_r; uniform vec3 u_c;
in vec2 in_xy;
out vec3 v_pos; out vec4 v_lpos;
void main() { vec3 p = u_c + vec3(in_xy.x * u_r, 0.0, in_xy.y * u_r); v_pos = p - u_c;
  v_lpos = u_light_vp * vec4(p, 1.0); gl_Position = u_vp * vec4(p, 1.0); }
"""
FLOOR_FS = """
#version 330
uniform sampler2DShadow u_shadow; uniform float u_r; uniform float u_shadow_texel; uniform vec3 u_col; uniform vec3 u_bg;
in vec3 v_pos; in vec4 v_lpos;
out vec4 f_color;
void main() {
  vec3 p = v_lpos.xyz / v_lpos.w * 0.5 + 0.5;
  float s = 0.0;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) s = 49.0; else
  for (int y = -3; y <= 3; y++) for (int x = -3; x <= 3; x++)
    s += texture(u_shadow, vec3(p.xy + vec2(x, y) * u_shadow_texel * 1.5, p.z - 0.002));
  s /= 49.0;
  float r = length(v_pos.xz) / u_r;
  vec3 c = u_col * (0.45 + 0.55 * s);
  f_color = vec4(c, 1.0 - smoothstep(0.3, 1.0, r));
}
"""

BG_VS = "#version 330\nin vec2 in_xy; out vec2 v; void main() { v = in_xy; gl_Position = vec4(in_xy, 0.999, 1.0); }\n"
BG_FS = """
#version 330
uniform vec3 u_top; uniform vec3 u_bot; in vec2 v; out vec4 f_color;
void main() { f_color = vec4(mix(u_bot, u_top, v.y * 0.5 + 0.5), 1.0); }
"""


def look_at(eye, target, up=(0, 1, 0)):
    eye, target, up = map(lambda a: np.asarray(a, float), (eye, target, up))
    f = target - eye; f /= np.linalg.norm(f)
    s = np.cross(f, up); s /= np.linalg.norm(s)
    u = np.cross(s, f)
    M = np.eye(4)
    M[0, :3], M[1, :3], M[2, :3] = s, u, -f
    M[:3, 3] = -M[:3, :3] @ eye
    return M


def perspective(fovy, aspect, near, far):
    f = 1 / np.tan(np.radians(fovy) / 2)
    M = np.zeros((4, 4))
    M[0, 0], M[1, 1] = f / aspect, f
    M[2, 2], M[2, 3] = (far + near) / (near - far), 2 * far * near / (near - far)
    M[3, 2] = -1
    return M


def ortho(l, r, b, t, n, f):
    M = np.eye(4)
    M[0, 0], M[1, 1], M[2, 2] = 2 / (r - l), 2 / (t - b), -2 / (f - n)
    M[:3, 3] = [-(r + l) / (r - l), -(t + b) / (t - b), -(f + n) / (f - n)]
    return M


def _m(M):
    return np.asarray(M, 'f4').T.tobytes()


# Luzes de um palco medidas no jogo (heat_light, 2026-10-27; partida com o HEAT, Stage 1), no espaço da câmera
# do GTE (x direita, y para baixo, z para dentro): luz 1 quase branca da frente-direita-cima, luz 2 = -luz 1 com
# cor negativa (escurece o lado de trás), luz 3 laranja fraca de trás-esquerda; BK = ambiente.
STAGE_LIGHT = dict(ldir=np.array([[0.65, -0.31, -0.69], [-0.65, 0.31, 0.69], [-0.72, -0.10, 0.68]]),
                   lcol=np.array([[0.75, 0.71, 0.75], [-0.56, -0.59, -0.52], [0.375, 0.31, 0.06]]),
                   bk=np.array([0.555, 0.531, 0.586]))


def stage_world(stage, view):
    """Direções da câmera do GTE -> mundo do render (câmera GL: y para cima, z para fora da tela)."""
    d = np.asarray(stage['ldir'], float) * np.array([1, -1, -1])
    w = d @ view[:3, :3]          # inversa da rotação da vista (ortogonal) aplicada a cada linha
    return w / np.linalg.norm(w, axis=1, keepdims=True)


ATTRS = (('in_pos', 3), ('in_nrm', 3), ('in_base', 3), ('in_over', 3), ('in_uv', 2), ('in_layer', 1),
         ('in_mode', 1), ('in_ao', 1))
NFLOATS = sum(k for _, k in ATTRS)


def layout(prog):
    """Formato do buffer intercalado para um programa (atributos que ele não usa viram padding)."""
    fmt, names = [], []
    for n, k in ATTRS:
        if n in prog:
            fmt.append(f'{k}f'); names.append(n)
        else:
            fmt.append(f'{4 * k}x')
    return ' '.join(fmt), names


def pack(pos, nrm, base, over, uv, layer, mode, ao=None):
    """Atributos por canto (F,3,·) -> buffer intercalado float32."""
    F = len(pos)
    lay = np.repeat(layer[:, None], 3, 1)[..., None].astype('f4')
    md = np.repeat(mode[:, None], 3, 1)[..., None].astype('f4')
    ao = np.ones((F, 3, 1), 'f4') if ao is None else ao[..., None]
    data = np.concatenate([pos, nrm, base, over, uv, lay, md, ao], -1).astype('f4')
    return data.reshape(F * 3, -1)


class Renderer:
    def __init__(self, ps1_layers: list[np.ndarray], hd_layers: list[np.ndarray] | None = None, nmaps=None):
        self.ctx = moderngl.create_standalone_context()
        self.ctx.enable(moderngl.DEPTH_TEST)
        c = self.ctx
        self.p_ps1 = c.program(vertex_shader=PS1_VS, fragment_shader=PS1_FS)
        self.p_mod = c.program(vertex_shader=MODERN_VS, fragment_shader=MODERN_FS)
        self.p_depth = c.program(vertex_shader=DEPTH_VS, fragment_shader=DEPTH_FS)
        self.p_out = c.program(vertex_shader=OUTLINE_VS, fragment_shader=OUTLINE_FS)
        self.p_floor = c.program(vertex_shader=FLOOR_VS, fragment_shader=FLOOR_FS)
        self.p_bg = c.program(vertex_shader=BG_VS, fragment_shader=BG_FS)
        self.tex_ps1 = self._array(ps1_layers, linear=False)
        self.tex_hd = self._array(hd_layers or ps1_layers, linear=True)
        # mapas de normal das camadas HD (tools/remake/detail.py, a mesma conta do jogo)
        if nmaps is None and hd_layers is not None:
            import detail
            nmaps = [detail.normal_map(l) for l in hd_layers]
        self.tex_nmap = self._array(nmaps, linear=True) if nmaps is not None else None
        self.nmap_on = 1.0
        quad = np.array([-1, -1, 1, -1, -1, 1, 1, 1], 'f4')
        self.quad = c.buffer(quad.tobytes())
        self.vao_bg = c.vertex_array(self.p_bg, [(self.quad, '2f', 'in_xy')])
        self.vao_floor = c.vertex_array(self.p_floor, [(self.quad, '2f', 'in_xy')])
        self.shadow_size = 4096
        self.shadow_tex = c.depth_texture((self.shadow_size,) * 2)
        self.shadow_tex.compare_func = '<='
        self.shadow_tex.repeat_x = self.shadow_tex.repeat_y = False
        self.shadow_fbo = c.framebuffer(depth_attachment=self.shadow_tex)

    def _array(self, layers, linear):
        h = max(l.shape[0] for l in layers); w = max(l.shape[1] for l in layers)
        buf = np.zeros((len(layers), h, w, 4), np.uint8)
        for i, l in enumerate(layers):
            buf[i, :l.shape[0], :l.shape[1]] = l
        t = self.ctx.texture_array((w, h, len(layers)), 4, buf.tobytes())
        if linear:
            t.build_mipmaps()
            t.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
            t.anisotropy = 16.0
        else:
            t.filter = (moderngl.NEAREST, moderngl.NEAREST)
        return t

    @staticmethod
    def camera(center, dist, yaw_deg, pitch_deg, size, fovy=26.0):
        """Câmera orbital: yaw 0 olha o personagem de frente (+z), pitch > 0 de cima."""
        yaw, pitch = np.radians(yaw_deg), np.radians(pitch_deg)
        d = np.array([np.sin(yaw) * np.cos(pitch), np.sin(pitch), np.cos(yaw) * np.cos(pitch)])
        eye = np.asarray(center) + d * dist
        V = look_at(eye, center)
        P = perspective(fovy, size[0] / size[1], dist * 0.05, dist * 4)
        return P @ V, eye

    @staticmethod
    def fit(points, yaw_deg, pitch_deg, size, margin=0.9, fovy=26.0):
        """Centro e distância de câmera para que `points` caibam na tela (com perspectiva)."""
        lo, hi = points.min(0), points.max(0)
        center = (lo + hi) / 2
        dist = np.linalg.norm(hi - lo) * 2
        for _ in range(4):
            vp, eye = Renderer.camera(center, dist, yaw_deg, pitch_deg, size, fovy)
            h = np.c_[points, np.ones(len(points))] @ vp.T
            ndc = h[:, :2] / h[:, 3:4]
            nlo, nhi = ndc.min(0), ndc.max(0)
            ext = max((nhi - nlo).max() / 2 / margin, 1e-3)
            # desloca o alvo para o centro do retângulo projetado (no plano da câmera)
            V = np.linalg.inv(vp)
            mid = (nlo + nhi) / 2
            ndc_c = vp @ np.r_[center, 1]; ndc_c /= ndc_c[3]
            tgt = V @ np.r_[mid, ndc_c[2], 1]; tgt /= tgt[3]
            center = tgt[:3]
            dist *= ext
        return center, dist

    @staticmethod
    def _fmt(prog):
        f, n = layout(prog)
        return (f, *n)

    def ambient_occlusion(self, p, tris, ndirs=96, res=768, smooth=3):
        """Oclusão ambiente por vértice: fração das direções da esfera em que o vértice é visível de fora
        do personagem (mapas de profundidade ortográficos na GPU), x2 (superfície exposta vê meia esfera).
        Não usa normais (parte das faces do PS1 tem orientação ambígua); depois suaviza pela malha."""
        c = self.ctx
        data = np.zeros((len(tris) * 3, NFLOATS), 'f4')
        data[:, :3] = p[tris].reshape(-1, 3)
        vbo = c.buffer(data.tobytes())
        vao = c.vertex_array(self.p_depth, [(vbo, *self._fmt(self.p_depth))])
        tex = c.depth_texture((res, res))
        fbo = c.framebuffer(depth_attachment=tex)
        lo, hi = p.min(0), p.max(0)
        center, R = (lo + hi) / 2, np.linalg.norm(hi - lo) / 2 * 1.02
        i = np.arange(ndirs) + 0.5
        phi = np.arccos(1 - 2 * i / ndirs); th = np.pi * (1 + 5 ** 0.5) * i
        dirs = np.stack([np.cos(th) * np.sin(phi), np.cos(phi), np.sin(th) * np.sin(phi)], 1)
        acc = np.zeros(len(p))
        ph = np.c_[p, np.ones(len(p))]
        bias = 0.012 / 3          # em profundidade normalizada (fração do volume)
        for d in dirs:
            V = look_at(center + d * R * 2, center, up=(0, 1, 0) if abs(d[1]) < 0.95 else (1, 0, 0))
            P = ortho(-R, R, -R, R, R * 0.5, R * 3.5)
            fbo.use(); c.viewport = (0, 0, res, res); fbo.clear(depth=1.0)
            self.p_depth['u_light_vp'].write(_m(P @ V))
            vao.render()
            depth = np.frombuffer(tex.read(), 'f4').reshape(res, res)
            q = ph @ (P @ V).T
            x = np.clip(((q[:, 0] * 0.5 + 0.5) * res).astype(int), 0, res - 1)
            y = np.clip(((q[:, 1] * 0.5 + 0.5) * res).astype(int), 0, res - 1)
            z = q[:, 2] * 0.5 + 0.5
            vis = z - bias <= depth[y, x]
            acc += vis
        vao.release(); vbo.release(); fbo.release(); tex.release()
        ao = np.minimum(acc / ndirs * 2.0, 1.0)
        # suavização laplaciana (vizinhos pelas arestas): tira o salpicado de vértices isolados
        e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
        e = np.concatenate([e, e[:, ::-1]])
        deg = np.bincount(e[:, 0], minlength=len(p)).astype(float)
        for _ in range(smooth):
            nb = np.bincount(e[:, 0], weights=ao[e[:, 1]], minlength=len(p))
            ao = 0.5 * ao + 0.5 * nb / np.maximum(deg, 1)
        return ao

    def _target(self, size, samples):
        c = self.ctx
        fbo = c.framebuffer(color_attachments=[c.renderbuffer(size, 4, samples=samples)],
                            depth_attachment=c.depth_renderbuffer(size, samples=samples))
        return fbo

    def _read(self, fbo, size, samples):
        if samples > 1:
            res = self.ctx.framebuffer(color_attachments=[self.ctx.renderbuffer(size, 4)])
            self.ctx.copy_framebuffer(res, fbo)
            fbo = res
        img = np.frombuffer(fbo.read(components=4), np.uint8).reshape(size[1], size[0], 4)[::-1]
        return Image.fromarray(img[..., :3].copy())

    def render_ps1(self, data, vp, size, ldir=(0.35, -0.75, -0.55), amb=0.45, bg=(0.10, 0.10, 0.13), stage=None,
                   view=None):
        c = self.ctx
        fbo = self._target(size, 0)
        fbo.use(); c.viewport = (0, 0, *size)
        c.clear(*bg, 1.0)
        vbo = c.buffer(np.ascontiguousarray(data, 'f4').tobytes())
        vao = c.vertex_array(self.p_ps1, [(vbo, *self._fmt(self.p_ps1))])
        self.p_ps1['u_vp'].write(_m(vp))
        l = np.array(ldir, float); l /= np.linalg.norm(l)
        self.p_ps1['u_ldir'].value = tuple(l); self.p_ps1['u_amb'].value = amb
        self.p_ps1['u_stage'].value = 1 if stage is not None else 0
        if stage is not None:
            w = stage_world(stage, view)
            self.p_ps1['u_sdir'].write(np.asarray(w, 'f4').tobytes())
            self.p_ps1['u_scol'].write(np.asarray(stage['lcol'], 'f4').tobytes())
            self.p_ps1['u_sbk'].value = tuple(stage['bk'])
        self.tex_ps1.use(0); self.p_ps1['u_tex'].value = 0
        vao.render()
        img = self._read(fbo, size, 0)
        vao.release(); vbo.release(); fbo.release()
        return img

    def set_ps1_layers(self, layers):
        """Troca as camadas do ORIGINAL (ex.: página do rosto com a casa copiada, tools/remake/faces.py)."""
        self.tex_ps1.release()
        self.tex_ps1 = self._array(layers, linear=False)

    def render_modern(self, data, vp, eye, size, center, radius, toon=False, ss=2, samples=8,
                      outline_width=0.0, rough=0.5, floor_y=0.0, stage=None, view=None, remap=()):
        """remap: [(máscara de camadas, x0, y0, x1, y1, du, dv)] em texels da página (expressão do rosto)."""
        c = self.ctx
        key = np.array([-0.45, -0.8, -0.55]); key /= np.linalg.norm(key)
        if stage is not None:   # a sombra vem da luz principal do palco (key aponta da luz para a cena)
            sw = stage_world(stage, view)
            key = -sw[0]
        # shadow map
        lv = look_at(np.asarray(center) - key * radius * 3, center)
        lp = ortho(-radius * 1.8, radius * 1.8, -radius * 1.8, radius * 1.8, radius * 0.5, radius * 6)
        lvp = lp @ lv
        vbo = c.buffer(np.ascontiguousarray(data, 'f4').tobytes())
        vao_d = c.vertex_array(self.p_depth, [(vbo, *self._fmt(self.p_depth))])
        self.shadow_fbo.use(); c.viewport = (0, 0, self.shadow_size, self.shadow_size)
        self.shadow_fbo.clear(depth=1.0)
        self.p_depth['u_light_vp'].write(_m(lvp))
        vao_d.render()
        big = (size[0] * ss, size[1] * ss)
        fbo = self._target(big, samples)
        fbo.use(); c.viewport = (0, 0, *big)
        c.clear(0, 0, 0, 1.0)
        top, bot = ((0.16, 0.15, 0.22), (0.05, 0.05, 0.07))
        c.disable(moderngl.DEPTH_TEST)
        self.p_bg['u_top'].value = top; self.p_bg['u_bot'].value = bot
        self.vao_bg.render(moderngl.TRIANGLE_STRIP)
        c.enable(moderngl.DEPTH_TEST)
        st = 1.0 / self.shadow_size
        pf = self.p_floor
        pf['u_vp'].write(_m(vp)); pf['u_light_vp'].write(_m(lvp)); pf['u_r'].value = radius * 1.6
        pf['u_c'].value = (center[0], floor_y, center[2])
        pf['u_shadow_texel'].value = st; pf['u_col'].value = (0.22, 0.21, 0.25)
        self.shadow_tex.use(1); pf['u_shadow'].value = 1
        c.enable(moderngl.BLEND); c.blend_func = moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA
        c.depth_mask = False
        self.vao_floor.render(moderngl.TRIANGLE_STRIP)
        c.depth_mask = True
        c.disable(moderngl.BLEND)
        p = self.p_mod
        vao = c.vertex_array(p, [(vbo, *self._fmt(p))])
        p['u_vp'].write(_m(vp)); p['u_light_vp'].write(_m(lvp))
        p['u_eye'].value = tuple(eye)
        p['u_key_dir'].value = tuple(key); p['u_key_col'].value = (1.9, 1.72, 1.5)
        fill = np.array([0.7, -0.25, -0.6]); fill /= np.linalg.norm(fill)
        p['u_fill_dir'].value = tuple(fill); p['u_fill_col'].value = (0.28, 0.34, 0.5)
        rim = np.array([0.2, -0.3, 1.0]); rim /= np.linalg.norm(rim)
        p['u_rim_dir'].value = tuple(rim); p['u_rim_col'].value = (0.9, 0.88, 1.0)
        p['u_sky'].value = (0.2, 0.21, 0.27); p['u_ground'].value = (0.09, 0.075, 0.07)
        p['u_rough'].value = rough; p['u_toon'].value = 1.0 if toon else 0.0
        p['u_stage'].value = 1 if stage is not None else 0
        if stage is not None:
            p['u_sdir'].write(np.asarray(sw, 'f4').tobytes())
            p['u_scol'].write(np.asarray(stage['lcol'], 'f4').tobytes())
            p['u_sbk'].value = tuple(stage['bk'])
            k0 = np.asarray(stage['lcol'][0]); kc = 2.7 / max(k0.sum(), 1e-3)
            rd = np.array([-stage['ldir'][0][0], stage['ldir'][0][1] * 0.5 - 0.3, 0.9])
            rdw = stage_world(dict(ldir=[rd / np.linalg.norm(rd)]), view)[0]
            p['u_rim_dir'].value = tuple(-rdw); p['u_rim_col'].value = tuple(k0 * kc * 0.95)
        p['u_shadow_texel'].value = st
        rm = list(remap)[:8] + [(0, 0, 0, 0, 0, 0, 0)] * (8 - min(len(remap), 8))
        p['u_nrm'].value = min(len(remap), 8)
        p['u_rm_mask'].write(np.array([r[0] for r in rm], 'i4').tobytes())
        p['u_rm_rect'].write(np.array([r[1:5] for r in rm], 'f4').tobytes())
        p['u_rm_off'].write(np.array([r[5:7] for r in rm], 'f4').tobytes())
        self.tex_hd.use(0); p['u_tex'].value = 0; p['u_shadow'].value = 1
        if self.tex_nmap is not None:
            self.tex_nmap.use(2); p['u_nmap'].value = 2
        p['u_nmap_on'].value = self.nmap_on if self.tex_nmap is not None else 0.0
        vao.render()
        if outline_width > 0:
            vao_o = c.vertex_array(self.p_out, [(vbo, *self._fmt(self.p_out))])
            self.p_out['u_vp'].write(_m(vp)); self.p_out['u_width'].value = outline_width
            c.enable(moderngl.CULL_FACE); c.front_face = 'ccw'
            c.cull_face = 'front'
            vao_o.render()
            c.disable(moderngl.CULL_FACE)
            vao_o.release()
        img = self._read(fbo, big, samples)
        vao.release(); vao_d.release(); vbo.release(); fbo.release()
        return img.resize(size, Image.LANCZOS) if ss > 1 else img
