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

#ifndef __USD_READER_MESH_H__
#define __USD_READER_MESH_H__

#include "pxr/usd/usdGeom/mesh.h"
#include "usd.h"
#include "usd_reader_geom.h"

typedef float f3Data[3];

class USDMeshReader : public USDGeomReader {

 public:
  USDMeshReader(pxr::UsdStageRefPtr stage,
                const pxr::UsdPrim &object,
                const USDImportParams &import_params,
                ImportSettings &settings);

  bool valid() const override;

  void createObject(Main *bmain, double motionSampleTime) override;
  void readObjectData(Main *bmain, double motionSampleTime) override;

  struct Mesh *read_mesh(struct Mesh *existing_mesh,
                         double motionSampleTime,
                         int read_flag,
                         float vel_scale,
                         const char **err_str);
  bool topology_changed(Mesh *existing_mesh, double motionSampleTime);

 private:
  void process_normals_vertex_varying(Mesh *mesh);
  void process_normals_face_varying(Mesh *mesh);
  void readFaceSetsSample(Main *bmain, Mesh *mesh, double motionSampleTime);
  void assign_facesets_to_mpoly(double motionSampleTime,
                                struct MPoly *mpoly,
                                int totpoly,
                                std::map<std::string, int> &r_mat_map);

  void read_mpolys(Mesh *mesh, pxr::UsdGeomMesh mesh_prim, double motionSampleTime);
  void read_uvs(Mesh *mesh,
                pxr::UsdGeomMesh mesh_prim,
                double motionSampleTime,
                bool load_uvs = false);
  void read_attributes(Mesh *mesh, pxr::UsdGeomMesh mesh_prim, double motionSampleTime);
  void read_vels(Mesh *mesh, pxr::UsdGeomMesh mesh_prim, float vel_scale, double motionSampleTime);

  void read_mesh_sample(const std::string &iobject_full_name,
                        ImportSettings *settings,
                        Mesh *mesh,
                        const pxr::UsdGeomMesh &mesh_prim,
                        double motionSampleTime,
                        bool new_mesh);

  pxr::UsdGeomMesh mesh_prim;

  std::unordered_map<std::string, pxr::TfToken> uv_token_map;
  std::map<const pxr::TfToken, bool> primvar_varying_map;

  pxr::VtIntArray m_face_indices;
  pxr::VtIntArray m_face_counts;
  pxr::VtVec3fArray m_positions;
  pxr::VtVec3fArray m_normals;
  pxr::TfToken m_normalInterpolation;
  pxr::TfToken m_orientation;
  bool m_isLeftHanded;

  int m_lastNumPositions;

  bool m_hasUVs;
  bool m_isTimeVarying;

  // This is to ensure we load all data once because we reuse the read_mesh function
  // in the mesh seq modifier, and in initial load. Ideally a better fix would be
  // implemented. Note this will break if face or positions vary...
  bool m_isInitialLoad;
};

#endif /* __USD_READER_MESH_H__ */
