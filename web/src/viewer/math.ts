// Minimal column-major 4x4 matrix helpers (WebGPU/WGSL layout).
export type Mat4 = Float32Array;
export const identity = (): Mat4 => new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
export function multiply(a: Mat4, b: Mat4): Mat4 {
  const o = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return o;
}
export function translation(x: number, y: number, z: number): Mat4 { const m = identity(); m[12] = x; m[13] = y; m[14] = z; return m; }
export function scaling(x: number, y: number, z: number): Mat4 { const m = identity(); m[0] = x; m[5] = y; m[10] = z; return m; }
export function rotX(a: number): Mat4 { const c = Math.cos(a), s = Math.sin(a); const m = identity(); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; return m; }
export function rotY(a: number): Mat4 { const c = Math.cos(a), s = Math.sin(a); const m = identity(); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; }
export function rotZ(a: number): Mat4 { const c = Math.cos(a), s = Math.sin(a); const m = identity(); m[0] = c; m[1] = s; m[4] = -s; m[5] = c; return m; }
export function perspective(fovY: number, aspect: number, near: number, far: number): Mat4 {
  const f = 1 / Math.tan(fovY / 2), m = new Float32Array(16);
  m[0] = f / aspect; m[5] = f; m[10] = far / (near - far); m[11] = -1; m[14] = near * far / (near - far);
  return m;
}
export function transformPoint(m: Mat4, x: number, y: number, z: number): [number, number, number] {
  return [m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]];
}
export function transformDir(m: Mat4, x: number, y: number, z: number): [number, number, number] {
  return [m[0] * x + m[4] * y + m[8] * z, m[1] * x + m[5] * y + m[9] * z, m[2] * x + m[6] * y + m[10] * z];
}
/** PS1 angle (4096 = 360 degrees) to radians. */
export const psxAngle = (a: number) => (a / 4096) * Math.PI * 2;
/** PsyQ RotMatrix: m = Rx * Ry * Rz (rotation applied to vector in order Z, then Y, then X). */
export function psxRotMatrix(rx: number, ry: number, rz: number): Mat4 {
  return multiply(rotX(psxAngle(rx)), multiply(rotY(psxAngle(ry)), rotZ(psxAngle(rz))));
}
