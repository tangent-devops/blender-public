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

#include "usd_reader_curve.h"
#include "usd_reader_prim.h"

#include "MEM_guardedalloc.h"
extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_camera_types.h"
#include "DNA_constraint_types.h"
#include "DNA_curve_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_camera.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_library.h"
#include "BKE_mesh.h"
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

#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/curves.h>

void USDCurvesReader::createObject(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Creating blender curve for prim: %s", m_prim.GetPath().GetText());
  m_curve = BKE_curve_add(bmain, m_name.c_str(), OB_CURVE);

  m_curve->flag |= CU_DEFORM_FILL | CU_3D;
  m_curve->actvert = CU_ACT_NONE;
  m_curve->resolu = 2;

  m_object = BKE_object_add_only_object(bmain, OB_CURVE, m_name.c_str());
  m_object->data = m_curve;
}

void USDCurvesReader::readObjectData(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Reading specific camera data: %s", m_prim.GetPath().GetText());

  Curve *cu = (Curve *)m_object->data;
  read_curve_sample(cu, motionSampleTime);

  if (curve_prim.GetPointsAttr().ValueMightBeTimeVarying()) {
    addCacheModifier();
  }

  USDXformReader::readObjectData(bmain, motionSampleTime);
}

void USDCurvesReader::read_curve_sample(Curve *cu, double motionSampleTime)
{
  curve_prim = pxr::UsdGeomBasisCurves::Get(m_stage, m_prim.GetPath());

  pxr::UsdAttribute widthsAttr = curve_prim.GetWidthsAttr();
  pxr::UsdAttribute vertexAttr = curve_prim.GetCurveVertexCountsAttr();
  pxr::UsdAttribute pointsAttr = curve_prim.GetPointsAttr();

  pxr::VtIntArray usdCounts;

  vertexAttr.Get(&usdCounts, motionSampleTime);
  int num_subcurves = usdCounts.size();

  pxr::VtVec3fArray usdPoints;
  pointsAttr.Get(&usdPoints, motionSampleTime);

  pxr::VtFloatArray usdWidths;
  widthsAttr.Get(&usdWidths, motionSampleTime);

  pxr::UsdAttribute basisAttr = curve_prim.GetBasisAttr();
  pxr::TfToken basis;
  basisAttr.Get(&basis, motionSampleTime);

  pxr::UsdAttribute typeAttr = curve_prim.GetTypeAttr();
  pxr::TfToken type;
  typeAttr.Get(&type, motionSampleTime);

  pxr::UsdAttribute wrapAttr = curve_prim.GetWrapAttr();
  pxr::TfToken wrap;
  wrapAttr.Get(&wrap, motionSampleTime);

  pxr::VtVec3fArray usdNormals;
  curve_prim.GetNormalsAttr().Get(&usdNormals, motionSampleTime);

  // If normals, extrude, else bevel
  // Perhaps to be replaced by Blender/USD Schema
  if (usdNormals.size() > 0) {
    // Set extrusion to 1.0f;
    m_curve->ext1 = 1.0f;
  }
  else {
    // Set bevel depth to 1.0f;
    m_curve->ext2 = 1.0f;
  }

  size_t idx = 0;
  for (size_t i = 0; i < num_subcurves; i++) {
    const int num_verts = usdCounts[i];
    Nurb *nu = static_cast<Nurb *>(MEM_callocN(sizeof(Nurb), "usd_getnurb"));

    if (basis == pxr::UsdGeomTokens->bspline) {
      nu->flag = CU_SMOOTH;
      nu->type = CU_NURBS;
    }
    else if (basis == pxr::UsdGeomTokens->bezier) {
      // TODO: Beziers are not properly imported as beziers...
      nu->type = CU_NURBS;
      nu->flag |= CU_SMOOTH;
      nu->flagu |= CU_NURB_ENDPOINT;
    }
    else if (basis.IsEmpty()) {
      nu->type = CU_POLY;
    }
    nu->resolu = cu->resolu;
    nu->resolv = cu->resolv;

    nu->pntsu = num_verts;
    nu->pntsv = 1;

    if (type == pxr::UsdGeomTokens->cubic) {
      nu->orderu = 4;
    }
    else if (type == pxr::UsdGeomTokens->linear) {
      nu->orderu = 2;
    }

    if (wrap == pxr::UsdGeomTokens->periodic) {
      nu->flagu |= CU_NURB_CYCLIC;
    }
    else if (wrap == pxr::UsdGeomTokens->pinned) {
      nu->flagu |= CU_NURB_ENDPOINT;
    }

    float weight = 1.0f;

    nu->bp = static_cast<BPoint *>(MEM_callocN(sizeof(BPoint) * nu->pntsu, "usd_getnurb"));
    BPoint *bp = nu->bp;

    for (int j = 0; j < nu->pntsu; j++, bp++, idx++) {
      bp->vec[0] = (float)usdPoints[idx][0];
      bp->vec[1] = (float)usdPoints[idx][1];
      bp->vec[2] = (float)usdPoints[idx][2];
      bp->vec[3] = weight;
      bp->f1 = SELECT;
      bp->weight = weight;

      float radius = m_curve->width;
      if (idx < usdWidths.size())
        radius = usdWidths[idx];

      bp->radius = radius;
    }

    BKE_nurb_knot_calc_u(nu);
    BKE_nurb_knot_calc_v(nu);

    BLI_addtail(BKE_curve_nurbs_get(cu), nu);
  }
}

