/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */
#include "usd_writer_transform.h"
#include "usd_hierarchy_iterator.h"

#include <pxr/base/gf/matrix4f.h>
#include <pxr/usd/usdGeom/xform.h>

extern "C" {
#include "BKE_object.h"

#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_math_vector.h"

#include "DNA_layer_types.h"
}

namespace USD {

static const float UNIT_M4[4][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1},
};

USDTransformWriter::USDTransformWriter(const USDExporterContext &ctx) : USDAbstractWriter(ctx)
{
}

void mat4_to_loc_eul_size(float loc[3], float eul[3], float size[3], const float (*M)[4])
{
  float rot[3][3];

  mat4_to_loc_rot_size(loc, rot, size, M);
  mat3_to_eul(eul, rot);
}

void convert_axes(int src_forward, int src_up, int dst_forward, int dst_up, float mat[4][4])
{
  float loc[3], eul[3], size[3];
  mat4_to_loc_eul_size(loc, eul, size, mat);
  float mattest[4][4];
  loc_eul_size_to_mat4(mattest, loc, eul, size);
  float mrot[3][3];
  mat3_from_axis_conversion(src_forward, src_up, dst_forward, dst_up, mrot);
  transpose_m3(mrot);  // transpose = inversion for this matrix
  mul_m3_v3(mrot, loc);
  mul_m3_v3(mrot, eul);
  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      if (mrot[x][y] < 0.0f) {
        mrot[x][y] *= -1.0f;
      }
    }
  }
  mul_m3_v3(mrot, size);
  loc_eul_size_to_mat4(mat, loc, eul, size);
}

void USDTransformWriter::do_write(HierarchyContext &context)
{
  pxr::UsdGeomXform xform;

  if (usd_export_context_.export_params.export_as_overs) {
    // Override existing prim on stage
    xform = pxr::UsdGeomXform(
        usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path));
  }
  else {
    // If prim exists, cast to UsdGeomXform (Solves merge transform and shape issue for animated
    // exports)
    pxr::UsdPrim existing_prim = usd_export_context_.stage->GetPrimAtPath(
        usd_export_context_.usd_path);
    if (existing_prim.IsValid()) {
      xform = pxr::UsdGeomXform(existing_prim);
    }
    else {
      xform = pxr::UsdGeomXform::Define(usd_export_context_.stage, usd_export_context_.usd_path);
    }
  }

  if (usd_export_context_.export_params.export_transforms) {
    float parent_relative_matrix[4][4];
    // The object matrix relative to the parent.
    if (usd_export_context_.export_params.convert_orientation) {
      float parent_inv_world[4][4], matrix_world[4][4];
      copy_m4_m4(matrix_world, context.matrix_world);
      copy_m4_m4(parent_inv_world, context.parent_matrix_inv_world);
      invert_m4(parent_inv_world);
      convert_axes(USD_GLOBAL_FORWARD_Y,
                   USD_GLOBAL_UP_Z,
                   usd_export_context_.export_params.forward_axis,
                   usd_export_context_.export_params.up_axis,
                   parent_inv_world);
      invert_m4(parent_inv_world);
      convert_axes(USD_GLOBAL_FORWARD_Y,
                   USD_GLOBAL_UP_Z,
                   usd_export_context_.export_params.forward_axis,
                   usd_export_context_.export_params.up_axis,
                   matrix_world);
      mul_m4_m4m4(parent_relative_matrix, parent_inv_world, matrix_world);
    }

    // USD Xforms are by default set with an identity transform.
    // This check ensures transforms of non-identity are authored
    // preventing usd composition collisions up and down stream.
    if (usd_export_context_.export_params.export_identity_transforms ||
        !compare_m4m4(parent_relative_matrix, UNIT_M4, 0.000000001f)) {
      if (!xformOp_) {
        xformOp_ = xform.AddTransformOp();
      }

      xformOp_.Set(pxr::GfMatrix4d(parent_relative_matrix), get_export_time_code());
    }
  }

  if (usd_export_context_.export_params.export_custom_properties && context.object) {
    auto prim = xform.GetPrim();
    write_id_properties(prim, context.object->id, get_export_time_code());
  }
}

bool USDTransformWriter::check_is_animated(const HierarchyContext &context) const
{
  if (context.duplicator != NULL) {
    /* This object is being duplicated, so could be emitted by a particle system and thus
     * influenced by forces. TODO(Sybren): Make this more strict. Probably better to get from the
     * depsgraph whether this object instance has a time source. */
    return true;
  }
  // TODO: This fails for a specific set of drivers and rig setups...
  // Setting 'context.animation_check_include_parent' to true fixed it...
  return BKE_object_moves_in_time(context.object, context.animation_check_include_parent);
}

}  // namespace USD
