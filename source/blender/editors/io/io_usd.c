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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editor/io
 */

#ifdef WITH_USD
#  include <string.h>
#  include "DNA_space_types.h"
#  include "DNA_modifier_types.h"

#  include "BKE_context.h"
#  include "BKE_main.h"
#  include "BKE_report.h"

#  include "BLI_blenlib.h"
#  include "BLI_path_util.h"
#  include "BLI_string.h"
#  include "BLI_utildefines.h"

#  include "BLT_translation.h"

#  include "ED_object.h"

#  include "MEM_guardedalloc.h"

#  include "RNA_access.h"
#  include "RNA_define.h"

#  include "RNA_enum_types.h"

#  include "UI_interface.h"
#  include "UI_resources.h"

#  include "WM_api.h"
#  include "WM_types.h"

#  include "DEG_depsgraph.h"

#  include "io_usd.h"
#  include "usd.h"

#  include "stdio.h"

const EnumPropertyItem rna_enum_usd_export_evaluation_mode_items[] = {
    {DAG_EVAL_RENDER,
     "RENDER",
     0,
     "Render",
     "Use Render settings for object visibility, modifier settings, etc"},
    {DAG_EVAL_VIEWPORT,
     "VIEWPORT",
     0,
     "Viewport",
     "Use Viewport settings for object visibility, modifier settings, etc"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_usd_import_read_flags[] = {
    {MOD_MESHSEQ_READ_VERT, "VERT", 0, "Vertex", ""},
    {MOD_MESHSEQ_READ_POLY, "POLY", 0, "Faces", ""},
    {MOD_MESHSEQ_READ_UV, "UV", 0, "UV", ""},
    {MOD_MESHSEQ_READ_COLOR, "COLOR", 0, "Color", ""},
    {MOD_MESHSEQ_READ_ATTR, "ATTR", 0, "Attributes", ""},
    {MOD_MESHSEQ_READ_VELS, "VELS", 0, "Velocities", ""},
    {0, NULL, 0, NULL, NULL},
};

/* Stored in the wmOperator's customdata field to indicate it should run as a background job.
 * This is set when the operator is invoked, and not set when it is only executed. */
enum { AS_BACKGROUND_JOB = 1 };
typedef struct eUSDOperatorOptions {
  bool as_background_job;
} eUSDOperatorOptions;

static int wm_usd_export_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  eUSDOperatorOptions *options = MEM_callocN(sizeof(eUSDOperatorOptions), "eUSDOperatorOptions");
  options->as_background_job = true;
  op->customdata = options;

  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    Main *bmain = CTX_data_main(C);
    char filepath[FILE_MAX];
    const char *main_blendfile_path = BKE_main_blendfile_path(bmain);

    if (main_blendfile_path[0] == '\0') {
      BLI_strncpy(filepath, "untitled", sizeof(filepath));
    }
    else {
      BLI_strncpy(filepath, main_blendfile_path, sizeof(filepath));
    }

    BLI_path_extension_replace(filepath, sizeof(filepath), ".usdc");
    RNA_string_set(op->ptr, "filepath", filepath);
  }

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int wm_usd_export_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  eUSDOperatorOptions *options = (eUSDOperatorOptions *)op->customdata;
  const bool as_background_job = (options != NULL && options->as_background_job);
  MEM_SAFE_FREE(op->customdata);

  const bool selected_objects_only = RNA_boolean_get(op->ptr, "selected_objects_only");
  const bool export_animation = RNA_boolean_get(op->ptr, "export_animation");
  const bool export_hair = RNA_boolean_get(op->ptr, "export_hair");
  const bool export_uvmaps = RNA_boolean_get(op->ptr, "export_uvmaps");
  const bool export_normals = RNA_boolean_get(op->ptr, "export_normals");
  const bool export_materials = RNA_boolean_get(op->ptr, "export_materials");
  const bool use_instancing = RNA_boolean_get(op->ptr, "use_instancing");
  const bool evaluation_mode = RNA_enum_get(op->ptr, "evaluation_mode");

  struct USDExportParams params = {
      export_animation,
      export_hair,
      export_uvmaps,
      export_normals,
      export_materials,
      selected_objects_only,
      use_instancing,
      evaluation_mode,
  };

  bool ok = USD_export(C, filename, &params, as_background_job);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_export_draw(bContext *UNUSED(C), wmOperator *op)
{
  uiLayout *layout = op->layout;
  uiLayout *col;
  struct PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, true);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "selected_objects_only", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "export_animation", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_hair", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_uvmaps", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_normals", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_materials", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(layout, true);
  uiItemR(col, ptr, "evaluation_mode", 0, NULL, ICON_NONE);

  uiLayout *box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Experimental:"), ICON_NONE);
  uiItemR(box, ptr, "use_instancing", 0, NULL, ICON_NONE);
}

