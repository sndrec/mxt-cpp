#include <track/curve_matrix.h>

godot::Transform3D CurveMatrix::get_root_transform_at_time(float in_time) const
{
	float pos_x_sample = position_x->sample(in_time);
	float pos_y_sample = position_y->sample(in_time);
	float pos_z_sample = position_z->sample(in_time);

	float rot_xx_sample = basis_xx->sample(in_time);
	float rot_xy_sample = basis_xy->sample(in_time);
	float rot_xz_sample = basis_xz->sample(in_time);
	float rot_yx_sample = basis_yx->sample(in_time);
	float rot_yy_sample = basis_yy->sample(in_time);
	float rot_yz_sample = basis_yz->sample(in_time);
	float rot_zx_sample = basis_zx->sample(in_time);
	float rot_zy_sample = basis_zy->sample(in_time);
	float rot_zz_sample = basis_zz->sample(in_time);

	float sca_x_sample = scale_x->sample(in_time);
	float sca_y_sample = scale_y->sample(in_time);
	float sca_z_sample = scale_z->sample(in_time);

	godot::Vector3 position = godot::Vector3(pos_x_sample, pos_y_sample, pos_z_sample);

	godot::Basis rotation = godot::Basis(godot::Vector3(rot_xx_sample, rot_xy_sample, rot_xz_sample), godot::Vector3(rot_yx_sample, rot_yy_sample, rot_yz_sample), godot::Vector3(rot_zx_sample, rot_zy_sample, rot_zz_sample));

	godot::Vector3 scale = godot::Vector3(sca_x_sample, sca_y_sample, sca_z_sample);

	return godot::Transform3D(rotation, position).scaled_local(scale);
};
