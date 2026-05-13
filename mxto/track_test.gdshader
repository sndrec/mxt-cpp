shader_type spatial;
render_mode unshaded;

varying vec3 local_vertex;

void vertex() {
  local_vertex = VERTEX;
}

void fragment() {
  vec3 origin = local_vertex * 0.1;
  vec3 dir = -(inverse(MODEL_MATRIX)*INV_VIEW_MATRIX*vec4(VIEW,0)).xyz;
  float coverage = 0.0f;
  for (int i=0; i < 500; i++) {
    vec3 pos = (mod(origin, 1.0f) - vec3(0.5f));
    float de = distance(pos, vec3(0, 0, 0)) - 0.2;
      if (de < 0.0001f) {
        coverage = pow(0.99f, float(i));
        break;
      }
    origin += dir * de;
  }
  ALBEDO = vec3(abs(MODEL_MATRIX*vec4(1, 0, 0, 0)).xyz) * coverage;
  ALPHA = coverage;
}