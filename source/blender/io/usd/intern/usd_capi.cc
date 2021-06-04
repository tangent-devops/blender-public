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

#include "usd.h"
#include "usd_hierarchy_iterator.h"

#include "usd_writer_material.h"

#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#include "MEM_guardedalloc.h"

extern "C" {
#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "DNA_cachefile_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "BKE_blender_version.h"
#include "BKE_cachefile.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BKE_world.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "WM_api.h"
#include "WM_types.h"
}

#include "usd_util.h"

namespace USD {
/* ********************** Export file ********************** */

struct ExportJobData {
  ViewLayer *view_layer;
  Main *bmain;
  Depsgraph *depsgraph;
  wmWindowManager *wm;

  char filename[FILE_MAX];
  USDExportParams params;

  short *stop;
  short *do_update;
  float *progress;

  bool was_canceled;
  bool export_ok;
};

static void export_startjob(void *customdata, short *stop, short *do_update, float *progress)
{
  ExportJobData *data = static_cast<ExportJobData *>(customdata);

  data->stop = stop;
  data->do_update = do_update;
  data->progress = progress;
  data->was_canceled = false;
  data->export_ok = true;

  G.is_rendering = true;
  WM_set_locked_interface(data->wm, true);
  G.is_break = false;

  // Construct the depsgraph for exporting.
  Scene *scene = DEG_get_input_scene(data->depsgraph);
  ViewLayer *view_layer = DEG_get_input_view_layer(data->depsgraph);
  DEG_graph_build_from_view_layer(data->depsgraph, data->bmain, scene, view_layer);
  BKE_scene_graph_update_tagged(data->depsgraph, data->bmain);

  *progress = 0.0f;
  *do_update = true;

  // For restoring the current frame after exporting animation is done.
  const int orig_frame = CFRA;

  if (!BLI_path_extension_check_glob(data->filename, "*.usd;*.usda;*.usdc"))
    BLI_path_extension_ensure(data->filename, FILE_MAX, ".usd");

  pxr::UsdStageRefPtr usd_stage = pxr::UsdStage::CreateNew(data->filename);
  if (!usd_stage) {
    /* This happens when the USD JSON files cannot be found. When that happens,
     * the USD library doesn't know it has the functionality to write USDA and
     * USDC files, and creating a new UsdStage fails. */
    WM_reportf(
        RPT_ERROR, "USD Export: unable to find suitable USD plugin to write %s", data->filename);
    data->export_ok = false;
    return;
  }

  // Handle World Surface (Environment Lights)
  // TODO: This should live somewhere else. But cannot live in usd_writer_light as it stands
  //      Few assumptions:
  //        - primpath (/lights/environment)
  //        - transform
  //        - no blender specific nodes used
  if (data->params.export_lights && scene && scene->world && scene->world->use_nodes &&
      !data->params.selected_objects_only) {

    float world_color[3];
    float world_intensity = 0.0f;

    char filepath[1024] = "\0";

    bool background_found = false;
    bool env_tex_found = false;
    pxr::SdfPath environment_light_path(std::string(data->params.root_prim_path) +
                                        "/lights/environment");

    // Store Light node tree
    pxr::UsdShadeMaterial world_mat = pxr::UsdShadeMaterial::Define(
        usd_stage, environment_light_path.AppendChild(pxr::TfToken("world_material")));
    create_usd_cycles_material(
        usd_stage, scene->world->nodetree, world_mat, data->params.export_as_overs,
        data->params.export_animated_textures, data->params.anim_tex_start,
        data->params.anim_tex_end, CFRA);

    // Convert node graph to USD Dome Light

    for (bNode *node = (bNode *)scene->world->nodetree->nodes.first; node; node = node->next) {

      // Get light intensity
      if (ELEM(node->type, SH_NODE_BACKGROUND)) {

        bNodeSocketValueRGBA *color_data =
            (bNodeSocketValueRGBA *)((bNodeSocket *)BLI_findlink(&node->inputs, 0))->default_value;
        bNodeSocketValueFloat *strength_data =
            (bNodeSocketValueFloat *)((bNodeSocket *)BLI_findlink(&node->inputs, 1))
                ->default_value;

        background_found = true;
        world_intensity = strength_data->value;
        world_color[0] = color_data->value[0];
        world_color[1] = color_data->value[1];
        world_color[2] = color_data->value[2];
      }

      // Get env tex path
      if (ELEM(node->type, SH_NODE_TEX_ENVIRONMENT)) {
        Image *ima = (Image *)node->id;

        STRNCPY(filepath, ima->name);
        BLI_path_abs(filepath, BKE_main_blendfile_path_from_global());

        BLI_str_replace_char(filepath, '\\', '/');

        env_tex_found = true;
      }
    }

    // Create USDLux light
    if (background_found) {

      pxr::UsdLuxDomeLight dome_light = pxr::UsdLuxDomeLight::Define(usd_stage,
                                                                     environment_light_path);

      pxr::UsdShadeMaterialBindingAPI api = pxr::UsdShadeMaterialBindingAPI(dome_light.GetPrim());
      api.Bind(world_mat);

      // TODO(bjs): Should be handled more correctly
      if (data->params.convert_orientation)
        pxr::UsdGeomXformCommonAPI(dome_light).SetRotate(pxr::GfVec3f(0.0f, 90.0f, 0.0f));
      else
        pxr::UsdGeomXformCommonAPI(dome_light);  //.SetRotate(pxr::GfVec3f(-90.0f, 0.0f, 90.0f));

      if (env_tex_found)
        dome_light.CreateTextureFileAttr().Set(pxr::SdfAssetPath(filepath));
      else
        dome_light.CreateColorAttr().Set(
            pxr::VtValue(pxr::GfVec3f(world_color[0], world_color[1], world_color[2])));
      dome_light.CreateIntensityAttr().Set(pxr::VtValue(world_intensity));
    }
  }

  // Define material prim path as a scope
  if (data->params.export_materials)
    USD::usd_define_or_over<pxr::UsdGeomScope>(
        usd_stage, pxr::SdfPath(data->params.material_prim_path), data->params.export_as_overs);

  pxr::VtValue upAxis = pxr::VtValue(pxr::UsdGeomTokens->z);
  if (data->params.convert_orientation) {
    if (data->params.up_axis == USD_GLOBAL_UP_X)
      upAxis = pxr::VtValue(pxr::UsdGeomTokens->x);
    else if (data->params.up_axis == USD_GLOBAL_UP_Y)
      upAxis = pxr::VtValue(pxr::UsdGeomTokens->y);
  }

  usd_stage->SetMetadata(pxr::UsdGeomTokens->upAxis, upAxis);
  usd_stage->SetMetadata(pxr::UsdGeomTokens->metersPerUnit,
                         pxr::VtValue(scene->unit.scale_length));
  usd_stage->GetRootLayer()->SetDocumentation(std::string("Blender ") +
                                              BKE_blender_version_string());

  // Set up the stage for animated data.
  if (data->params.export_animation) {
    usd_stage->SetTimeCodesPerSecond(FPS);
    usd_stage->SetStartTimeCode(data->params.frame_start);
    usd_stage->SetEndTimeCode(data->params.frame_end);
  }

  // Create root prim if defined
  if (strlen(data->params.root_prim_path) > 0) {
    usd_stage->DefinePrim(pxr::SdfPath(data->params.root_prim_path), pxr::TfToken("Xform"));
  }

  USDHierarchyIterator iter(data->depsgraph, usd_stage, data->params);

  try {
    if (data->params.export_animation) {
      // Writing the animated frames is not 100% of the work, but it's our best guess.
      float progress_per_frame = 1.0f / std::max(1.0f,
                                                 (float) (data->params.frame_end -
                                                          data->params.frame_start + 1.0) /
                                                 data->params.frame_step);

      for (float frame = data->params.frame_start; frame <= data->params.frame_end;
           frame += data->params.frame_step) {
        if (G.is_break || (stop != nullptr && *stop)) {
          break;
        }

        // Update the scene for the next frame to render.
        scene->r.cfra = static_cast<int>(frame);
        scene->r.subframe = frame - scene->r.cfra;
        BKE_scene_graph_update_for_newframe(data->depsgraph, data->bmain);

        iter.set_export_frame(frame);
        iter.iterate_and_write();

        *progress += progress_per_frame;
        *do_update = true;
      }
    } else {
      // If we're not animating, a single iteration over all objects is enough.
      iter.iterate_and_write();
    }
  }
  catch(...) {
    WM_reportf(RPT_ERROR, "USD Export: An error was encountered. Export aborted.");
    data->export_ok = false;
  }

  iter.release_writers();

  if (!data->export_ok) {
    return;
  }

  // Set Stage Default Prim Path
  if (strlen(data->params.default_prim_path) > 0) {
    std::string valid_default_prim_path = pxr::TfMakeValidIdentifier(
        data->params.default_prim_path);

    if (valid_default_prim_path[0] == '_') {
      valid_default_prim_path[0] = '/';
    }
    if (valid_default_prim_path[0] != '/') {
      valid_default_prim_path = "/" + valid_default_prim_path;
    }

    pxr::UsdPrim defaultPrim = usd_stage->GetPrimAtPath(pxr::SdfPath(valid_default_prim_path));

    if (defaultPrim.IsValid()) {
      WM_reportf(RPT_INFO, "Set default prim path: %s", valid_default_prim_path);
      usd_stage->SetDefaultPrim(defaultPrim);
    }
  }

  // Set Scale
  pxr::UsdGeomSetStageMetersPerUnit(usd_stage, pxr::UsdGeomLinearUnits::meters);

  usd_stage->GetRootLayer()->Save();

  // Finish up by going back to the keyframe that was current before we started.
  if (CFRA != orig_frame) {
    CFRA = orig_frame;
    BKE_scene_graph_update_for_newframe(data->depsgraph, data->bmain);
  }

  data->export_ok = !data->was_canceled;

  *progress = 1.0f;
  *do_update = true;
}

static void export_endjob(void *customdata)
{
  ExportJobData *data = static_cast<ExportJobData *>(customdata);

  DEG_graph_free(data->depsgraph);

  MEM_freeN(data->params.default_prim_path);
  MEM_freeN(data->params.root_prim_path);
  MEM_freeN(data->params.material_prim_path);

  if (data->was_canceled && BLI_exists(data->filename)) {
    BLI_delete(data->filename, false, false);
  }

  G.is_rendering = false;
  WM_set_locked_interface(data->wm, false);
}

}  // namespace USD