void WM_OT_usd_export(struct wmOperatorType *ot)
{
  ot->name = "Export USD";
  ot->description = "Export current scene in a USD archive";
  ot->idname = "WM_OT_usd_export";

  ot->invoke = wm_usd_export_invoke;
  ot->exec = wm_usd_export_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_export_draw;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_ALPHA);

  RNA_def_boolean(ot->srna,
                  "selected_objects_only",
                  false,
                  "Selection Only",
                  "Only selected objects are exported. Unselected parents of selected objects are "
                  "exported as empty transform");

  RNA_def_boolean(ot->srna,
                  "export_animation",
                  false,
                  "Animation",
                  "When checked, the render frame range is exported. When false, only the current "
                  "frame is exported");
  RNA_def_boolean(
      ot->srna, "export_hair", false, "Hair", "When checked, hair is exported as USD curves");
  RNA_def_boolean(ot->srna,
                  "export_uvmaps",
                  true,
                  "UV Maps",
                  "When checked, all UV maps of exported meshes are included in the export");
  RNA_def_boolean(ot->srna,
                  "export_normals",
                  true,
                  "Normals",
                  "When checked, normals of exported meshes are included in the export");
  RNA_def_boolean(ot->srna,
                  "export_materials",
                  true,
                  "Materials",
                  "When checked, the viewport settings of materials are exported as USD preview "
                  "materials, and material assignments are exported as geometry subsets");

  RNA_def_boolean(ot->srna,
                  "use_instancing",
                  false,
                  "Instancing",
                  "When checked, instanced objects are exported as references in USD. "
                  "When unchecked, instanced objects are exported as real objects");

  RNA_def_enum(ot->srna,
               "evaluation_mode",
               rna_enum_usd_export_evaluation_mode_items,
               DAG_EVAL_RENDER,
               "Use Settings for",
               "Determines visibility of objects, modifier settings, and other areas where there "
               "are different settings for viewport and rendering");
}

/* ====== USD Import ====== */

static int wm_usd_import_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  if (!RNA_struct_property_is_set(op->ptr, "as_background_job")) {
    RNA_boolean_set(op->ptr, "as_background_job", true);
  }
  return WM_operator_filesel(C, op, event);
}

