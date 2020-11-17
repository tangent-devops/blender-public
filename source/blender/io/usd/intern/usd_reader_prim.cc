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

#include "usd_reader_prim.h"

extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_modifier_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_constraint.h"
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

USDPrimReader::USDPrimReader(pxr::UsdStageRefPtr stage,
                             const pxr::UsdPrim &object,
                             const USDImportParams &import_params,
                             ImportSettings &settings)
    : m_name(object.GetName().GetString()),
      m_prim_path(object.GetPrimPath().GetString()),
      m_prim(object),
      m_stage(stage),
      m_import_params(import_params),
      m_settings(&settings)
{
  //@TODO(bjs): This should be handled better
  if (m_name == "/")
    m_name = "root";
}

USDPrimReader::~USDPrimReader()
{
}

const pxr::UsdPrim &USDPrimReader::prim() const
{
  return m_prim;
}

Object *USDPrimReader::object() const
{
  return m_object;
}

void USDPrimReader::object(Object *ob)
{
  m_object = ob;
}

bool USDPrimReader::valid() const
{
  return m_prim.IsValid();
}

void USDPrimReader::createObject(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Creating blender object for prim: %s", m_prim.GetPath().GetText());
  m_object = BKE_object_add_only_object(bmain, OB_EMPTY, m_name.c_str());
  m_object->empty_drawsize = 0.1f;
  m_object->data = NULL;
}

void USDPrimReader::readObjectData(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Reading prim data: %s", m_prim.GetPath().GetText());
  // m_object = BKE_object_add_only_object(bmain, OB_EMPTY, m_prim_path.c_str());
  // m_object->data = NULL;
}

void USDPrimReader::addCacheModifier()
{
}

int USDPrimReader::refcount() const
{
  return m_refcount;
}

void USDPrimReader::incref()
{
  m_refcount++;
}

void USDPrimReader::decref()
{
  m_refcount--;
  BLI_assert(m_refcount >= 0);
}
