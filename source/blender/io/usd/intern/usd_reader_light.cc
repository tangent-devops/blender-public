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

#include "usd_reader_light.h"
#include "usd_reader_prim.h"
#include "usd_util.h"

extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_light_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_constraint.h"
#include "BKE_light.h"
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

#include <pxr/usd/usdLux/light.h>

#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

#include <iostream>

void USDLightReader::createObject(Main *bmain, double motionSampleTime)
{
  WM_reportf(RPT_WARNING, "Creating blender light for prim: %s", m_prim.GetPath().GetText());
  Light *blight = static_cast<Light *>(BKE_light_add(bmain, m_name.c_str()));

  m_object = BKE_object_add_only_object(bmain, OB_LAMP, m_name.c_str());
  m_object->data = blight;
}

void USDLightReader::readObjectData(Main *bmain, double motionSampleTime)
{
  Light *blight = (Light *)m_object->data;

  pxr::UsdLuxLight light_prim = pxr::UsdLuxLight::Get(m_stage, m_prim.GetPath());
  pxr::UsdLuxShapingAPI shapingAPI(light_prim);

  // Set light type

  if (m_prim.IsA<pxr::UsdLuxDiskLight>()) {
    blight->type = LA_AREA;
    blight->area_shape = LA_AREA_DISK;
    // Ellipse lights are not currently supported
  }
  else if (m_prim.IsA<pxr::UsdLuxRectLight>()) {
    blight->type = LA_AREA;
    blight->area_shape = LA_AREA_RECT;
  }
  else if (m_prim.IsA<pxr::UsdLuxSphereLight>()) {
    blight->type = LA_LOCAL;

    if (shapingAPI.GetShapingConeAngleAttr().IsAuthored()) {
      blight->type = LA_SPOT;
    }
  }
  else if (m_prim.IsA<pxr::UsdLuxDistantLight>()) {
    blight->type = LA_SUN;
  }

  // Set light values

  pxr::VtValue intensity;
  light_prim.GetIntensityAttr().Get(&intensity, motionSampleTime);

  blight->energy = intensity.Get<float>();

  // TODO: Not currently supported
  // pxr::VtValue exposure;
  // light_prim.GetExposureAttr().Get(&exposure, motionSampleTime);

  // TODO: Not currently supported
  // pxr::VtValue diffuse;
  // light_prim.GetDiffuseAttr().Get(&diffuse, motionSampleTime);

  pxr::VtValue specular;
  light_prim.GetSpecularAttr().Get(&specular, motionSampleTime);
  blight->spec_fac = specular.Get<float>();

  pxr::VtValue color;
  light_prim.GetColorAttr().Get(&color, motionSampleTime);
  pxr::GfVec3f color_vec = color.Get<pxr::GfVec3f>();
  blight->r = color_vec[0];
  blight->g = color_vec[1];
  blight->b = color_vec[2];

  // TODO: Not currently supported
  // pxr::VtValue use_color_temp;
  // light_prim.GetEnableColorTemperatureAttr().Get(&use_color_temp, motionSampleTime);

  // TODO: Not currently supported
  // pxr::VtValue color_temp;
  // light_prim.GetColorTemperatureAttr().Get(&color_temp, motionSampleTime);

  switch (blight->type) {
    case LA_AREA:
      if (blight->area_shape == LA_AREA_RECT && m_prim.IsA<pxr::UsdLuxRectLight>()) {
        pxr::UsdLuxRectLight rect_light = pxr::UsdLuxRectLight::Get(m_stage, m_prim.GetPath());

        pxr::VtValue width;
        rect_light.GetWidthAttr().Get(&width, motionSampleTime);

        pxr::VtValue height;
        rect_light.GetHeightAttr().Get(&height, motionSampleTime);

        blight->area_size = width.Get<float>();
        blight->area_sizey = height.Get<float>();
      }

      if (blight->area_shape == LA_AREA_DISK && m_prim.IsA<pxr::UsdLuxDiskLight>()) {
        pxr::UsdLuxDiskLight disk_light = pxr::UsdLuxDiskLight::Get(m_stage, m_prim.GetPath());

        pxr::VtValue radius;
        disk_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>() * 2.0f;
      }
      break;
    case LA_LOCAL:
      if (m_prim.IsA<pxr::UsdLuxSphereLight>()) {
        pxr::UsdLuxSphereLight sphere_light = pxr::UsdLuxSphereLight::Get(m_stage,
                                                                          m_prim.GetPath());

        pxr::VtValue radius;
        sphere_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>();
      }
      break;
    case LA_SPOT:
      if (m_prim.IsA<pxr::UsdLuxSphereLight>()) {
        pxr::UsdLuxSphereLight sphere_light = pxr::UsdLuxSphereLight::Get(m_stage,
                                                                          m_prim.GetPath());

        pxr::VtValue radius;
        sphere_light.GetRadiusAttr().Get(&radius, motionSampleTime);

        blight->area_size = radius.Get<float>();

        pxr::VtValue coneAngle;
        shapingAPI.GetShapingConeAngleAttr().Get(&coneAngle, motionSampleTime);
        blight->spotsize = coneAngle.Get<float>() * ((float)M_PI / 180.0f) * 2.0f;

        pxr::VtValue spotBlend;
        shapingAPI.GetShapingConeSoftnessAttr().Get(&spotBlend, motionSampleTime);
        blight->spotblend = spotBlend.Get<float>();
      }
      break;
    case LA_SUN:
      if (m_prim.IsA<pxr::UsdLuxDistantLight>()) {
        pxr::UsdLuxDistantLight distant_light = pxr::UsdLuxDistantLight::Get(m_stage,
                                                                             m_prim.GetPath());

        pxr::VtValue angle;
        distant_light.GetAngleAttr().Get(&angle, motionSampleTime);
        blight->sun_angle = angle.Get<float>();
      }
      break;
  }

  USDXformReader::readObjectData(bmain, motionSampleTime);
}