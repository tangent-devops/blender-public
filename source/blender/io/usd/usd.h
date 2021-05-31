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

#ifndef __USD_H__
#define __USD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "DEG_depsgraph.h"

struct Scene;
struct bContext;
struct Object;

typedef enum USD_global_forward_axis {
  USD_GLOBAL_FORWARD_X = 0,
  USD_GLOBAL_FORWARD_Y = 1,
  USD_GLOBAL_FORWARD_Z = 2,
  USD_GLOBAL_FORWARD_MINUS_X = 3,
  USD_GLOBAL_FORWARD_MINUS_Y = 4,
  USD_GLOBAL_FORWARD_MINUS_Z = 5
} USD_global_forward_axis;

typedef enum USD_global_up_axis {
  USD_GLOBAL_UP_X = 0,
  USD_GLOBAL_UP_Y = 1,
  USD_GLOBAL_UP_Z = 2,
  USD_GLOBAL_UP_MINUS_X = 3,
  USD_GLOBAL_UP_MINUS_Y = 4,
  USD_GLOBAL_UP_MINUS_Z = 5
} USD_global_up_axis;

static const USD_global_forward_axis USD_DEFAULT_FORWARD = USD_GLOBAL_FORWARD_MINUS_Z;
static const USD_global_up_axis USD_DEFAULT_UP = USD_GLOBAL_UP_Y;

struct USDExportParams {
  double frame_start;
  double frame_end;

  bool export_animation;
  bool export_hair;
  bool export_vertices;
  bool export_vertex_colors;
  bool export_vertex_groups;
  bool export_face_maps;
  bool export_uvmaps;
  bool export_normals;
  bool export_transforms;
  bool export_materials;
  bool export_animated_textures;
  double anim_tex_start;
  double anim_tex_end;
  bool export_meshes;
  bool export_lights;
  bool export_cameras;
  bool export_curves;
  bool export_particles;
  bool selected_objects_only;
  bool use_instancing;
  enum eEvaluationMode evaluation_mode;
  char *default_prim_path;   // USD Stage Default Primitive Path
  char *root_prim_path;      // Root path to encapsulate blender stage under. e.g. /shot
  char *material_prim_path;  // Prim path to store all generated USDShade, shaders under e.g.
                             // /materials
  bool generate_preview_surface;
  bool convert_uv_to_st;
  bool convert_orientation;
  enum USD_global_forward_axis forward_axis;
  enum USD_global_up_axis up_axis;
  bool apply_transforms;
  bool export_child_particles;
  bool export_as_overs;
  bool merge_transform_and_shape;
  bool export_custom_properties;
  bool export_identity_transforms;
  bool apply_subdiv;
  bool author_blender_name;
  bool vertex_data_as_face_varying;
  float frame_step;
  bool override_shutter;
  double shutter_open;
  double shutter_close;
};

/* The USD_export takes a as_background_job parameter, and returns a boolean.
 *
 * When as_background_job=true, returns false immediately after scheduling
 * a background job.
 *
 * When as_background_job=false, performs the export synchronously, and returns
 * true when the export was ok, and false if there were any errors.
 */

bool USD_export(struct bContext *C,
                const char *filepath,
                const struct USDExportParams *params,
                bool as_background_job);

int USD_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* __USD_H__ */