static int wm_usd_import_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  const float scale = RNA_float_get(op->ptr, "scale");
  const float vel_scale = RNA_float_get(op->ptr, "vel_scale");
  const bool is_sequence = RNA_boolean_get(op->ptr, "is_sequence");
  const bool set_frame_range = RNA_boolean_get(op->ptr, "set_frame_range");
  const bool validate_meshes = RNA_boolean_get(op->ptr, "validate_meshes");
  const bool as_background_job = RNA_boolean_get(op->ptr, "as_background_job");
  const char global_read_flag = RNA_enum_get(op->ptr, "global_read_flag");

  const bool import_cameras = RNA_boolean_get(op->ptr, "import_cameras");
  const bool import_curves = RNA_boolean_get(op->ptr, "import_curves");
  const bool import_lights = RNA_boolean_get(op->ptr, "import_lights");
  const bool import_materials = RNA_boolean_get(op->ptr, "import_materials");
  const bool import_meshes = RNA_boolean_get(op->ptr, "import_meshes");
  const bool import_volumes = RNA_boolean_get(op->ptr, "import_volumes");

  const bool import_subdiv = RNA_boolean_get(op->ptr, "import_subdiv");

  const bool create_collection = RNA_boolean_get(op->ptr, "create_collection");

  char *prim_path_mask = malloc(1024);
  RNA_string_get(op->ptr, "prim_path_mask", prim_path_mask);

  int offset = 0;
  int sequence_len = 1;

  if (is_sequence) {
    // @TODO: Not Implemented
    /*sequence_len = get_sequence_len(filename, &offset);
    if (sequence_len < 0) {
      BKE_report(op->reports, RPT_ERROR, "Unable to determine ABC sequence length");
      return OPERATOR_CANCELLED;
    }*/
  }

  /* Switch out of edit mode to avoid being stuck in it (T54326). */
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    ED_object_mode_toggle(C, OB_MODE_EDIT);
  }

  struct USDImportParams params = {
      scale,
      vel_scale,
      is_sequence,
      set_frame_range,
      sequence_len,
      offset,
      validate_meshes,
      global_read_flag,
      import_cameras,
      import_curves,
      import_lights,
      import_materials,
      import_meshes,
      import_volumes,
      prim_path_mask,
      import_subdiv,
      create_collection,
  };

  bool ok = USD_import(C, filename, &params, as_background_job);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_import_draw(bContext *UNUSED(C), wmOperator *op)
{
  uiLayout *layout = op->layout;
  struct PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, false);

  uiLayout *box = uiLayoutBox(layout);
  uiLayout *row = uiLayoutRow(box, false);

  uiItemL(box, IFACE_("USD Import"), ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Global Read Flag:"), ICON_NONE);
  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "global_read_flag", UI_ITEM_R_EXPAND, NULL, ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Manual Transform:"), ICON_NONE);
  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "scale", 0, NULL, ICON_NONE);
  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "vel_scale", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Options:"), ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "relative_path", 0, NULL, ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "set_frame_range", 0, NULL, ICON_NONE);

  // TODO: Not supported
  // row = uiLayoutRow(box, false);
  // uiItemR(row, ptr, "is_sequence", 0, NULL, ICON_NONE);

  // row = uiLayoutRow(box, false);
  // uiItemR(row, ptr, "validate_meshes", 0, NULL, ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "import_subdiv", 0, NULL, ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, ptr, "create_collection", 0, NULL, ICON_NONE);

  // row = uiLayoutRow(box, false);
  // uiItemR(row, ptr, "prim_path_mask", 0, NULL, ICON_NONE);

  uiLayout *prim_path_mask_box = uiLayoutBox(box);
  row = uiLayoutRow(prim_path_mask_box, false);
  uiItemL(row, IFACE_("Prim Path Mask:"), ICON_NONE);

  row = uiLayoutRow(prim_path_mask_box, false);
  uiItemR(row, ptr, "prim_path_mask", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Primitive Types:"), ICON_OBJECT_DATA);
  uiItemR(box, ptr, "import_cameras", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_curves", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_lights", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_materials", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_meshes", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_volumes", 0, NULL, ICON_NONE);
}

void WM_OT_usd_import(struct wmOperatorType *ot)
{
  PropertyRNA *prop;

  ot->name = "Import USD";
  ot->description = "Import USD stage into current scene";
  ot->idname = "WM_OT_usd_import";

  ot->invoke = wm_usd_import_invoke;
  ot->exec = wm_usd_import_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_import_draw;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_ALPHA);

  RNA_def_float(
      ot->srna,
      "scale",
      1.0f,
      0.0001f,
      1000.0f,
      "Scale",
      "Value by which to enlarge or shrink the objects with respect to the world's origin",
      0.0001f,
      1000.0f);
  RNA_def_float(ot->srna,
                "vel_scale",
                1.0f,
                0.0001f,
                1000.0f,
                "Velocity Scale",
                "Amount to scale velocity",
                0.0001f,
                1000.0f);

  RNA_def_boolean(
      ot->srna,
      "set_frame_range",
      true,
      "Set Frame Range",
      "If checked, update scene's start and end frame to match those of the USD archive");

  RNA_def_boolean(ot->srna,
                  "validate_meshes",
                  0,
                  "Validate Meshes",
                  "Check imported mesh objects for invalid data (slow)");

  RNA_def_boolean(ot->srna,
                  "is_sequence",
                  false,
                  "Is Sequence",
                  "Only set to true if the cache is split into separate files. (UNSUPPORTED)");

  RNA_def_boolean(
      ot->srna,
      "as_background_job",
      false,
      "Run as Background Job",
      "Enable this to run the export in the background, disable to block Blender while exporting. "
      "This option is deprecated; EXECUTE this operator to run in the foreground, and INVOKE it "
      "to run as a background job");

  RNA_def_boolean(
      ot->srna, "import_cameras", true, "Cameras", "When checked, all cameras will be imported");
  RNA_def_boolean(
      ot->srna, "import_curves", true, "Curves", "When checked, all curves will be imported");
  RNA_def_boolean(
      ot->srna, "import_lights", true, "Lights", "When checked, all lights will be imported");
  RNA_def_boolean(ot->srna,
                  "import_materials",
                  true,
                  "Materials",
                  "When checked, all materials will be imported");
  RNA_def_boolean(
      ot->srna, "import_meshes", true, "Meshes", "When checked, all meshes will be imported");
  RNA_def_boolean(ot->srna,
                  "import_volumes",
                  true,
                  "Volumes",
                  "(Tangent Specific) When checked, all volumes will be imported");

  RNA_def_boolean(ot->srna,
                  "import_subdiv",
                  true,
                  "Import Subdiv Scheme",
                  "If enabled, subdiv surface modifiers will be created based on USD "
                  "SubdivisionScheme attribute");

  RNA_def_boolean(ot->srna,
                  "create_collection",
                  false,
                  "Create Collection",
                  "If enabled, all import objects will be added to a new collection");

  prop = RNA_def_enum(ot->srna,
                      "global_read_flag",
                      rna_enum_usd_import_read_flags,
                      0,
                      "Flags",
                      "Set read flag for all usd import mesh sequence cache modifiers");

  RNA_def_property_flag(prop, PROP_ENUM_FLAG);
  RNA_def_property_enum_default(prop, MOD_MESHSEQ_READ_ALL);

  RNA_def_string(ot->srna,
                 "prim_path_mask",
                 NULL,
                 1024,
                 "",
                 "If set, this will specify a specific primitive from the usd stage");
}

#endif /* WITH_USD */
