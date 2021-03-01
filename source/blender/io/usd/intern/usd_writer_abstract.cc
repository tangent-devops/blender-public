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
#include "usd_writer_abstract.h"
#include "usd_hierarchy_iterator.h"
#include "usd_writer_material.h"

#include "usd_util.h"

#include <pxr/base/tf/stringUtils.h>

extern "C" {
#include "BKE_anim_data.h"
#include "BKE_key.h"

#include "BLI_utildefines.h"

#include "DNA_scene_types.h"
#include "DEG_depsgraph_query.h"
#include "DNA_modifier_types.h"
}


/* TfToken objects are not cheap to construct, so we do it once. */
namespace usdtokens {
// Materials
static const pxr::TfToken diffuse_color("diffuseColor", pxr::TfToken::Immortal);
static const pxr::TfToken metallic("metallic", pxr::TfToken::Immortal);
static const pxr::TfToken preview_shader("previewShader", pxr::TfToken::Immortal);
static const pxr::TfToken preview_surface("UsdPreviewSurface", pxr::TfToken::Immortal);
static const pxr::TfToken roughness("roughness", pxr::TfToken::Immortal);
static const pxr::TfToken surface("surface", pxr::TfToken::Immortal);
static const pxr::TfToken blenderName("userProperties:blenderName", pxr::TfToken::Immortal);
}  // namespace usdtokens

