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

#ifndef __USD_UTIL_H__
#define __USD_UTIL_H__

#include <string>

#ifdef _MSC_VER
#  define USD_INLINE static __forceinline
#else
#  define USD_INLINE static inline
#endif

#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdLux/light.h>
#include <pxr/usd/usdShade/material.h>

#include "usd.h"
#include "usd_exporter_context.h"

struct Light;

struct Material;
struct bNodeTree;
struct bNode;

namespace USD {

template<typename T>
T usd_define_or_over(pxr::UsdStageRefPtr stage, pxr::SdfPath path, bool as_overs = false)
{
  return (as_overs) ? T(stage->OverridePrim(path)) : T::Define(stage, path);
}

void localize(bNodeTree *localtree, bNodeTree *ntree);

void ntree_shader_groups_expand_inputs(bNodeTree *localtree);

void ntree_shader_groups_flatten(bNodeTree *localtree);

std::string get_node_tex_image_filepath(bNode *node);

}  // Namespace USD

#endif /* __USD_UTIL_H__ */
