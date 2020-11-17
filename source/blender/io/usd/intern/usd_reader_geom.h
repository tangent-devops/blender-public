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

#ifndef __USD_READER_GEOM_H__
#define __USD_READER_GEOM_H__

#include "usd.h"
#include "usd_reader_xform.h"

struct Mesh;

class USDGeomReader : public USDXformReader {

 public:
  USDGeomReader(pxr::UsdStageRefPtr stage,
                const pxr::UsdPrim &object,
                const USDImportParams &import_params,
                ImportSettings &settings)
      : USDXformReader(stage, object, import_params, settings)
  {
  }

  bool valid() const override;

  virtual void createObject(Main *bmain, double motionSampleTime) override;
  virtual void readObjectData(Main *bmain, double motionSampleTime) override;

  virtual Mesh *read_mesh(struct Mesh *existing_mesh,
                          double motionSampleTime,
                          int read_flag,
                          float vel_scale,
                          const char **err_str);

  void addCacheModifier() override;
  void addSubdivModifier();

  bool topology_changed(Mesh *existing_mesh, double motionSampleTime);
};

#endif /* __USD_READER_GEOM_H__ */
