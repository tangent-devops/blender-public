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

class USDPrimReader;
struct ImportSettings;

struct Light;

struct Material;
struct bNodeTree;

namespace USD {

USDPrimReader *create_reader(const pxr::UsdStageRefPtr &stage,
                             const pxr::UsdPrim &prim,
                             const USDImportParams &params,
                             ImportSettings &settings);
USDPrimReader *create_fake_reader(class USDStageReader *archive, const pxr::UsdPrim &prim);

}  // Namespace USD

#endif /* __USD_UTIL_H__ */
