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
 */

#include "usd_reader_xform.h"
#include "usd_reader_prim.h"

extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_modifier_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_constraint.h"
#include "BKE_lib_id.h"
#include "BKE_library.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "BLI_listbase.h"
#include "BLI_math_geom.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "WM_api.h"
#include "WM_types.h"
}

#include <pxr/base/gf/math.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>

void USDXformReader::createObject(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Creating blender object for prim: %s", m_prim.GetPath().GetText());
  m_object = BKE_object_add_only_object(bmain, OB_EMPTY, m_name.c_str());
  m_object->empty_drawsize = 0.1f;
  m_object->data = NULL;
}

void USDXformReader::readObjectData(Main *bmain, double motionSampleTime)
{
  USDPrimReader::readObjectData(bmain, motionSampleTime);

  WM_reportf(RPT_WARNING, "Reading specific xform data: %s", m_prim.GetPath().GetText());

  bool is_constant;
  float transform_from_usd[4][4];

  read_matrix(transform_from_usd, motionSampleTime, 1.0f, is_constant);

  if (!is_constant) {
    bConstraint *con = BKE_constraint_add_for_object(
        m_object, NULL, CONSTRAINT_TYPE_TRANSFORM_CACHE);
    bTransformCacheConstraint *data = static_cast<bTransformCacheConstraint *>(con->data);
    BLI_strncpy(data->object_path, m_prim.GetPath().GetText(), FILE_MAX);

    data->cache_file = m_settings->cache_file;
    id_us_plus(&data->cache_file->id);
  }

  BKE_object_apply_mat4(m_object, transform_from_usd, true, false);
}

typedef float m4[4];

void USDXformReader::read_matrix(float r_mat[4][4] /* local matrix */,
                                 const float time,
                                 const float scale,
                                 bool &is_constant)
{
  is_constant = true;
  pxr::UsdGeomXformable xformable = pxr::UsdGeomXformable(
      m_prim);  // pxr::UsdGeomXformable::Get(m_stage, m_prim.GetPath());

  bool resetsXformStack = false;
  std::vector<pxr::UsdGeomXformOp> orderedXformOps = xformable.GetOrderedXformOps(
      &resetsXformStack);

  unit_m4(r_mat);

  if (orderedXformOps.size() <= 0)
    return;

  int opCount = 0;
  for (std::vector<pxr::UsdGeomXformOp>::iterator I = orderedXformOps.begin();
       I != orderedXformOps.end();
       ++I) {

    pxr::UsdGeomXformOp &xformOp = (*I);

    is_constant = !xformOp.MightBeTimeVarying();

    pxr::GfMatrix4d mat = xformOp.GetOpTransform(time) * pxr::GfMatrix4d(1.0f).SetScale(scale);

    float t_mat[4][4];
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        t_mat[j][i] = mat[j][i];
      }
    }
    mul_m4_m4m4(r_mat, r_mat, t_mat);
    opCount++;
  }
}