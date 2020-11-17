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

#ifndef __USD_READER_OBJECT_H__
#define __USD_READER_OBJECT_H__

#include "usd.h"

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdUtils/sparseValueWriter.h>

#include <vector>

extern "C" {
#include "DNA_ID.h"
}

struct CacheFile;
struct Main;
struct Mesh;
struct Object;

struct ImportSettings {
  bool do_convert_mat;
  float conversion_mat[4][4];

  int from_up;
  int from_forward;
  float scale;
  bool is_sequence;
  bool set_frame_range;

  /* Length and frame offset of file sequences. */
  int sequence_len;
  int sequence_offset;

  /* From MeshSeqCacheModifierData.read_flag */
  int read_flag;

  bool validate_meshes;

  CacheFile *cache_file;

  float vel_scale;

  ImportSettings()
      : do_convert_mat(false),
        from_up(0),
        from_forward(0),
        scale(1.0f),
        is_sequence(false),
        set_frame_range(false),
        sequence_len(1),
        sequence_offset(0),
        read_flag(0),
        validate_meshes(false),
        cache_file(NULL),
        vel_scale(1.0f)
  {
  }
};

// Most generic USD Reader

class USDPrimReader {

 protected:
  std::string m_name;
  std::string m_prim_path;
  Object *m_object;
  pxr::UsdPrim m_prim;
  pxr::UsdStageRefPtr m_stage;
  const USDImportParams &m_import_params;
  USDPrimReader *m_parent_reader;

  ImportSettings *m_settings;

  int m_refcount;

 public:
  // USDPrimReader(pxr::UsdPrim* prim, ImportSettings &settings);
  USDPrimReader(pxr::UsdStageRefPtr stage,
                const pxr::UsdPrim &object,
                const USDImportParams &import_params,
                ImportSettings &settings);
  ~USDPrimReader();

  const pxr::UsdPrim &prim() const;

  virtual bool valid() const;

  virtual void createObject(Main *bmain, double motionSampleTime);
  virtual void readObjectData(Main *bmain, double motionSampleTime);

  Object *object() const;
  void object(Object *ob);

  USDPrimReader *parent() const
  {
    return m_parent_reader;
  }
  void parent(USDPrimReader *parent)
  {
    m_parent_reader = parent;
  }

  int refcount() const;
  void incref();
  void decref();

  virtual void addCacheModifier();

  const std::string &name() const
  {
    return m_name;
  }
  const std::string &prim_path() const
  {
    return m_prim_path;
  }
};

// template<typename Schema> static bool has_animations(Schema &schema, ImportSettings *settings)
// {
//   return settings->is_sequence || !schema.isConstant();
// }

// class USDObjectReader {
//  protected:
//   std::string m_name;
//   std::string m_object_name;
//   std::string m_data_name;
//   Object *m_object;
//   pxr::UsdPrim m_iobject;

//   ImportSettings *m_settings;

//   chrono_t m_min_time;
//   chrono_t m_max_time;

//   /* Use reference counting since the same reader may be used by multiple
//    * modifiers and/or constraints. */
//   int m_refcount;

//   bool m_inherits_xform;

//  public:
//   USDObjectReader *parent_reader;

//  public:
//   explicit USDObjectReader(const pxr::UsdPrim &object, ImportSettings &settings);

//   virtual ~USDObjectReader();

//   const Alembic::USD::IObject &iobject() const;

//   typedef std::vector<USDObjectReader *> ptr_vector;

//   /**
//    * Returns the transform of this object. This can be the Alembic object
//    * itself (in case of an Empty) or it can be the parent Alembic object.
//    */
//   virtual Alembic::USDGeom::IXform xform();

//   Object *object() const;
//   void object(Object *ob);

//   const std::string &name() const
//   {
//     return m_name;
//   }
//   const std::string &object_name() const
//   {
//     return m_object_name;
//   }
//   const std::string &data_name() const
//   {
//     return m_data_name;
//   }
//   bool inherits_xform() const
//   {
//     return m_inherits_xform;
//   }

//   virtual bool valid() const = 0;
//   virtual bool accepts_object_type(const Alembic::USDCoreAbstract::ObjectHeader &alembic_header,
//                                    const Object *const ob,
//                                    const char **err_str) const = 0;

//   virtual void readObjectData(Main *bmain, const Alembic::USD::ISampleSelector &sample_sel) = 0;

//   virtual struct Mesh *read_mesh(struct Mesh *mesh,
//                                  const Alembic::USD::ISampleSelector &sample_sel,
//                                  int read_flag,
//                                  const char **err_str);
//   virtual bool topology_changed(Mesh *existing_mesh,
//                                 const Alembic::USD::ISampleSelector &sample_sel);

//   /** Reads the object matrix and sets up an object transform if animated. */
//   void setupObjectTransform(const float time);

//   void addCacheModifier();

//   chrono_t minTime() const;
//   chrono_t maxTime() const;

//   int refcount() const;
//   void incref();
//   void decref();

//   void read_matrix(float r_mat[4][4], const float time, const float scale, bool &is_constant);

//  protected:
//   void determine_inherits_xform();
// };

// Imath::M44d get_matrix(const Alembic::USDGeom::IXformSchema &schema, const float time);

#endif /* __USD_READER_OBJECT_H__ */
