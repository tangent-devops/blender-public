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
#include "usd_util.h"
#include "usd_hierarchy_iterator.h"

#include "usd_reader_camera.h"
#include "usd_reader_curve.h"
#include "usd_reader_geom.h"
#include "usd_reader_light.h"
#include "usd_reader_mesh.h"
#include "usd_reader_nurbs.h"
#include "usd_reader_prim.h"
#include "usd_reader_stage.h"
#include "usd_reader_volume.h"
#include "usd_reader_xform.h"

extern "C" {
#include "BKE_animsys.h"
#include "BKE_colorband.h"
#include "BKE_colortools.h"
#include "BKE_key.h"
#include "BKE_node.h"

#include "DNA_color_types.h"
#include "DNA_light_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BKE_blender_version.h"
#include "BKE_cachefile.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_layer.h"
#include "BKE_light.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BKE_world.h"

#include "BLI_fileops.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
}
#include "MEM_guardedalloc.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdLux/light.h>

namespace USD {

USDPrimReader *create_reader(const pxr::UsdStageRefPtr &stage,
                             const pxr::UsdPrim &prim,
                             const USDImportParams &params,
                             ImportSettings &settings)
{
  USDPrimReader *reader = nullptr;

  if (params.import_cameras && prim.IsA<pxr::UsdGeomCamera>()) {
    reader = new USDCameraReader(stage, prim, params, settings);
  }
  else if (params.import_curves && prim.IsA<pxr::UsdGeomBasisCurves>()) {
    reader = new USDCurvesReader(stage, prim, params, settings);
  }
  else if (params.import_curves && prim.IsA<pxr::UsdGeomNurbsCurves>()) {
    reader = new USDNurbsReader(stage, prim, params, settings);
  }
  else if (params.import_meshes && prim.IsA<pxr::UsdGeomMesh>()) {
    reader = new USDMeshReader(stage, prim, params, settings);
  }
  else if (params.import_lights && prim.IsA<pxr::UsdLuxLight>()) {
    reader = new USDLightReader(stage, prim, params, settings);
  }
  else if (params.import_volumes && prim.IsA<pxr::UsdVolVolume>()) {
    reader = new USDVolumeReader(stage, prim, params, settings);
  }
  else {
    reader = new USDXformReader(stage, prim, params, settings);
  }

  return reader;
}

// TODO: The handle does not have the proper import params or settings
USDPrimReader *create_fake_reader(USD::USDStageReader *archive, const pxr::UsdPrim &prim)
{
  USDPrimReader *reader = nullptr;
  if (prim.IsA<pxr::UsdGeomCamera>()) {
    reader = new USDCameraReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else if (prim.IsA<pxr::UsdGeomBasisCurves>()) {
    reader = new USDCurvesReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else if (prim.IsA<pxr::UsdGeomNurbsCurves>()) {
    reader = new USDNurbsReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else if (prim.IsA<pxr::UsdGeomMesh>()) {
    reader = new USDMeshReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else if (prim.IsA<pxr::UsdLuxLight>()) {
    reader = new USDLightReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else if (prim.IsA<pxr::UsdVolVolume>()) {
    reader = new USDVolumeReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  else {
    reader = new USDXformReader(archive->stage(), prim, archive->params(), archive->settings());
  }
  return reader;
}

}  // Namespace USD