float track_distance_fade_coverage(vec3 view_position, float fade_distance) {
	float fog_mix = max(0.0, (length(view_position) - 500.0) / fade_distance);
	fog_mix = clamp(pow(fog_mix, 2.0), 0.0, 1.0);
	fog_mix = sin(fog_mix * PI * 0.5);
	return 1.0 - fog_mix;
}
