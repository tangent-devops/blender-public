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

#include "usd_reader_geom.h"
#include "usd_reader_prim.h"

#include "MEM_guardedalloc.h"
extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_camera_types.h"
#include "DNA_constraint_types.h"
#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_camera.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_lib_id.h"
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
#include <pxr/usd/usdGeom/mesh.h>

void USDGeomReader::createObject(Main *bmain, double motionSampleTime)
{
}

bool USDGeomReader::valid() const
{
  return true;
  // return m_schema.valid();
}

bool USDGeomReader::topology_changed(Mesh *existing_mesh, double motionSampleTime)
{
  return true;
}

void USDGeomReader::readObjectData(Main *bmain, double motionSampleTime)
{
}

Mesh *USDGeomReader::read_mesh(struct Mesh *existing_mesh,
                               double motionSampleTime,
                               int read_flag,
                               float vel_scale,
                               const char **err_str)
{
  return nullptr;
}

void USDGeomReader::addCacheModifier()
{
  ModifierData *md = modifier_new(eModifierType_MeshSequenceCache);
  BLI_addtail(&m_object->modifiers, md);

  MeshSeqCacheModifierData *mcmd = reinterpret_cast<MeshSeqCacheModifierData *>(md);

  mcmd->cache_file = m_settings->cache_file;
  id_us_plus(&mcmd->cache_file->id);
  mcmd->read_flag = m_import_params.global_read_flag;
  // mcmd->vel_fac = m_settings->vel_scale;

  BLI_strncpy(mcmd->object_path, m_prim.GetPath().GetString().c_str(), FILE_MAX);
}

void USDGeomReader::addSubdivModifier()
{
  ModifierData *md = modifier_new(eModifierType_Subsurf);
  BLI_addtail(&m_object->modifiers, md);

  //SubsurfModifierData *subd = reinterpret_cast<SubsurfModifierData *>(md);
}