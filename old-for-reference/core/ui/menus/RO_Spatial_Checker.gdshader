shader_type spatial;

render_mode unshaded, skip_vertex_transform;

uniform float plate_dist = 1.0;
uniform float checkerRepeat = 8.0;
uniform float checkerRotate = 0.1;
uniform float inTransparency = 1.0;
uniform vec2 outlineSize = vec2(0.965, 0.95);

uniform vec2 checkerScrollSpeed = vec2(0.2, -0.1);

uniform vec3 outlineColor : source_color = vec3(0.9, 0.5, 0.4);

uniform vec3 topGradientCheck1 : source_color = vec3(1.0, 0.6, 0.7);
uniform vec3 bottomGradientCheck1 : source_color = vec3(1.0, 0.15, 0.6);

uniform vec3 topGradientCheck2 : source_color = vec3(0.6, 0.2, 0.3);
uniform vec3 bottomGradientCheck2 : source_color = vec3(0.4, 0.05, 0.2);

uniform sampler2D checkerTexture : repeat_enable;

void vertex()
{
    mat4 modified_model_view = VIEW_MATRIX * mat4(
        INV_VIEW_MATRIX[0],
        INV_VIEW_MATRIX[1],
        INV_VIEW_MATRIX[2],
        MODEL_MATRIX[3]
    );
    MODELVIEW_MATRIX = modified_model_view;
	vec3 OLD_VERTEX = VERTEX;
	VERTEX = (VERTEX * plate_dist) + (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	NORMAL = normalize((MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

vec2 rotate(vec2 uv, vec2 pivot, float angle)
{
	mat2 rotation = mat2(vec2(sin(angle), -cos(angle)),
						vec2(cos(angle), sin(angle)));

	uv -= pivot;
	uv = uv * rotation;
	uv += pivot;
	return uv;
}

void fragment(){
	float ratio = VIEWPORT_SIZE.x / VIEWPORT_SIZE.y;
	float repeat = checkerRepeat;
	vec2 loopingUV = vec2(SCREEN_UV.x * repeat * ratio, SCREEN_UV.y * repeat);
	loopingUV = rotate(loopingUV, vec2(0, 0), checkerRotate);
	loopingUV.x = loopingUV.x + TIME * checkerScrollSpeed.x;
	loopingUV.y = loopingUV.y + TIME * checkerScrollSpeed.y;
	vec3 color1 = mix(topGradientCheck1, bottomGradientCheck1, UV.y);
	vec3 color2 = mix(topGradientCheck2, bottomGradientCheck2, UV.y);
	vec4 textureColor = texture(checkerTexture, loopingUV);
	vec3 checkerColor = mix(color1, color2, textureColor.r);
	float dist = clamp((max(max(UV.x, abs(1.0 - UV.x)) * outlineSize.x, max(UV.y, abs(1.0 - UV.y))) - outlineSize.y) * 1000.0, 0.0, 1.0);
	vec3 finalColor = mix(checkerColor, outlineColor, dist);
	ALPHA = inTransparency;
	ALBEDO = finalColor;
}