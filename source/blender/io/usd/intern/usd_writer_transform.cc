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

  if (usd_export_context_.export_params.export_transforms &&
      !usd_export_context_.export_params.apply_transforms) {
    float parent_relative_matrix[4][4];
    // The object matrix relative to the parent.
    mul_m4_m4m4(parent_relative_matrix, context.parent_matrix_inv_world, context.matrix_world);

    if (usd_export_context_.export_params.convert_orientation) {
      // In order to correct for the baked rotation, we have to sandwich the matrix
      // with the rotation matrix and the inverse rotation matrix.
      float mrot[3][3];
      float mrot4[4][4];
      // This returns the inverse rotation
      mat3_from_axis_conversion(USD_GLOBAL_FORWARD_Y,
                                USD_GLOBAL_UP_Z,
                                usd_export_context_.export_params.forward_axis,
                                usd_export_context_.export_params.up_axis,
                                mrot);
      copy_m4_m3(mrot4, mrot);
      mul_m4_m4m4(parent_relative_matrix, parent_relative_matrix, mrot4);
      transpose_m4(mrot4);  // Transpose is equivalent to inverse for this matrix
      mul_m4_m4m4(parent_relative_matrix, mrot4, parent_relative_matrix);
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
