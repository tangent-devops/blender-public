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

/** \file
 * \ingroup busd
 */

#ifndef __USD_READER_LIGHT_H__
#define __USD_READER_LIGHT_H__

#include "usd.h"
#include "usd_reader_xform.h"

class USDLightReader : public USDXformReader {

 public:
  USDLightReader(pxr::UsdStageRefPtr stage,
                 const pxr::UsdPrim &object,
                 const USDImportParams &import_params,
                 ImportSettings &settings)
      : USDXformReader(stage, object, import_params, settings)
  {
  }

  void createObject(Main *bmain, double motionSampleTime) override;
  void readObjectData(Main *bmain, double motionSampleTime) override;
};

#endif /* __USD_READER_LIGHT_H__ */