namespace USD {

USDAbstractWriter::USDAbstractWriter(const USDExporterContext &usd_export_context)
    : usd_export_context_(usd_export_context),
      usd_value_writer_(),
      frame_has_been_written_(false),
      is_animated_(false)
{
}

USDAbstractWriter::~USDAbstractWriter()
{
}

bool USDAbstractWriter::is_supported(const HierarchyContext * /*context*/) const
{
  return true;
}

pxr::UsdTimeCode USDAbstractWriter::get_export_time_code() const
{
  if (is_animated_) {
    return usd_export_context_.hierarchy_iterator->get_export_time_code();
  }
  // By using the default timecode USD won't even write a single `timeSample` for non-animated
  // data. Instead, it writes it as non-timesampled.
  static pxr::UsdTimeCode default_timecode = pxr::UsdTimeCode::Default();
  return default_timecode;
}

void USDAbstractWriter::write(HierarchyContext &context)
{
  if (!frame_has_been_written_) {
    is_animated_ = usd_export_context_.export_params.export_animation &&
                   check_is_animated(context);
  }
  else if (!is_animated_) {
    /* A frame has already been written, and without animation one frame is enough. */
    return;
  }

  do_write(context);

  frame_has_been_written_ = true;
}

bool USDAbstractWriter::check_is_animated(const HierarchyContext &context) const
{
  const Object *object = context.object;

  if (BKE_animdata_id_is_animated(static_cast<ID *>(object->data))) {
    return true;
  }
  if (BKE_key_from_object(object) != nullptr) {
    return true;
  }

  /* Test modifiers. */
  /* TODO(Sybren): replace this with a check on the depsgraph to properly check for dependency on
   * time. */
  ModifierData *md = static_cast<ModifierData *>(object->modifiers.first);
  while (md) {
    if (md->type != eModifierType_Subsurf) {
      return true;
    }
    md = md->next;
  }

  return false;
}

const pxr::SdfPath &USDAbstractWriter::usd_path() const
{
  return usd_export_context_.usd_path;
}

pxr::UsdShadeMaterial USDAbstractWriter::ensure_usd_material(Material *material)
{
  pxr::SdfPath material_library_path(this->usd_export_context_.export_params.material_prim_path);
  pxr::UsdStageRefPtr stage = usd_export_context_.stage;

  // Construct the material.
  pxr::TfToken material_name(usd_export_context_.hierarchy_iterator->get_id_name(&material->id));
  pxr::SdfPath usd_path = material_library_path.AppendChild(material_name);
  pxr::UsdShadeMaterial usd_material = pxr::UsdShadeMaterial::Get(stage, usd_path);
  if (usd_material) {
    return usd_material;
  }
  usd_material = (usd_export_context_.export_params.export_as_overs) ?
                     pxr::UsdShadeMaterial(usd_export_context_.stage->OverridePrim(usd_path)) :
                     pxr::UsdShadeMaterial::Define(usd_export_context_.stage, usd_path);

  // TODO(bskinner) maybe always export viewport material as variant...
  const Scene *scene = DEG_get_evaluated_scene(this->usd_export_context_.depsgraph);
  if (material->use_nodes) {
    create_usd_cycles_material(this->usd_export_context_.stage, material, usd_material,
                               this->usd_export_context_.export_params.export_as_overs,
                               this->usd_export_context_.export_params.export_animated_textures,
                               this->usd_export_context_.export_params.anim_tex_start,
                               this->usd_export_context_.export_params.anim_tex_end,
                               scene->r.cfra);
  }
  if (material->use_nodes && this->usd_export_context_.export_params.generate_preview_surface) {
    create_usd_preview_surface_material(this->usd_export_context_, material, usd_material,
                               this->usd_export_context_.export_params.export_animated_textures,
                               this->usd_export_context_.export_params.anim_tex_start,
                               this->usd_export_context_.export_params.anim_tex_end,
                               scene->r.cfra);
  }
  else {
    create_usd_viewport_material(this->usd_export_context_, material, usd_material);
  }

  if (usd_export_context_.export_params.export_custom_properties && material) {
    auto prim = usd_material.GetPrim();
    write_id_properties(prim, material->id, get_export_time_code());
  }

  return usd_material;
}

void USDAbstractWriter::write_id_properties(pxr::UsdPrim &prim,
                                            const ID &id,
                                            pxr::UsdTimeCode timecode)
{
  if (usd_export_context_.export_params.author_blender_name) {
    if (GS(id.name) == ID_OB) {
      // Author property of original blenderName
      prim.CreateAttribute(pxr::TfToken(usdtokens::blenderName.GetString() + ":object"),
                           pxr::SdfValueTypeNames->String,
                           true)
          .Set<std::string>(std::string(id.name + 2));
    }
    else {
      prim.CreateAttribute(pxr::TfToken(usdtokens::blenderName.GetString() + ":data"),
                           pxr::SdfValueTypeNames->String,
                           true)
          .Set<std::string>(std::string(id.name + 2));
    }
  }

  if (id.properties)
    write_user_properties(prim, (IDProperty *)id.properties, timecode);
}

void USDAbstractWriter::write_user_properties(pxr::UsdPrim &prim,
                                              IDProperty *properties,
                                              pxr::UsdTimeCode timecode)
{
  if (properties == nullptr)
    return;
  if (properties->type != IDP_GROUP)
    return;

  IDProperty *prop;
  for (prop = (IDProperty *)properties->data.group.first; prop; prop = prop->next) {
    std::string prop_name = pxr::TfMakeValidIdentifier(prop->name);
    pxr::TfToken prop_token;
    pxr::UsdAttribute prop_attr;

    bool is_usd_attribute = false;

    // If starts with USD_ treat as usd property
    if (prop_name.rfind("USD_", 0) == 0) {
      std::string usd_prop_name = prop_name;
      usd_prop_name.erase(0, 4);
      prop_token = pxr::TfToken(usd_prop_name);
      prop_attr = prim.GetAttribute(prop_token);
      if (prop_attr)
        is_usd_attribute = true;
    }

    if (!is_usd_attribute) {
      prop_token = pxr::TfToken("userProperties:" + prop_name);
      switch (prop->type) {
        case IDP_INT:
          prop_attr = prim.CreateAttribute(prop_token, pxr::SdfValueTypeNames->Int, true);
          break;
        case IDP_FLOAT:
          prop_attr = prim.CreateAttribute(prop_token, pxr::SdfValueTypeNames->Float, true);
          break;
        case IDP_DOUBLE:
          prop_attr = prim.CreateAttribute(prop_token, pxr::SdfValueTypeNames->Double, true);
          break;
        case IDP_STRING:
          prop_attr = prim.CreateAttribute(prop_token, pxr::SdfValueTypeNames->String, true);
          break;
      }
    }

    if (prop_attr) {
      if (prop_attr.GetTypeName() == pxr::SdfValueTypeNames->Int)
        prop_attr.Set<int>(prop->data.val, timecode);

      else if (prop_attr.GetTypeName() == pxr::SdfValueTypeNames->Float)
        prop_attr.Set<float>(*(float *)&prop->data.val, timecode);

      else if (prop_attr.GetTypeName() == pxr::SdfValueTypeNames->Double)
        prop_attr.Set<double>(*(double *)&prop->data.val, timecode);

      else if (prop_attr.GetTypeName() == pxr::SdfValueTypeNames->String)
        prop_attr.Set<std::string>((char *)prop->data.pointer, timecode);

      else if (prop_attr.GetTypeName() == pxr::SdfValueTypeNames->Token)
        prop_attr.Set<pxr::TfToken>(pxr::TfToken((char *)prop->data.pointer), timecode);
    }
  }
}

}  // namespace USD
