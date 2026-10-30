void main() {
  ivec2 p = ivec2(gl_FragCoord.xy) - u_org;
  vec3 c = texelFetch(u_tex, p, 0).rgb;
  float m = texelFetch(u_mask, p, 0).r;
  o = vec4(mix(c, vec3(1.0, 0.0, 0.0), 0.5 * m), 1.0);
}
