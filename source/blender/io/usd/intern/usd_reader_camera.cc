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

#include "usd_reader_camera.h"
#include "usd_reader_prim.h"

extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_camera_types.h"
#include "DNA_constraint_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_camera.h"
#include "BKE_constraint.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_math_geom.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "WM_api.h"
#include "WM_types.h"
}

#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>

void USDCameraReader::createObject(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Creating blender camera for prim: %s", m_prim.GetPath().GetText());
  Camera *bcam = static_cast<Camera *>(BKE_camera_add(bmain, m_name.c_str()));

  m_object = BKE_object_add_only_object(bmain, OB_CAMERA, m_name.c_str());
  m_object->data = bcam;
}

void USDCameraReader::readObjectData(Main *bmain, double motionSampleTime)
{
  Camera *bcam = (Camera *)m_object->data;

  pxr::UsdGeomCamera cam_prim = pxr::UsdGeomCamera::Get(m_stage, m_prim.GetPath());

  pxr::VtValue val;
  cam_prim.GetFocalLengthAttr().Get(&val, motionSampleTime);
  pxr::VtValue verApOffset;
  cam_prim.GetVerticalApertureOffsetAttr().Get(&verApOffset, motionSampleTime);
  pxr::VtValue horApOffset;
  cam_prim.GetHorizontalApertureOffsetAttr().Get(&horApOffset, motionSampleTime);
  pxr::VtValue clippingRangeVal;
  cam_prim.GetClippingRangeAttr().Get(&clippingRangeVal, motionSampleTime);
  pxr::VtValue focalDistanceVal;
  cam_prim.GetFocusDistanceAttr().Get(&focalDistanceVal, motionSampleTime);
  pxr::VtValue fstopVal;
  cam_prim.GetFStopAttr().Get(&fstopVal, motionSampleTime);
  pxr::VtValue projectionVal;
  cam_prim.GetProjectionAttr().Get(&projectionVal, motionSampleTime);
  pxr::VtValue verAp;
  cam_prim.GetVerticalApertureAttr().Get(&verAp, motionSampleTime);
  pxr::VtValue horAp;
  cam_prim.GetHorizontalApertureAttr().Get(&horAp, motionSampleTime);

  bcam->lens = val.Get<float>();
  // bcam->sensor_x = 0.0f;
  // bcam->sensor_y = 0.0f;
  bcam->shiftx = verApOffset.Get<float>();
  bcam->shifty = horApOffset.Get<float>();

  bcam->type = (projectionVal.Get<pxr::TfToken>().GetString() == "perspective") ? CAM_PERSP :
                                                                                  CAM_ORTHO;

  bcam->clip_start = max_ff(0.1f, clippingRangeVal.Get<pxr::GfVec2f>()[0]);
  bcam->clip_end = clippingRangeVal.Get<pxr::GfVec2f>()[1];

  bcam->dof.focus_distance = focalDistanceVal.Get<float>();
  bcam->dof.aperture_fstop = static_cast<float>(fstopVal.Get<float>());

  if (bcam->type == CAM_ORTHO) {
    bcam->ortho_scale = max_ff(verAp.Get<float>(), horAp.Get<float>());
  }

  USDXformReader::readObjectData(bmain, motionSampleTime);
}