Mesh *USDCurvesReader::read_mesh(struct Mesh *existing_mesh,
                                 double motionSampleTime,
                                 int read_flag,
                                 float vel_scale,
                                 const char **err_str)
{
  pxr::UsdGeomCurves curve_prim = pxr::UsdGeomCurves::Get(m_stage, m_prim.GetPath());

  pxr::UsdAttribute widthsAttr = curve_prim.GetWidthsAttr();
  pxr::UsdAttribute vertexAttr = curve_prim.GetCurveVertexCountsAttr();
  pxr::UsdAttribute pointsAttr = curve_prim.GetPointsAttr();

  pxr::VtIntArray usdCounts;

  vertexAttr.Get(&usdCounts, motionSampleTime);
  int num_subcurves = usdCounts.size();

  pxr::VtVec3fArray usdPoints;
  pointsAttr.Get(&usdPoints, motionSampleTime);

  int vertex_idx = 0;
  int curve_idx;
  Curve *curve = static_cast<Curve *>(m_object->data);

  const int curve_count = BLI_listbase_count(&curve->nurb);
  bool same_topology = curve_count == num_subcurves;

  if (same_topology) {
    Nurb *nurbs = static_cast<Nurb *>(curve->nurb.first);
    for (curve_idx = 0; nurbs; nurbs = nurbs->next, curve_idx++) {
      const int num_in_usd = usdCounts[curve_idx];
      const int num_in_blender = nurbs->pntsu;

      if (num_in_usd != num_in_blender) {
        same_topology = false;
        break;
      }
    }
  }

  if (!same_topology) {
    BKE_nurbList_free(&curve->nurb);
    read_curve_sample(curve, motionSampleTime);
  }
  else {
    Nurb *nurbs = static_cast<Nurb *>(curve->nurb.first);
    for (curve_idx = 0; nurbs; nurbs = nurbs->next, curve_idx++) {
      const int totpoint = usdCounts[curve_idx];

      if (nurbs->bp) {
        BPoint *point = nurbs->bp;

        for (int i = 0; i < totpoint; i++, point++, vertex_idx++) {
          point->vec[0] = usdPoints[vertex_idx][0];
          point->vec[1] = usdPoints[vertex_idx][1];
          point->vec[2] = usdPoints[vertex_idx][2];
        }
      }
      else if (nurbs->bezt) {
        BezTriple *bezier = nurbs->bezt;

        for (int i = 0; i < totpoint; i++, bezier++, vertex_idx++) {
          bezier->vec[1][0] = usdPoints[vertex_idx][0];
          bezier->vec[1][1] = usdPoints[vertex_idx][1];
          bezier->vec[1][2] = usdPoints[vertex_idx][2];
        }
      }
    }
  }

  return BKE_mesh_new_nomain_from_curve(m_object);
}