bool USD_export(bContext *C,
                const char *filepath,
                const USDExportParams *params,
                bool as_background_job)
{
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Scene *scene = CTX_data_scene(C);

  USD::ExportJobData *job = static_cast<USD::ExportJobData *>(
      MEM_mallocN(sizeof(USD::ExportJobData), "ExportJobData"));

  job->bmain = CTX_data_main(C);
  job->wm = CTX_wm_manager(C);
  job->export_ok = false;
  BLI_strncpy(job->filename, filepath, sizeof(job->filename));

  job->depsgraph = DEG_graph_new(job->bmain, scene, view_layer, params->evaluation_mode);
  job->params = *params;

  bool export_ok = false;
  if (as_background_job) {
    wmJob *wm_job = WM_jobs_get(
        job->wm, CTX_wm_window(C), scene, "USD Export", WM_JOB_PROGRESS, WM_JOB_TYPE_ALEMBIC);

    /* setup job */
    WM_jobs_customdata_set(wm_job, job, MEM_freeN);
    WM_jobs_timer(wm_job, 0.1, NC_SCENE | ND_FRAME, NC_SCENE | ND_FRAME);
    WM_jobs_callbacks(wm_job, USD::export_startjob, NULL, NULL, USD::export_endjob);

    WM_jobs_start(CTX_wm_manager(C), wm_job);
  }
  else {
    /* Fake a job context, so that we don't need NULL pointer checks while exporting. */
    short stop = 0, do_update = 0;
    float progress = 0.f;

    USD::export_startjob(job, &stop, &do_update, &progress);
    USD::export_endjob(job);
    export_ok = job->export_ok;

    MEM_freeN(job);
  }

  return export_ok;
}

int USD_get_version(void)
{
  /* USD 19.11 defines:
   *
   * #define PXR_MAJOR_VERSION 0
   * #define PXR_MINOR_VERSION 19
   * #define PXR_PATCH_VERSION 11
   * #define PXR_VERSION 1911
   *
   * So the major version is implicit/invisible in the public version number.
   */
  return PXR_VERSION;
}
