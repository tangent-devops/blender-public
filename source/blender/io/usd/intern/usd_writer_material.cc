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
#include "usd_writer_material.h"
#include "usd_util.h"

extern "C" {
#include "BKE_animsys.h"
#include "BKE_colorband.h"
#include "BKE_colortools.h"
#include "BKE_key.h"
#include "BKE_node.h"

#include "DNA_color_types.h"
#include "DNA_light_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BKE_blender_version.h"
#include "BKE_cachefile.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_context.h"
#include "BKE_curve.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BKE_world.h"

#include "BLI_fileops.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
}

#include "MEM_guardedalloc.h"
#include "RNA_access.h"
#include "RNA_blender_cpp.h"
#include "RNA_types.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/scope.h>

//#include <pxr/usdImaging/usdImaging/tokens.h> NOT INCLUDED IN BLENDER BUILD

class MaterialExportException {
private:
  std::string m_error_str;
public:
  MaterialExportException(std::string error_str) : m_error_str(error_str) {
  }

  std::string get_error_str()  const {
    return m_error_str;
  }
};

/* TfToken objects are not cheap to construct, so we do it once. */
namespace usdtokens {
// Materials
static const pxr::TfToken diffuse_color("diffuseColor", pxr::TfToken::Immortal);
static const pxr::TfToken metallic("metallic", pxr::TfToken::Immortal);
static const pxr::TfToken preview_shader("previewShader", pxr::TfToken::Immortal);
static const pxr::TfToken preview_surface("UsdPreviewSurface", pxr::TfToken::Immortal);
static const pxr::TfToken uv_texture("UsdUVTexture", pxr::TfToken::Immortal);
static const pxr::TfToken primvar_float2("UsdPrimvarReader_float2", pxr::TfToken::Immortal);
static const pxr::TfToken roughness("roughness", pxr::TfToken::Immortal);
static const pxr::TfToken specular("specular", pxr::TfToken::Immortal);
static const pxr::TfToken opacity("opacity", pxr::TfToken::Immortal);
static const pxr::TfToken surface("surface", pxr::TfToken::Immortal);
static const pxr::TfToken perspective("perspective", pxr::TfToken::Immortal);
static const pxr::TfToken orthographic("orthographic", pxr::TfToken::Immortal);
static const pxr::TfToken rgb("rgb", pxr::TfToken::Immortal);
static const pxr::TfToken r("r", pxr::TfToken::Immortal);
static const pxr::TfToken g("g", pxr::TfToken::Immortal);
static const pxr::TfToken b("b", pxr::TfToken::Immortal);
static const pxr::TfToken st("st", pxr::TfToken::Immortal);
static const pxr::TfToken result("result", pxr::TfToken::Immortal);
static const pxr::TfToken varname("varname", pxr::TfToken::Immortal);
static const pxr::TfToken normal("normal", pxr::TfToken::Immortal);
static const pxr::TfToken ior("ior", pxr::TfToken::Immortal);
static const pxr::TfToken file("file", pxr::TfToken::Immortal);
static const pxr::TfToken preview("preview", pxr::TfToken::Immortal);
}  // namespace usdtokens

/* Cycles specific tokens (Blender Importer and HdCycles) */
namespace cyclestokens {
static const pxr::TfToken cycles("cycles", pxr::TfToken::Immortal);
static const pxr::TfToken UVMap("UVMap", pxr::TfToken::Immortal);
static const pxr::TfToken filename("filename", pxr::TfToken::Immortal);
static const pxr::TfToken interpolation("interpolation", pxr::TfToken::Immortal);
static const pxr::TfToken projection("projection", pxr::TfToken::Immortal);
static const pxr::TfToken extension("extension", pxr::TfToken::Immortal);
static const pxr::TfToken colorspace("colorspace", pxr::TfToken::Immortal);
static const pxr::TfToken attribute("attribute", pxr::TfToken::Immortal);
static const pxr::TfToken bsdf("bsdf", pxr::TfToken::Immortal);
static const pxr::TfToken closure("closure", pxr::TfToken::Immortal);
static const pxr::TfToken vector("vector", pxr::TfToken::Immortal);
static const pxr::TfToken image_source("image_source", pxr::TfToken::Immortal);

// tokens for material settings
namespace material {
  static const pxr::TfToken pass_id("cycles:material:pass_id", pxr::TfToken::Immortal);
  static const pxr::TfToken use_mis("cycles:material:use_mis", pxr::TfToken::Immortal);
  static const pxr::TfToken use_transparent_shadow(
    "cycles:material:use_transparent_shadow", pxr::TfToken::Immortal);
  static const pxr::TfToken heterogeneous_volume(
    "cycles:material:heterogeneous_volume", pxr::TfToken::Immortal);
  static const pxr::TfToken volume_sampling_method(
    "cycles:material:volume_sampling_method", pxr::TfToken::Immortal);
  static const pxr::TfToken volume_interpolation_method(
    "cycles:material:volume_interpolation_method", pxr::TfToken::Immortal);
  static const pxr::TfToken volume_step_rate(
    "cycles:material:volume_step_rate", pxr::TfToken::Immortal);
  static const pxr::TfToken displacement_method(
    "cycles:material:displacement_method", pxr::TfToken::Immortal);
}

// tokens for animated textures
static const pxr::TfToken num_frames("num_frames", pxr::TfToken::Immortal);
static const pxr::TfToken start_frame("start_frame", pxr::TfToken::Immortal);
static const pxr::TfToken frame_offset("frame_offset", pxr::TfToken::Immortal);
static const pxr::TfToken cyclic("cyclic", pxr::TfToken::Immortal);

// tokens for generated textures
static const pxr::TfToken gen_tex_x("gen_tex_x", pxr::TfToken::Immortal);
static const pxr::TfToken gen_tex_y("gen_tex_y", pxr::TfToken::Immortal);
static const pxr::TfToken gen_tex_type("gen_tex_type", pxr::TfToken::Immortal);
static const pxr::TfToken gen_tex_flag("gen_tex_flag", pxr::TfToken::Immortal);
static const pxr::TfToken gen_tex_color("gen_tex_color", pxr::TfToken::Immortal);

// token for movie texture
static const pxr::TfToken deinterlace("deinterlace", pxr::TfToken::Immortal);
}  // namespace cyclestokens

namespace USD {
static const int HD_CYCLES_CURVE_EXPORT_RES = 256;

/**
 * We need to encode cycles shader node enums as strings.
 * There seems to be no way to get these directly from the Cycles
 * API. So we have to store these for now.
 * Update: /source/blender/makesrna/intern/rna_nodetree.c
 * this looks suspiciously like we could use this to avoid these maps
 *
 */

// This helper wraps the conversion maps and in case of future features, or missing map entries
// we encode the index. HdCycles can ingest enums as strings or integers. The trouble with ints
// is that the order of enums is different from Blender to Cycles. Aguably, adding this ingeger
// fallback will 'hide' missing future features, and 'may' work. However this code should be
// considered 'live' and require tweaking with each new version until we can share this conversion
// somehow. (Perhaps as mentioned above with rna_nodetree.c)
bool usd_handle_shader_enum(pxr::TfToken a_token,
                            const std::map<int, std::string> &a_conversion_table,
                            pxr::UsdShadeShader &a_shader,
                            int a_value)
{
  std::map<int, std::string>::const_iterator it = a_conversion_table.find(a_value);
  if (it != a_conversion_table.end()) {
    a_shader.CreateInput(pxr::TfToken(a_token), pxr::SdfValueTypeNames->String).Set(it->second);
    return true;
  }
  else {
    a_shader.CreateInput(pxr::TfToken(a_token), pxr::SdfValueTypeNames->Int).Set(a_value);
  }
  return false;
}

bool usd_handle_material_enum(pxr::TfToken a_token,
                              const std::map<int, pxr::TfToken> &a_conversion_table,
                              pxr::UsdShadeMaterial &a_material,
                              int a_value)
{
  std::map<int, pxr::TfToken>::const_iterator it = a_conversion_table.find(a_value);
  if (it != a_conversion_table.end()) {
    a_material.GetPrim().CreateAttribute(a_token, pxr::SdfValueTypeNames->Token, false,
      pxr::SdfVariabilityUniform).Set(it->second);
    return true;
  }
  else {
    a_material.CreateInput(pxr::TfToken(a_token), pxr::SdfValueTypeNames->Int).Set(a_value);
  }
  return false;
}

static const std::map<int, pxr::TfToken> material_displacement_method_conversion = {
  {MA_DISPLACEMENT_BUMP, pxr::TfToken("displacement_bump")},
  {MA_DISPLACEMENT_TRUE, pxr::TfToken("displacement_true")},
  {MA_DISPLACEMENT_BOTH, pxr::TfToken("displacement_both")},
};

static const std::map<int, pxr::TfToken> material_volume_sampling_method_conversion = {
  {MA_VOLUME_SAMPLING_DISTANCE, pxr::TfToken("volume_sampling_distance")},
  {MA_VOLUME_SAMPLING_EQUIANGULAR, pxr::TfToken("volume_sampling_equiangular")},
  {MA_VOLUME_SAMPLING_MULTIPLE_IMPORTANCE, pxr::TfToken("volume_sampling_multiple_importance")},
};

static const std::map<int, pxr::TfToken> material_volume_interpolation_method_conversion = {
  {MA_VOLUME_INTERPOLATION_LINEAR, pxr::TfToken("volume_interpolation_linear")},
  {MA_VOLUME_INTERPOLATION_CUBIC, pxr::TfToken("volume_interpolation_cubic")},
};

static const std::map<int, std::string> node_noise_dimensions_conversion = {
    {1, "1D"},
    {2, "2D"},
    {3, "3D"},
    {4, "4D"},
};
static const std::map<int, std::string> node_voronoi_feature_conversion = {
    {SHD_VORONOI_F1, "f1"},
    {SHD_VORONOI_F2, "f2"},
    {SHD_VORONOI_SMOOTH_F1, "smooth_f1"},
    {SHD_VORONOI_DISTANCE_TO_EDGE, "distance_to_edge"},
    {SHD_VORONOI_N_SPHERE_RADIUS, "n_sphere_radius"},
};
static const std::map<int, std::string> node_voronoi_distance_conversion = {
    {SHD_VORONOI_EUCLIDEAN, "euclidean"},
    {SHD_VORONOI_MANHATTAN, "manhattan"},
    {SHD_VORONOI_CHEBYCHEV, "chebychev"},
    {SHD_VORONOI_MINKOWSKI, "minkowski"},
};
static const std::map<int, std::string> node_musgrave_type_conversion = {
    {SHD_MUSGRAVE_MULTIFRACTAL, "multifractal"},
    {SHD_MUSGRAVE_FBM, "fBM"},
    {SHD_MUSGRAVE_HYBRID_MULTIFRACTAL, "hybrid_multifractal"},
    {SHD_MUSGRAVE_RIDGED_MULTIFRACTAL, "ridged_multifractal"},
    {SHD_MUSGRAVE_HETERO_TERRAIN, "hetero_terrain"},
};
static const std::map<int, std::string> node_wave_type_conversion = {
    {SHD_WAVE_BANDS, "bands"},
    {SHD_WAVE_RINGS, "rings"},
};
static const std::map<int, std::string> node_wave_bands_direction_conversion = {
    {SHD_WAVE_BANDS_DIRECTION_X, "x"},
    {SHD_WAVE_BANDS_DIRECTION_Y, "y"},
    {SHD_WAVE_BANDS_DIRECTION_Z, "z"},
    {SHD_WAVE_BANDS_DIRECTION_DIAGONAL, "diagonal"},
};
static const std::map<int, std::string> node_wave_rings_direction_conversion = {
    {SHD_WAVE_RINGS_DIRECTION_X, "x"},
    {SHD_WAVE_RINGS_DIRECTION_Y, "y"},
    {SHD_WAVE_RINGS_DIRECTION_Z, "z"},
    {SHD_WAVE_RINGS_DIRECTION_SPHERICAL, "spherical"},
};
static const std::map<int, std::string> node_wave_profile_conversion = {
    {SHD_WAVE_PROFILE_SIN, "sine"},
    {SHD_WAVE_PROFILE_SAW, "saw"},
    {SHD_WAVE_PROFILE_TRI, "tri"},
};
static const std::map<int, std::string> node_point_density_space_conversion = {
    {SHD_POINTDENSITY_SPACE_OBJECT, "object"},
    {SHD_POINTDENSITY_SPACE_WORLD, "world"},
};
static const std::map<int, std::string> node_point_density_interpolation_conversion = {
    {SHD_INTERP_CLOSEST, "closest"},
    {SHD_INTERP_LINEAR, "linear"},
    {SHD_INTERP_CUBIC, "cubic"},
    {SHD_INTERP_SMART, "smart"},
};
static const std::map<int, std::string> node_mapping_type_conversion = {
    {NODE_MAPPING_TYPE_POINT, "point"},
    {NODE_MAPPING_TYPE_TEXTURE, "texture"},
    {NODE_MAPPING_TYPE_VECTOR, "vector"},
    {NODE_MAPPING_TYPE_NORMAL, "normal"},
};
// No defines exist for these, we create our own?
static const std::map<int, std::string> node_mix_rgb_type_conversion = {
    {0, "mix"},
    {1, "add"},
    {2, "multiply"},
    {3, "subtract"},
    {4, "screen"},
    {5, "divide"},
    {6, "difference"},
    {7, "darken"},
    {8, "lighten"},
    {9, "overlay"},
    {10, "dodge"},
    {11, "burn"},
    {12, "hue"},
    {13, "saturation"},
    {14, "value"},
    {15, "color"},
    {16, "soft_light"},
    {17, "linear_light"},
};
static const std::map<int, std::string> node_displacement_conversion = {
    {SHD_SPACE_TANGENT, "tangent"},
    {SHD_SPACE_OBJECT, "object"},
    {SHD_SPACE_WORLD, "world"},
    {SHD_SPACE_BLENDER_OBJECT, "blender_object"},
    {SHD_SPACE_BLENDER_WORLD, "blender_world"},
};
static const std::map<int, std::string> node_sss_falloff_conversion = {
    {SHD_SUBSURFACE_CUBIC, "cubic"},
    {SHD_SUBSURFACE_GAUSSIAN, "gaussian"},
    {SHD_SUBSURFACE_BURLEY, "burley"},
    {SHD_SUBSURFACE_RANDOM_WALK, "random_walk"},
};
static const std::map<int, std::string> node_principled_hair_parametrization_conversion = {
    {SHD_PRINCIPLED_HAIR_REFLECTANCE, "Direct coloring"},
    {SHD_PRINCIPLED_HAIR_PIGMENT_CONCENTRATION, "Melanin concentration"},
    {SHD_PRINCIPLED_HAIR_DIRECT_ABSORPTION, "Absorption coefficient"},
};
static const std::map<int, std::string> node_clamp_type_conversion = {
    {NODE_CLAMP_MINMAX, "minmax"},
    {NODE_CLAMP_RANGE, "range"},
};
static const std::map<int, std::string> node_math_type_conversion = {
    {NODE_MATH_ADD, "add"},
    {NODE_MATH_SUBTRACT, "subtract"},
    {NODE_MATH_MULTIPLY, "multiply"},
    {NODE_MATH_DIVIDE, "divide"},
    {NODE_MATH_MULTIPLY_ADD, "multiply_add"},
    {NODE_MATH_SINE, "sine"},
    {NODE_MATH_COSINE, "cosine"},
    {NODE_MATH_TANGENT, "tangent"},
    {NODE_MATH_SINH, "sinh"},
    {NODE_MATH_COSH, "cosh"},
    {NODE_MATH_TANH, "tanh"},
    {NODE_MATH_ARCSINE, "arcsine"},
    {NODE_MATH_ARCCOSINE, "arccosine"},
    {NODE_MATH_ARCTANGENT, "arctangent"},
    {NODE_MATH_POWER, "power"},
    {NODE_MATH_LOGARITHM, "logarithm"},
    {NODE_MATH_MINIMUM, "minimum"},
    {NODE_MATH_MAXIMUM, "maximum"},
    {NODE_MATH_ROUND, "round"},
    {NODE_MATH_LESS_THAN, "less_than"},
    {NODE_MATH_GREATER_THAN, "greater_than"},
    {NODE_MATH_MODULO, "modulo"},
    {NODE_MATH_ABSOLUTE, "absolute"},
    {NODE_MATH_ARCTAN2, "arctan2"},
    {NODE_MATH_FLOOR, "floor"},
    {NODE_MATH_CEIL, "ceil"},
    {NODE_MATH_FRACTION, "fraction"},
    {NODE_MATH_TRUNC, "trunc"},
    {NODE_MATH_SNAP, "snap"},
    {NODE_MATH_WRAP, "wrap"},
    {NODE_MATH_PINGPONG, "pingpong"},
    {NODE_MATH_SQRT, "sqrt"},
    {NODE_MATH_INV_SQRT, "inversesqrt"},
    {NODE_MATH_SIGN, "sign"},
    {NODE_MATH_EXPONENT, "exponent"},
    {NODE_MATH_RADIANS, "radians"},
    {NODE_MATH_DEGREES, "degrees"},
    {NODE_MATH_SMOOTH_MIN, "smoothmin"},
    {NODE_MATH_SMOOTH_MAX, "smoothmax"},
    {NODE_MATH_COMPARE, "compare"},
};
static const std::map<int, std::string> node_vector_math_type_conversion = {
    {NODE_VECTOR_MATH_ADD, "add"},
    {NODE_VECTOR_MATH_SUBTRACT, "subtract"},
    {NODE_VECTOR_MATH_MULTIPLY, "multiply"},
    {NODE_VECTOR_MATH_DIVIDE, "divide"},

    {NODE_VECTOR_MATH_CROSS_PRODUCT, "cross_product"},
    {NODE_VECTOR_MATH_PROJECT, "project"},
    {NODE_VECTOR_MATH_REFLECT, "reflect"},
    {NODE_VECTOR_MATH_DOT_PRODUCT, "dot_product"},

    {NODE_VECTOR_MATH_DISTANCE, "distance"},
    {NODE_VECTOR_MATH_LENGTH, "length"},
    {NODE_VECTOR_MATH_SCALE, "scale"},
    {NODE_VECTOR_MATH_NORMALIZE, "normalize"},

    {NODE_VECTOR_MATH_SNAP, "snap"},
    {NODE_VECTOR_MATH_FLOOR, "floor"},
    {NODE_VECTOR_MATH_CEIL, "ceil"},
    {NODE_VECTOR_MATH_MODULO, "modulo"},
    {NODE_VECTOR_MATH_FRACTION, "fraction"},
    {NODE_VECTOR_MATH_ABSOLUTE, "absolute"},
    {NODE_VECTOR_MATH_MINIMUM, "minimum"},
    {NODE_VECTOR_MATH_MAXIMUM, "maximum"},
    {NODE_VECTOR_MATH_WRAP, "wrap"},
    {NODE_VECTOR_MATH_SINE, "sine"},
    {NODE_VECTOR_MATH_COSINE, "cosine"},
    {NODE_VECTOR_MATH_TANGENT, "tangent"},
};
static const std::map<int, std::string> node_vector_rotate_type_conversion = {
    {NODE_VECTOR_ROTATE_TYPE_AXIS, "axis"},
    {NODE_VECTOR_ROTATE_TYPE_AXIS_X, "x_axis"},
    {NODE_VECTOR_ROTATE_TYPE_AXIS_Y, "y_axis"},
    {NODE_VECTOR_ROTATE_TYPE_AXIS_Z, "z_axis"},
    {NODE_VECTOR_ROTATE_TYPE_EULER_XYZ, "euler_xyz"},
};
static const std::map<int, std::string> node_vector_transform_type_conversion = {
    {SHD_VECT_TRANSFORM_TYPE_VECTOR, "vector"},
    {SHD_VECT_TRANSFORM_TYPE_POINT, "point"},
    {SHD_VECT_TRANSFORM_TYPE_NORMAL, "normal"},
};
static const std::map<int, std::string> node_vector_transform_space_conversion = {
    {SHD_VECT_TRANSFORM_SPACE_WORLD, "world"},
    {SHD_VECT_TRANSFORM_SPACE_OBJECT, "object"},
    {SHD_VECT_TRANSFORM_SPACE_CAMERA, "camera"},
};
static const std::map<int, std::string> node_normal_map_space_conversion = {
    {SHD_SPACE_TANGENT, "tangent"},
    {SHD_SPACE_OBJECT, "object"},
    {SHD_SPACE_WORLD, "world"},
    {SHD_SPACE_BLENDER_OBJECT, "blender_object"},
    {SHD_SPACE_BLENDER_WORLD, "blender_world"},
};
static const std::map<int, std::string> node_tangent_direction_type_conversion = {
    {SHD_TANGENT_RADIAL, "radial"},
    {SHD_TANGENT_UVMAP, "uv_map"},
};
static const std::map<int, std::string> node_tangent_axis_conversion = {
    {SHD_TANGENT_AXIS_X, "x"},
    {SHD_TANGENT_AXIS_Y, "y"},
    {SHD_TANGENT_AXIS_Z, "z"},
};
static const std::map<int, std::string> node_image_tex_alpha_type_conversion = {
    {IMA_ALPHA_STRAIGHT, "unassociated"},
    {IMA_ALPHA_PREMUL, "associated"},
    {IMA_ALPHA_CHANNEL_PACKED, "channel_packed"},
    {IMA_ALPHA_IGNORE, "ignore"},
    //{IMAGE_ALPHA_AUTO, "auto"},
};
static const std::map<int, std::string> node_image_tex_interpolation_conversion = {
    {SHD_INTERP_CLOSEST, "closest"},
    {SHD_INTERP_LINEAR, "linear"},
    {SHD_INTERP_CUBIC, "cubic"},
    {SHD_INTERP_SMART, "smart"},
};
static const std::map<int, std::string> node_image_tex_extension_conversion = {
    {SHD_IMAGE_EXTENSION_REPEAT, "periodic"},
    {SHD_IMAGE_EXTENSION_EXTEND, "clamp"},
    {SHD_IMAGE_EXTENSION_CLIP, "black"},
};
static const std::map<int, std::string> node_image_tex_projection_conversion = {
    {SHD_PROJ_FLAT, "flat"},
    {SHD_PROJ_BOX, "box"},
    {SHD_PROJ_SPHERE, "sphere"},
    {SHD_PROJ_TUBE, "tube"},
};
static const std::map<int, std::string> node_env_tex_projection_conversion = {
    {SHD_PROJ_EQUIRECTANGULAR, "equirectangular"},
    {SHD_PROJ_MIRROR_BALL, "mirror_ball"},
};
// TODO: 2.90 introduced enums
/*static const std::map<int, std::string> node_sky_tex_type_conversion = {
    {SHD_SKY_PREETHAM, "preetham"},
    {SHD_SKY_HOSEK, "hosek_wilkie"},
    {SHD_SKY_NISHITA, "nishita_improved"},
};*/
static const std::map<int, std::string> node_sky_tex_type_conversion = {
    {0, "preetham"},
    {1, "hosek_wilkie"},
    {2, "nishita_improved"},
};

// END TODO
static const std::map<int, std::string> node_gradient_tex_type_conversion = {
    {SHD_BLEND_LINEAR, "linear"},
    {SHD_BLEND_LINEAR, "quadratic"},
    {SHD_BLEND_EASING, "easing"},
    {SHD_BLEND_DIAGONAL, "diagonal"},
    {SHD_BLEND_RADIAL, "radial"},
    {SHD_BLEND_QUADRATIC_SPHERE, "quadratic_sphere"},
    {SHD_BLEND_SPHERICAL, "spherical"},
};
static const std::map<int, std::string> node_glossy_distribution_conversion = {
    {SHD_GLOSSY_SHARP, "sharp"},
    {SHD_GLOSSY_BECKMANN, "beckmann"},
    {SHD_GLOSSY_GGX, "GGX"},
    {SHD_GLOSSY_ASHIKHMIN_SHIRLEY, "ashikhmin_shirley"},
    {SHD_GLOSSY_MULTI_GGX, "Multiscatter GGX"},
};
static const std::map<int, std::string> node_anisotropic_distribution_conversion = {
    {SHD_GLOSSY_BECKMANN, "beckmann"},
    {SHD_GLOSSY_GGX, "GGX"},
    {SHD_GLOSSY_MULTI_GGX, "Multiscatter GGX"},
    {SHD_GLOSSY_ASHIKHMIN_SHIRLEY, "ashikhmin_shirley"},
};
static const std::map<int, std::string> node_glass_distribution_conversion = {
    {SHD_GLOSSY_SHARP, "sharp"},
    {SHD_GLOSSY_BECKMANN, "beckmann"},
    {SHD_GLOSSY_GGX, "GGX"},
    {SHD_GLOSSY_MULTI_GGX, "Multiscatter GGX"},
};
static const std::map<int, std::string> node_refraction_distribution_conversion = {
    {SHD_GLOSSY_SHARP, "sharp"},
    {SHD_GLOSSY_BECKMANN, "beckmann"},
    {SHD_GLOSSY_GGX, "GGX"},
};
static const std::map<int, std::string> node_toon_component_conversion = {
    {SHD_TOON_DIFFUSE, "diffuse"},
    {SHD_TOON_GLOSSY, "glossy"},
};
static const std::map<int, std::string> node_hair_component_conversion = {
    {SHD_HAIR_REFLECTION, "reflection"},
    {SHD_HAIR_TRANSMISSION, "transmission"},
};

static const std::map<int, std::string> node_principled_distribution_conversion = {
    {SHD_GLOSSY_GGX, "GGX"},
    {SHD_GLOSSY_MULTI_GGX, "Multiscatter GGX"},
};
static const std::map<int, std::string> node_principled_subsurface_method_conversion = {
    {SHD_SUBSURFACE_BURLEY, "burley"},
    {SHD_SUBSURFACE_RANDOM_WALK, "random_walk"},
};

void to_lower(std::string &string)
{
  std::transform(string.begin(), string.end(), string.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
}

void set_default(bNode *node,
                 bNodeSocket *socketValue,
                 bNodeSocket *socketName,
                 pxr::UsdShadeShader usd_shader)
{
  std::string inputName = socketName->identifier;

  switch (node->type) {
    case SH_NODE_MATH: {
      if (inputName == "Value_001")
        inputName = "Value2";
      else
        inputName = "Value1";
    } break;
    case SH_NODE_VECTOR_MATH: {
      if (inputName == "Vector_001")
        inputName = "Vector2";
      else if (inputName == "Vector_002")
        inputName = "Vector3";
      else
        inputName = "Vector1";
    } break;
    case SH_NODE_SEPRGB: {
      if (inputName == "Image")
        inputName = "color";
    } break;
  }

  to_lower(inputName);

  pxr::TfToken sock_in = pxr::TfToken(pxr::TfMakeValidIdentifier(inputName));
  switch (socketValue->type) {
    case SOCK_FLOAT: {
      bNodeSocketValueFloat *float_data = (bNodeSocketValueFloat *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Float)
          .Set(pxr::VtValue(float_data->value));
      break;
    }
    case SOCK_VECTOR: {
      bNodeSocketValueVector *vector_data = (bNodeSocketValueVector *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Float3)
          .Set(pxr::GfVec3f(vector_data->value[0], vector_data->value[1], vector_data->value[2]));
      break;
    }
    case SOCK_RGBA: {
      bNodeSocketValueRGBA *rgba_data = (bNodeSocketValueRGBA *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Float4)
          .Set(pxr::GfVec4f(
              rgba_data->value[0], rgba_data->value[1], rgba_data->value[2], rgba_data->value[2]));
      break;
    }
    case SOCK_BOOLEAN: {
      bNodeSocketValueBoolean *bool_data = (bNodeSocketValueBoolean *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Bool)
          .Set(pxr::VtValue(bool_data->value));
      break;
    }
    case SOCK_INT: {
      bNodeSocketValueInt *int_data = (bNodeSocketValueInt *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Int)
          .Set(pxr::VtValue(int_data->value));
      break;
    }
    case SOCK_STRING: {
      bNodeSocketValueString *string_data = (bNodeSocketValueString *)socketValue->default_value;
      usd_shader.CreateInput(sock_in, pxr::SdfValueTypeNames->Token)
          .Set(pxr::TfToken(pxr::TfMakeValidIdentifier(string_data->value)));
      break;
    }
    default:
      // Unsupported data type
      break;
  }
}

bNode *traverse_channel(bNodeSocket *input, short target_type = SH_NODE_TEX_IMAGE);

bNode *traverse_channel(bNodeSocket *input, short target_type)
{
  bNodeSocket *tSock = input;
  if (input->link) {
    bNode *tNode = tSock->link->fromnode;

    // if texture node
    if (tNode->type == target_type) {
      return tNode;
    }

    // for all inputs
    for (bNodeSocket *nSock = (bNodeSocket *)tNode->inputs.first; nSock; nSock = nSock->next) {
      tNode = traverse_channel(nSock);
      if (tNode)
        return tNode;
    }

    return NULL;
  }
  else {
    return NULL;
  }
}

/* Call this to create the asset filename input for each texture node (e.g. an Image Texture, or
 * Environment Texture). It supports export of animated image sequences). */
bool create_texture_shader_input(pxr::UsdShadeShader &shader,
                                          bNode * const node,
                                          ImageUser * const iuser,
                                          const bool export_animated_textures,
                                          const double anim_tex_start,
                                          const double anim_tex_end,
                                          const double current_frame) {

  Image *ima = (Image *) node->id;

  if ( !ima ) {
    throw MaterialExportException(
      std::string("Error: Image texture has not been specified for texture node ") +
      std::string(node->name));
  }

  shader.CreateInput(cyclestokens::image_source, pxr::SdfValueTypeNames->Int).Set(
    static_cast<int>(ima->source));

  if (ELEM(ima->source, IMA_SRC_FILE, IMA_SRC_SEQUENCE, IMA_SRC_MOVIE, IMA_SRC_TILED)) {

    std::string imagePath = get_node_tex_image_filepath(node);

    if (imagePath.size() > 0) {
      if (ELEM(ima->source, IMA_SRC_SEQUENCE, IMA_SRC_MOVIE)) {
        // TO DO: If hdcycles is modified to calculate the frames based on exported parameters for
        // Frames, Start Frame, Offset, Cyclic then there's no need to bake out the calculated
        // frame's filename, per frame. Remove the per frame exporting code once that's done.
        shader.CreateInput(cyclestokens::num_frames, pxr::SdfValueTypeNames->Int)
          .Set(iuser->frames);
        shader.CreateInput(cyclestokens::start_frame, pxr::SdfValueTypeNames->Int)
          .Set(iuser->sfra);
        shader.CreateInput(cyclestokens::frame_offset, pxr::SdfValueTypeNames->Int)
          .Set(iuser->offset);
        shader.CreateInput(cyclestokens::cyclic, pxr::SdfValueTypeNames->Bool)
          .Set(static_cast<bool>(iuser->cycl));

        if (ELEM(ima->source, IMA_SRC_MOVIE)) {
          shader.CreateInput(cyclestokens::deinterlace, pxr::SdfValueTypeNames->Bool)
            .Set( ((ima->flag & IMA_DEINTERLACE) != 0) );
        }

        if (!export_animated_textures or ELEM(ima->source, IMA_SRC_MOVIE)) {
          // Export the scene's current frame only.
          bool is_in_range;
          auto file_frame_num = BKE_image_user_frame_get(iuser, current_frame, &is_in_range);
          std::string imagePath = get_node_tex_image_filepath(node, file_frame_num);
          shader.CreateInput(cyclestokens::filename, pxr::SdfValueTypeNames->Asset)
            .Set(pxr::SdfAssetPath(imagePath));
        } else {
          auto shade_input = shader.CreateInput(cyclestokens::filename,
                                                pxr::SdfValueTypeNames->Asset);
          for (auto output_frame_num = anim_tex_start;
               output_frame_num <= anim_tex_end;
               output_frame_num++) {
            bool is_in_range;
            auto file_frame_num = BKE_image_user_frame_get(iuser, output_frame_num, &is_in_range);

            std::string perFrameImagePath = get_node_tex_image_filepath(node, file_frame_num);
            shade_input.Set(pxr::SdfAssetPath(perFrameImagePath), output_frame_num);
          }
        }
      } else {
        shader.CreateInput(cyclestokens::filename, pxr::SdfValueTypeNames->Asset)
          .Set(pxr::SdfAssetPath(imagePath));
      }
      return true;
    } else {
      shader.CreateInput(cyclestokens::filename, pxr::SdfValueTypeNames->Asset)
        .Set(pxr::SdfAssetPath(""));
    }
    return false;
  }
  else if (ELEM(ima->source, IMA_SRC_GENERATED)) {
    shader.CreateInput(cyclestokens::gen_tex_x, pxr::SdfValueTypeNames->Int)
      .Set(ima->gen_x);
    shader.CreateInput(cyclestokens::gen_tex_y, pxr::SdfValueTypeNames->Int)
      .Set(ima->gen_y);
    shader.CreateInput(cyclestokens::gen_tex_type, pxr::SdfValueTypeNames->Int)
      .Set(static_cast<int>(ima->gen_type));
    shader.CreateInput(cyclestokens::gen_tex_flag, pxr::SdfValueTypeNames->Int)
      .Set(static_cast<int>(ima->gen_flag));
    shader.CreateInput(cyclestokens::gen_tex_color, pxr::SdfValueTypeNames->Float4)
      .Set(pxr::GfVec4f(
        ima->gen_color[0], ima->gen_color[1], ima->gen_color[2], ima->gen_color[3]));

    return true;
  }
  else if (ELEM(ima->source, IMA_SRC_VIEWER)) {
    // Not currently supported.
    return false;
  }
  return false;
}

/* Creates a USD Preview Surface node based on given cycles shading node */
pxr::UsdShadeShader create_usd_preview_shader_node(USDExporterContext const &usd_export_context_,
                                                   pxr::UsdShadeMaterial const &material,
                                                   const char *const name,
                                                   const int type,
                                                   bNode *const node,
                                                   const bool export_animated_textures,
                                                   const double anim_tex_start,
                                                   const double anim_tex_end,
                                                   const double current_frame)
{
  pxr::SdfPath shader_path = material.GetPath()
                                 .AppendChild(usdtokens::preview)
                                 .AppendChild(pxr::TfToken(pxr::TfMakeValidIdentifier(name)));
  pxr::UsdShadeShader shader = (usd_export_context_.export_params.export_as_overs) ?
                                   pxr::UsdShadeShader(
                                       usd_export_context_.stage->OverridePrim(shader_path)) :
                                   pxr::UsdShadeShader::Define(usd_export_context_.stage,
                                                               shader_path);
  switch (type) {
    case SH_NODE_TEX_IMAGE: {
      shader.CreateIdAttr(pxr::VtValue(usdtokens::uv_texture));

      NodeTexImage *tex_original = (NodeTexImage *)node->storage;

      create_texture_shader_input(shader, node, &tex_original->iuser,
        export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
      break;
    }
    case SH_NODE_TEX_COORD:
    case SH_NODE_UVMAP: {
      shader.CreateIdAttr(pxr::VtValue(usdtokens::primvar_float2));
      break;
    }
    /*case SH_NODE_MAPPING: {
      shader.CreateIdAttr(pxr::VtValue(usdtokens::primvar_float2));
      break;
    }*/
    case SH_NODE_BSDF_DIFFUSE:
    case SH_NODE_BSDF_PRINCIPLED: {
      shader.CreateIdAttr(pxr::VtValue(usdtokens::preview_surface));
      material.CreateSurfaceOutput().ConnectToSource(shader, usdtokens::surface);
      break;
    }

    default:
      break;
  }

  return shader;
}

/* Creates a USDShadeShader based on given cycles shading node */
pxr::UsdShadeShader create_cycles_shader_node(pxr::UsdStageRefPtr a_stage,
                                              pxr::SdfPath &shaderPath,
                                              bNode *node,
                                              const bool a_asOvers,
                                              const bool export_animated_textures,
                                              const double anim_tex_start,
                                              const double anim_tex_end,
                                              const double current_frame)
{
  pxr::SdfPath primpath = shaderPath.AppendChild(
      pxr::TfToken(pxr::TfMakeValidIdentifier(node->name)));

  // Early out if already created
  if (a_stage->GetPrimAtPath(primpath).IsValid())
    return pxr::UsdShadeShader::Get(a_stage, primpath);

  pxr::UsdShadeShader shader = (a_asOvers) ? pxr::UsdShadeShader(a_stage->OverridePrim(primpath)) :
                                             pxr::UsdShadeShader::Define(a_stage, primpath);

  // Author Cycles Shader Node ID
  // For now we convert spaces to _ and transform to lowercase.
  // This isn't a 1:1 gaurantee it will be in the format for cycles standalone.
  // e.g. Blender: ShaderNodeBsdfPrincipled. Cycles_principled_bsdf
  // But works for now. We should also author idname to easier import directly
  // to Blender.
  bNodeType *ntype = node->typeinfo;
  std::string usd_shade_type_name(ntype->ui_name);
  to_lower(usd_shade_type_name);

  // TODO Move this to a more generic conversion map?
  if (usd_shade_type_name == "rgb")
    usd_shade_type_name = "color";
  if (node->type == SH_NODE_MIX_SHADER)
    usd_shade_type_name = "mix_closure";
  if (node->type == SH_NODE_ADD_SHADER)
    usd_shade_type_name = "add_closure";
  if (node->type == SH_NODE_OUTPUT_MATERIAL)
    usd_shade_type_name = "output";
  if (node->type == SH_NODE_OUTPUT_WORLD)
    usd_shade_type_name = "output";
  if (node->type == SH_NODE_OUTPUT_LIGHT)
    usd_shade_type_name = "output";
  if (node->type == SH_NODE_UVMAP)
    usd_shade_type_name = "uvmap";
  if (node->type == SH_NODE_VALTORGB)
    usd_shade_type_name = "rgb_ramp";
  if (node->type == SH_NODE_HUE_SAT)
    usd_shade_type_name = "hsv";
  if (node->type == SH_NODE_BRIGHTCONTRAST)
    usd_shade_type_name = "brightness_contrast";
  if (node->type == SH_NODE_BACKGROUND)
    usd_shade_type_name = "background_shader";
  if (node->type == SH_NODE_VOLUME_SCATTER)
    usd_shade_type_name = "scatter_volume";
  if (node->type == SH_NODE_VOLUME_ABSORPTION)
    usd_shade_type_name = "absorption_volume";

  shader.CreateIdAttr(
      pxr::VtValue(pxr::TfToken("cycles_" + pxr::TfMakeValidIdentifier(usd_shade_type_name))));

  // Store custom1-4

  switch (node->type) {
    case SH_NODE_TEX_WHITE_NOISE: {
      usd_handle_shader_enum(pxr::TfToken("Dimensions"),
                             node_noise_dimensions_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_MATH: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_math_type_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_VECTOR_MATH: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_vector_math_type_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_MAPPING: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_mapping_type_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_MIX_RGB: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_mix_rgb_type_conversion, shader, (int)node->custom1);
      shader.CreateInput(pxr::TfToken("Use_Clamp"), pxr::SdfValueTypeNames->Bool)
          .Set(node->custom1 & SHD_MIXRGB_CLAMP);
    } break;
    case SH_NODE_VECTOR_DISPLACEMENT: {
      usd_handle_shader_enum(
          pxr::TfToken("Space"), node_displacement_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_VECTOR_ROTATE: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_vector_rotate_type_conversion, shader, (int)node->custom1);
      shader.CreateInput(pxr::TfToken("Invert"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom2);
    } break;
    case SH_NODE_VECT_TRANSFORM: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_vector_transform_type_conversion, shader, (int)node->custom1);
      usd_handle_shader_enum(pxr::TfToken("Space"),
                             node_vector_transform_space_conversion,
                             shader,
                             (int)node->custom2);
    } break;
    case SH_NODE_SUBSURFACE_SCATTERING: {
      usd_handle_shader_enum(
          pxr::TfToken("Falloff"), node_sss_falloff_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_CLAMP: {
      usd_handle_shader_enum(
          pxr::TfToken("Type"), node_clamp_type_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_WIREFRAME: {
      shader.CreateInput(pxr::TfToken("Use_Pixel_Size"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom1);
    } break;
    case SH_NODE_BSDF_GLOSSY: {
      // Cycles Standalone uses a different enum for distribution and subsurface, we encode strings
      // instead
      usd_handle_shader_enum(pxr::TfToken("Distribution"),
                             node_glossy_distribution_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_BSDF_REFRACTION: {
      // Cycles Standalone uses a different enum for distribution and subsurface, we encode strings
      // instead
      usd_handle_shader_enum(pxr::TfToken("Distribution"),
                             node_refraction_distribution_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_BSDF_TOON: {
      usd_handle_shader_enum(
          pxr::TfToken("component"), node_toon_component_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_DISPLACEMENT: {
      usd_handle_shader_enum(
          pxr::TfToken("Space"), node_displacement_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_BSDF_HAIR: {
      usd_handle_shader_enum(
          pxr::TfToken("component"), node_hair_component_conversion, shader, (int)node->custom1);
    } break;
    case SH_NODE_BSDF_HAIR_PRINCIPLED: {
      usd_handle_shader_enum(pxr::TfToken("parametrization"),
                             node_principled_hair_parametrization_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_MAP_RANGE: {
      shader.CreateInput(pxr::TfToken("Use_Clamp"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom1);
      shader.CreateInput(pxr::TfToken("Type"), pxr::SdfValueTypeNames->Int)
          .Set((int)node->custom2);
    } break;
    case SH_NODE_BEVEL: {
      shader.CreateInput(pxr::TfToken("Samples"), pxr::SdfValueTypeNames->Int)
          .Set((int)node->custom1);
    } break;
    case SH_NODE_AMBIENT_OCCLUSION: {
      shader.CreateInput(pxr::TfToken("Samples"), pxr::SdfValueTypeNames->Int)
          .Set((int)node->custom1);
      // TODO: Format?
      shader.CreateInput(pxr::TfToken("Inside"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom2);
      shader.CreateInput(pxr::TfToken("Only_Local"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom3);
    } break;
    case SH_NODE_BSDF_ANISOTROPIC: {
      // Cycles Standalone uses a different enum for distribution and subsurface, we encode strings
      // instead
      usd_handle_shader_enum(pxr::TfToken("Distribution"),
                             node_anisotropic_distribution_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_BSDF_GLASS: {
      // Cycles Standalone uses a different enum for distribution and subsurface, we encode strings
      // instead
      usd_handle_shader_enum(pxr::TfToken("Distribution"),
                             node_glass_distribution_conversion,
                             shader,
                             (int)node->custom1);
    } break;
    case SH_NODE_BUMP: {
      shader.CreateInput(pxr::TfToken("Invert"), pxr::SdfValueTypeNames->Bool)
          .Set((bool)node->custom1);
    } break;
    case SH_NODE_BSDF_PRINCIPLED: {
      // Cycles Standalone uses a different enum for distribution and subsurface, we encode strings
      // instead

      int distribution = ((node->custom1) & 6);

      usd_handle_shader_enum(pxr::TfToken("Distribution"),
                             node_principled_distribution_conversion,
                             shader,
                             (int)node->custom1);
      usd_handle_shader_enum(pxr::TfToken("Subsurface_Method"),
                             node_principled_subsurface_method_conversion,
                             shader,
                             (int)node->custom2);

      // Removed in 2.82+?
      bool sss_diffuse_blend_get = (((node->custom1) & 8) != 0);
      shader.CreateInput(pxr::TfToken("Blend_SSS_Diffuse"), pxr::SdfValueTypeNames->Bool)
          .Set(sss_diffuse_blend_get);
    } break;
  }

  // Convert all internal storage
  switch (node->type) {

      // -- Texture Node Storage

    case SH_NODE_TEX_SKY: {
      NodeTexSky *sky_storage = (NodeTexSky *)node->storage;
      if (!sky_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
      usd_handle_shader_enum(
          pxr::TfToken("type"), node_sky_tex_type_conversion, shader, sky_storage->sky_model);
      shader.CreateInput(pxr::TfToken("sun_direction"), pxr::SdfValueTypeNames->Vector3f)
          .Set(pxr::GfVec3f(sky_storage->sun_direction[0],
                            sky_storage->sun_direction[1],
                            sky_storage->sun_direction[2]));
      shader.CreateInput(pxr::TfToken("turbidity"), pxr::SdfValueTypeNames->Float)
          .Set(sky_storage->turbidity);
      shader.CreateInput(pxr::TfToken("ground_albedo"), pxr::SdfValueTypeNames->Float)
          .Set(sky_storage->ground_albedo);
    } break;

    case SH_NODE_TEX_IMAGE: {
      NodeTexImage *tex_original = (NodeTexImage *)node->storage;

      if (!tex_original)
        break;

      create_texture_shader_input(shader, node, &tex_original->iuser, export_animated_textures,
        anim_tex_start, anim_tex_end, current_frame);

      usd_handle_shader_enum(cyclestokens::interpolation,
                             node_image_tex_interpolation_conversion,
                             shader,
                             tex_original->interpolation);
      usd_handle_shader_enum(cyclestokens::projection,
                             node_image_tex_projection_conversion,
                             shader,
                             tex_original->projection);
      usd_handle_shader_enum(cyclestokens::extension,
                             node_image_tex_extension_conversion,
                             shader,
                             tex_original->extension);

      Image *ima = (Image *)node->id;
      usd_handle_shader_enum(pxr::TfToken("alpha_type"),
                             node_image_tex_alpha_type_conversion,
                             shader,
                             (int)ima->alpha_mode);

      // Colorspace RNA
      PointerRNA id_ptr;
      RNA_id_pointer_create(node->id, &id_ptr);
      BL::Image b_image(id_ptr);
      PointerRNA colorspace_ptr = b_image.colorspace_settings().ptr;
      PropertyRNA *prop = RNA_struct_find_property(&colorspace_ptr, "name");
      const char *identifier = "";
      int value = RNA_property_enum_get(&colorspace_ptr, prop);
      RNA_property_enum_identifier(NULL, &colorspace_ptr, prop, value, &identifier);

      shader.CreateInput(cyclestokens::colorspace, pxr::SdfValueTypeNames->String)
          .Set(std::string(identifier));

      break;
    }

    case SH_NODE_TEX_CHECKER: {
      // NodeTexChecker *storage = (NodeTexChecker *)node->storage;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
    } break;

    case SH_NODE_TEX_BRICK: {
      NodeTexBrick *brick_storage = (NodeTexBrick *)node->storage;
      if (!brick_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
      shader.CreateInput(pxr::TfToken("offset_freq"), pxr::SdfValueTypeNames->Int)
          .Set(brick_storage->offset_freq);
      shader.CreateInput(pxr::TfToken("squash_freq"), pxr::SdfValueTypeNames->Int)
          .Set(brick_storage->squash_freq);
      shader.CreateInput(pxr::TfToken("offset"), pxr::SdfValueTypeNames->Float)
          .Set(brick_storage->offset);
      shader.CreateInput(pxr::TfToken("squash"), pxr::SdfValueTypeNames->Float)
          .Set(brick_storage->squash);
    } break;

    case SH_NODE_TEX_ENVIRONMENT: {
      NodeTexEnvironment *env_storage = (NodeTexEnvironment *)node->storage;
      if (!env_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;

      create_texture_shader_input(shader, node, &env_storage->iuser, export_animated_textures,
        anim_tex_start, anim_tex_end, current_frame);

      usd_handle_shader_enum(cyclestokens::projection,
                             node_env_tex_projection_conversion,
                             shader,
                             env_storage->projection);
      usd_handle_shader_enum(cyclestokens::interpolation,
                             node_image_tex_interpolation_conversion,
                             shader,
                             env_storage->interpolation);

      Image *ima = (Image *)node->id;
      usd_handle_shader_enum(pxr::TfToken("alpha_type"),
                             node_image_tex_alpha_type_conversion,
                             shader,
                             (int)ima->alpha_mode);

      // Colorspace RNA
      PointerRNA id_ptr;
      RNA_id_pointer_create(node->id, &id_ptr);
      BL::Image b_image(id_ptr);
      PointerRNA colorspace_ptr = b_image.colorspace_settings().ptr;
      PropertyRNA *prop = RNA_struct_find_property(&colorspace_ptr, "name");
      const char *identifier = "";
      int value = RNA_property_enum_get(&colorspace_ptr, prop);
      RNA_property_enum_identifier(NULL, &colorspace_ptr, prop, value, &identifier);

      shader.CreateInput(cyclestokens::colorspace, pxr::SdfValueTypeNames->String)
        .Set(std::string(identifier));
    } break;

    case SH_NODE_TEX_GRADIENT: {
      NodeTexGradient *grad_storage = (NodeTexGradient *)node->storage;
      if (!grad_storage)
        break;

      usd_handle_shader_enum(pxr::TfToken("type"),
                             node_gradient_tex_type_conversion,
                             shader,
                             grad_storage->gradient_type);
    } break;

    case SH_NODE_TEX_NOISE: {
      NodeTexNoise *noise_storage = (NodeTexNoise *)node->storage;
      if (!noise_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
      usd_handle_shader_enum(pxr::TfToken("dimensions"),
                             node_noise_dimensions_conversion,
                             shader,
                             noise_storage->dimensions);
    } break;

    case SH_NODE_TEX_VORONOI: {
      NodeTexVoronoi *voronoi_storage = (NodeTexVoronoi *)node->storage;
      if (!voronoi_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
      usd_handle_shader_enum(pxr::TfToken("dimensions"),
                             node_noise_dimensions_conversion,
                             shader,
                             voronoi_storage->dimensions);
      usd_handle_shader_enum(pxr::TfToken("feature"),
                             node_voronoi_feature_conversion,
                             shader,
                             voronoi_storage->feature);
      usd_handle_shader_enum(pxr::TfToken("metric"),
                             node_voronoi_distance_conversion,
                             shader,
                             voronoi_storage->distance);
    } break;

    case SH_NODE_TEX_MUSGRAVE: {
      NodeTexMusgrave *musgrave_storage = (NodeTexMusgrave *)node->storage;
      if (!musgrave_storage)
        break;

      usd_handle_shader_enum(pxr::TfToken("type"),
                             node_musgrave_type_conversion,
                             shader,
                             musgrave_storage->musgrave_type);
      usd_handle_shader_enum(pxr::TfToken("dimensions"),
                             node_noise_dimensions_conversion,
                             shader,
                             musgrave_storage->dimensions);
    } break;

    case SH_NODE_TEX_WAVE: {
      NodeTexWave *wave_storage = (NodeTexWave *)node->storage;
      if (!wave_storage)
        break;

      usd_handle_shader_enum(
          pxr::TfToken("type"), node_wave_type_conversion, shader, wave_storage->wave_type);
      usd_handle_shader_enum(pxr::TfToken("profile"),
                             node_wave_profile_conversion,
                             shader,
                             wave_storage->wave_profile);
      usd_handle_shader_enum(pxr::TfToken("rings_direction"),
                             node_wave_rings_direction_conversion,
                             shader,
                             wave_storage->rings_direction);
      usd_handle_shader_enum(pxr::TfToken("bands_direction"),
                             node_wave_bands_direction_conversion,
                             shader,
                             wave_storage->bands_direction);

    } break;

    case SH_NODE_TEX_POINTDENSITY: {
      NodeShaderTexPointDensity *pd_storage = (NodeShaderTexPointDensity *)node->storage;
      if (!pd_storage)
        break;

      // TODO: Incomplete...

      usd_handle_shader_enum(pxr::TfToken("space"),
                             node_point_density_space_conversion,
                             shader,
                             (int)pd_storage->space);
      usd_handle_shader_enum(pxr::TfToken("interpolation"),
                             node_point_density_interpolation_conversion,
                             shader,
                             (int)pd_storage->interpolation);
    } break;

    case SH_NODE_TEX_MAGIC: {
      NodeTexMagic *magic_storage = (NodeTexMagic *)node->storage;
      if (!magic_storage)
        break;
      // TexMapping tex_mapping;
      // ColorMapping color_mapping;
      shader.CreateInput(pxr::TfToken("depth"), pxr::SdfValueTypeNames->Int)
          .Set(magic_storage->depth);
    } break;

      // ==== Ramp

    case SH_NODE_VALTORGB: {
      ColorBand *coba = (ColorBand *)node->storage;
      if (!coba)
        break;

      pxr::VtVec3fArray array;
      pxr::VtFloatArray alpha_array;

      int size = HD_CYCLES_CURVE_EXPORT_RES;
      for (int i = 0; i < size; i++) {

        const float in = (float)i / size;
        float out[4] = {0, 0, 0, 0};

        BKE_colorband_evaluate(coba, in, out);
        array.push_back(pxr::GfVec3f(out[0], out[1], out[2]));
        alpha_array.push_back(out[3]);
      }

      shader.CreateInput(pxr::TfToken("Interpolate"), pxr::SdfValueTypeNames->Bool)
          .Set(coba->ipotype != COLBAND_INTERP_LINEAR);

      shader.CreateInput(pxr::TfToken("Ramp"), pxr::SdfValueTypeNames->Float3Array).Set(array);
      shader.CreateInput(pxr::TfToken("Ramp_Alpha"), pxr::SdfValueTypeNames->FloatArray)
          .Set(alpha_array);
    } break;

      // ==== Curves

    case SH_NODE_CURVE_VEC: {
      CurveMapping *vec_curve_storage = (CurveMapping *)node->storage;
      if (!vec_curve_storage)
        break;

      pxr::VtVec3fArray array;

      BKE_curvemapping_initialize(vec_curve_storage);

      int size = HD_CYCLES_CURVE_EXPORT_RES;
      for (int i = 0; i < size; i++) {

        float out[3] = {0, 0, 0};

        const float iter[3] = {(float)i / size, (float)i / size, (float)i / size};

        BKE_curvemapping_evaluate3F(vec_curve_storage, out, iter);
        array.push_back(pxr::GfVec3f(out[0], out[1], out[2]));
      }

      // @TODO(bjs): Implement properly
      shader.CreateInput(pxr::TfToken("Min_X"), pxr::SdfValueTypeNames->Float).Set(0.0f);
      shader.CreateInput(pxr::TfToken("Max_X"), pxr::SdfValueTypeNames->Float).Set(1.0f);

      shader.CreateInput(pxr::TfToken("Curves"), pxr::SdfValueTypeNames->Float3Array).Set(array);

    } break;

    case SH_NODE_CURVE_RGB: {
      CurveMapping *col_curve_storage = (CurveMapping *)node->storage;
      if (!col_curve_storage)
        break;

      pxr::VtVec3fArray array;

      BKE_curvemapping_initialize(col_curve_storage);

      int size = HD_CYCLES_CURVE_EXPORT_RES;
      for (int i = 0; i < size; i++) {

        float out[3] = {0, 0, 0};

        const float iter[3] = {(float)i / size, (float)i / size, (float)i / size};

        BKE_curvemapping_evaluateRGBF(col_curve_storage, out, iter);
        array.push_back(pxr::GfVec3f(out[0], out[1], out[2]));
      }

      // @TODO(bjs): Implement properly
      shader.CreateInput(pxr::TfToken("Min_X"), pxr::SdfValueTypeNames->Float).Set(0.0f);
      shader.CreateInput(pxr::TfToken("Max_X"), pxr::SdfValueTypeNames->Float).Set(1.0f);

      shader.CreateInput(pxr::TfToken("Curves"), pxr::SdfValueTypeNames->Float3Array).Set(array);
    } break;

    // ==== Misc
    case SH_NODE_VALUE: {
      if (!node->outputs.first)
        break;
      bNodeSocket *val_sock = (bNodeSocket *)node->outputs.first;
      if (val_sock) {
        bNodeSocketValueFloat *float_data = (bNodeSocketValueFloat *)val_sock->default_value;
        shader.CreateInput(pxr::TfToken("value"), pxr::SdfValueTypeNames->Float)
            .Set(float_data->value);
      }
    } break;

    case SH_NODE_RGB: {
      if (!node->outputs.first)
        break;
      bNodeSocket *val_sock = (bNodeSocket *)node->outputs.first;
      if (val_sock) {
        bNodeSocketValueRGBA *col_data = (bNodeSocketValueRGBA *)val_sock->default_value;
        shader.CreateInput(pxr::TfToken("value"), pxr::SdfValueTypeNames->Color3f)
            .Set(pxr::GfVec3f(col_data->value[0], col_data->value[1], col_data->value[2]));
      }
    } break;

    case SH_NODE_UVMAP: {
      NodeShaderUVMap *uv_storage = (NodeShaderUVMap *)node->storage;
      if (!uv_storage)
        break;
      // We need to make valid here because actual uv primvar has been
      shader.CreateInput(cyclestokens::attribute, pxr::SdfValueTypeNames->String)
          .Set(pxr::TfMakeValidIdentifier(uv_storage->uv_map));
      break;
    }

    case SH_NODE_HUE_SAT: {
      NodeHueSat *hue_sat_node_str = (NodeHueSat *)node->storage;
      if (!hue_sat_node_str)
        break;
      shader.CreateInput(pxr::TfToken("hue"), pxr::SdfValueTypeNames->Float)
          .Set(hue_sat_node_str->hue);
      shader.CreateInput(pxr::TfToken("sat"), pxr::SdfValueTypeNames->Float)
          .Set(hue_sat_node_str->sat);
      shader.CreateInput(pxr::TfToken("val"), pxr::SdfValueTypeNames->Float)
          .Set(hue_sat_node_str->val);
    } break;

    case SH_NODE_TANGENT: {
      NodeShaderTangent *tangent_node_str = (NodeShaderTangent *)node->storage;
      if (!tangent_node_str)
        break;
      usd_handle_shader_enum(pxr::TfToken("direction_type"),
                             node_tangent_direction_type_conversion,
                             shader,
                             tangent_node_str->direction_type);
      usd_handle_shader_enum(
          pxr::TfToken("axis"), node_tangent_axis_conversion, shader, tangent_node_str->axis);
      shader.CreateInput(pxr::TfToken("Attribute"), pxr::SdfValueTypeNames->String)
          .Set(tangent_node_str->uv_map);
    } break;

    case SH_NODE_NORMAL_MAP: {
      NodeShaderNormalMap *normal_node_str = (NodeShaderNormalMap *)node->storage;
      if (!normal_node_str)
        break;
      usd_handle_shader_enum(
          pxr::TfToken("Space"), node_normal_map_space_conversion, shader, normal_node_str->space);

      // We need to make valid here because actual uv primvar has been
      shader.CreateInput(pxr::TfToken("Attribute"), pxr::SdfValueTypeNames->String)
          .Set(pxr::TfMakeValidIdentifier(normal_node_str->uv_map));
    } break;

    case SH_NODE_VERTEX_COLOR: {
      NodeShaderVertexColor *vert_col_node_str = (NodeShaderVertexColor *)node->storage;
      if (!vert_col_node_str)
        break;
      shader.CreateInput(pxr::TfToken("layer_name"), pxr::SdfValueTypeNames->String)
          .Set(vert_col_node_str->layer_name);
    } break;

    case SH_NODE_TEX_IES: {
      NodeShaderTexIES *ies_node_str = (NodeShaderTexIES *)node->storage;
      if (!ies_node_str)
        break;
      shader.CreateInput(pxr::TfToken("mode"), pxr::SdfValueTypeNames->Int)
          .Set(ies_node_str->mode);

      // TODO Cycles standalone expects this as "File Name" ustring...
      shader.CreateInput(cyclestokens::filename, pxr::SdfValueTypeNames->Asset)
          .Set(pxr::SdfAssetPath(ies_node_str->filepath));
    } break;

    case SH_NODE_ATTRIBUTE: {
      NodeShaderAttribute *attr_node_str = (NodeShaderAttribute *)node->storage;
      if (!attr_node_str)
        break;
      shader.CreateInput(pxr::TfToken("Attribute"), pxr::SdfValueTypeNames->String)
          .Set(attr_node_str->name);
    } break;
  }

  // Assign default input inputs
  for (bNodeSocket *nSock = (bNodeSocket *)node->inputs.first; nSock; nSock = nSock->next) {
    set_default(node, nSock, nSock, shader);
  }

  return shader;
}

/* Entry point to create approximate USD Preview Surface network from Cycles node graph.
 * Due to the limited nodes in the USD Preview Surface Spec, only the following nodes
 * are supported:
 *  - UVMap
 *  - Texture Coordinate
 *  - Image Texture
 *  - Principled BSDF
 * More may be added in the future. */
void create_usd_preview_surface_material(USDExporterContext const &usd_export_context_,
                                         Material *material,
                                         pxr::UsdShadeMaterial &usd_material,
                                         const bool export_animated_textures,
                                         const double anim_tex_start,
                                         const double anim_tex_end,
                                         const double current_frame)
{
  try {
    usd_define_or_over<pxr::UsdGeomScope>(usd_export_context_.stage,
                                          usd_material.GetPath().AppendChild(usdtokens::preview),
                                          usd_export_context_.export_params.export_as_overs);

    pxr::TfToken defaultUVSampler = (usd_export_context_.export_params.convert_uv_to_st) ?
                                    usdtokens::st :
                                    cyclestokens::UVMap;

    for (bNode *node = (bNode *) material->nodetree->nodes.first; node; node = node->next) {
      if (node->type == SH_NODE_BSDF_PRINCIPLED || node->type == SH_NODE_BSDF_DIFFUSE) {
        // We only handle the first instance of matching BSDF
        // USD Preview surface has no concept of layering materials

        pxr::UsdShadeShader previewSurface = create_usd_preview_shader_node(
          usd_export_context_, usd_material, node->name, node->type, node, export_animated_textures,
          anim_tex_start, anim_tex_end, current_frame);

        // @TODO: Maybe use this call: bNodeSocket *in_sock = nodeFindSocket(node, SOCK_IN, "Base
        // Color");
        for (bNodeSocket *sock = (bNodeSocket *) node->inputs.first; sock; sock = sock->next) {
          bNode *found_node = NULL;
          pxr::UsdShadeShader created_shader;

          if (strncmp(sock->name, "Base Color", 64) == 0 || strncmp(sock->name, "Color", 64) == 0) {
            // -- Base Color

            found_node = traverse_channel(sock);
            if (found_node) {  // Create connection
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::diffuse_color, pxr::SdfValueTypeNames->Float3)
                .ConnectToSource(created_shader, usdtokens::rgb);
            } 
            else {  // Set hardcoded value
              bNodeSocketValueRGBA *socket_data = (bNodeSocketValueRGBA *) sock->default_value;
              previewSurface.CreateInput(usdtokens::diffuse_color, pxr::SdfValueTypeNames->Float3)
                .Set(pxr::VtValue(pxr::GfVec3f(
                  socket_data->value[0], socket_data->value[1], socket_data->value[2])));
            }
          } 
          else if (strncmp(sock->name, "Roughness", 64) == 0) {
            // -- Roughness

            found_node = traverse_channel(sock);
            if (found_node) {  // Create connection
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::roughness, pxr::SdfValueTypeNames->Float)
                .ConnectToSource(created_shader, usdtokens::r);
            } 
            else {  // Set hardcoded value
              bNodeSocketValueFloat *socket_data = (bNodeSocketValueFloat *) sock->default_value;
              previewSurface.CreateInput(usdtokens::roughness, pxr::SdfValueTypeNames->Float)
                .Set(pxr::VtValue(socket_data->value));
            }
          } 
          else if (strncmp(sock->name, "Metallic", 64) == 0) {
            // -- Metallic

            found_node = traverse_channel(sock);
            if (found_node) {  // Set hardcoded value
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::metallic, pxr::SdfValueTypeNames->Float)
                .ConnectToSource(created_shader, usdtokens::r);
            } 
            else {  // Set hardcoded value
              bNodeSocketValueFloat *socket_data = (bNodeSocketValueFloat *) sock->default_value;
              previewSurface.CreateInput(usdtokens::metallic, pxr::SdfValueTypeNames->Float)
                .Set(pxr::VtValue(socket_data->value));
            }
          } 
          else if (strncmp(sock->name, "Specular", 64) == 0) {
            // -- Specular

            found_node = traverse_channel(sock);
            if (found_node) {  // Set hardcoded value
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::specular, pxr::SdfValueTypeNames->Float)
                .ConnectToSource(created_shader, usdtokens::r);
            } 
            else {  // Set hardcoded value
              bNodeSocketValueFloat *socket_data = (bNodeSocketValueFloat *) sock->default_value;
              previewSurface.CreateInput(usdtokens::specular, pxr::SdfValueTypeNames->Float)
                .Set(pxr::VtValue(socket_data->value));
            }
          } 
          else if (strncmp(sock->name, "Transmission", 64) == 0) {
            // -- Transmission
            // @TODO: We might need to check this, could need one minus

            found_node = traverse_channel(sock);
            if (found_node) {  // Set hardcoded value
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::opacity, pxr::SdfValueTypeNames->Float)
                .ConnectToSource(created_shader, usdtokens::r);
            } 
            else {  // Set hardcoded value
              bNodeSocketValueFloat *socket_data = (bNodeSocketValueFloat *) sock->default_value;
              previewSurface.CreateInput(usdtokens::opacity, pxr::SdfValueTypeNames->Float)
                .Set(pxr::VtValue(1.0f - socket_data->value));
            }
          } 
          else if (strncmp(sock->name, "IOR", 64) == 0) {
            // -- Specular
            // @TODO: We assume no input connection

            // Set hardcoded value
            bNodeSocketValueFloat *socket_data = (bNodeSocketValueFloat *) sock->default_value;
            previewSurface.CreateInput(usdtokens::ior, pxr::SdfValueTypeNames->Float)
              .Set(pxr::VtValue(socket_data->value));
          } 
          else if (strncmp(sock->name, "Normal", 64) == 0) {
            // -- Normal
            // @TODO: We assume no default value

            found_node = traverse_channel(sock);
            if (found_node) {
              created_shader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, found_node->name, found_node->type, found_node,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              previewSurface.CreateInput(usdtokens::normal, pxr::SdfValueTypeNames->Float)
                .ConnectToSource(created_shader, usdtokens::rgb);
            }
          }

          // If any input node has been found, look for uv node
          if (found_node) {

            bool found_uv_node = false;

            // Find UV Input
            for (bNodeSocket *sock = (bNodeSocket *) found_node->inputs.first; sock;
                 sock = sock->next) {
              if (sock == nullptr)
                continue;
              if (sock->link == nullptr)
                continue;

              if (strncmp(sock->name, "Vector", 64) != 0)
                continue;
              // bNode *uvNode = sock->link->fromnode;
              bNode *uvNode = traverse_channel(sock, SH_NODE_TEX_COORD);
              if (uvNode == nullptr)
                uvNode = traverse_channel(sock, SH_NODE_UVMAP);

              if (uvNode == NULL)
                continue;

              pxr::UsdShadeShader uvShader = create_usd_preview_shader_node(
                usd_export_context_, usd_material, uvNode->name, uvNode->type, uvNode,
                export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
              if (!uvShader.GetPrim().IsValid())
                continue;

              found_uv_node = true;

              if (uvNode->storage != NULL) {
                NodeShaderUVMap *uvmap = (NodeShaderUVMap *) uvNode->storage;
                if (uvmap) {

                  // We need to make valid here because actual uv primvar has been
                  std::string uv_set = pxr::TfMakeValidIdentifier(uvmap->uv_map);
                  if (usd_export_context_.export_params.convert_uv_to_st)
                    uv_set = "st";

                  uvShader.CreateInput(usdtokens::varname, pxr::SdfValueTypeNames->Token)
                    .Set(pxr::TfToken(uv_set));
                  created_shader.CreateInput(usdtokens::st, pxr::SdfValueTypeNames->Float2)
                    .ConnectToSource(uvShader, usdtokens::result);
                } 
                else {
                  uvShader.CreateInput(usdtokens::varname, pxr::SdfValueTypeNames->Token)
                    .Set(defaultUVSampler);
                  created_shader.CreateInput(usdtokens::st, pxr::SdfValueTypeNames->Float2)
                    .ConnectToSource(uvShader, usdtokens::result);
                }
              } 
              else {
                uvShader.CreateInput(usdtokens::varname, pxr::SdfValueTypeNames->Token)
                  .Set(defaultUVSampler);
                created_shader.CreateInput(usdtokens::st, pxr::SdfValueTypeNames->Float2)
                  .ConnectToSource(uvShader, usdtokens::result);
              }
            }

            if (!found_uv_node) {
              pxr::UsdShadeShader uvShader = create_usd_preview_shader_node(
                usd_export_context_,
                usd_material,
                const_cast<char *>("uvmap"),
                SH_NODE_TEX_COORD,
                NULL,
                export_animated_textures,
                anim_tex_start,
                anim_tex_end,
                current_frame);
              if (!uvShader.GetPrim().IsValid())
                continue;
              uvShader.CreateInput(usdtokens::varname, pxr::SdfValueTypeNames->Token)
                .Set(defaultUVSampler);
              created_shader.CreateInput(usdtokens::st, pxr::SdfValueTypeNames->Float2)
                .ConnectToSource(uvShader, usdtokens::result);
            }
          }
        }
        return;
      }
    }
    return;
  }
  catch (const MaterialExportException &exception) {
    std::string error_str = std::string("USD Export: ") + exception.get_error_str();
    WM_reportf(RPT_ERROR, error_str.c_str());
    throw std::runtime_error(error_str.c_str());
  }
}

void store_cycles_nodes(pxr::UsdStageRefPtr a_stage,
                        bNodeTree *ntree,
                        pxr::SdfPath shader_path,
                        bNode **material_out,
                        const bool a_asOvers,
                        const bool export_animated_textures,
                        const double anim_tex_start,
                        const double anim_tex_end,
                        const double current_frame)
{
  for (bNode *node = (bNode *) ntree->nodes.first; node; node = node->next) {

    // Blacklist certain nodes
    if (node->flag & NODE_MUTED)
      continue;

    if (node->type == SH_NODE_OUTPUT_MATERIAL) {
      *material_out = node;
      continue;
    }

    pxr::UsdShadeShader node_shader = create_cycles_shader_node(
      a_stage, shader_path, node, a_asOvers, export_animated_textures, anim_tex_start,
      anim_tex_end, current_frame);
  }
}

void link_cycles_nodes(pxr::UsdStageRefPtr a_stage,
                       pxr::UsdShadeMaterial &usd_material,
                       bNodeTree *ntree,
                       pxr::SdfPath shader_path,
                       bool a_asOvers)
{
  // for all links
  for (bNodeLink *link = (bNodeLink *)ntree->links.first; link; link = link->next) {
    bNode *from_node = link->fromnode, *to_node = link->tonode;
    bNodeSocket *from_sock = link->fromsock, *to_sock = link->tosock;

    // We should not encounter any groups, the node tree is pre-flattened.
    if (to_node->type == NODE_GROUP_OUTPUT)
      continue;

    if (from_node->type == NODE_GROUP_OUTPUT)
      continue;

    if (from_node == nullptr)
      continue;
    if (to_node == nullptr)
      continue;
    if (from_sock == nullptr)
      continue;
    if (to_sock == nullptr)
      continue;

    pxr::UsdShadeShader from_shader = pxr::UsdShadeShader::Define(
        a_stage,
        shader_path.AppendChild(pxr::TfToken(pxr::TfMakeValidIdentifier(from_node->name))));

    if (to_node->type == SH_NODE_OUTPUT_MATERIAL) {
      if (strncmp(to_sock->name, "Surface", 64) == 0) {
        if (strncmp(from_sock->name, "BSDF", 64) == 0)
          usd_material.CreateSurfaceOutput(cyclestokens::cycles)
              .ConnectToSource(from_shader, cyclestokens::bsdf);
        else
          usd_material.CreateSurfaceOutput(cyclestokens::cycles)
              .ConnectToSource(from_shader, cyclestokens::closure);
      }
      else if (strncmp(to_sock->name, "Volume", 64) == 0)
        usd_material.CreateVolumeOutput(cyclestokens::cycles)
            .ConnectToSource(from_shader, cyclestokens::bsdf);
      else if (strncmp(to_sock->name, "Displacement", 64) == 0)
        usd_material.CreateDisplacementOutput(cyclestokens::cycles)
            .ConnectToSource(from_shader, cyclestokens::vector);
      continue;
    }

    pxr::UsdShadeShader to_shader = pxr::UsdShadeShader::Define(
        a_stage, shader_path.AppendChild(pxr::TfToken(pxr::TfMakeValidIdentifier(to_node->name))));

    if (!from_shader.GetPrim().IsValid())
      continue;

    if (!to_shader.GetPrim().IsValid())
      continue;

    // TODO CLEAN
    std::string toName(to_sock->identifier);
    switch (to_node->type) {
      case SH_NODE_MATH: {
        if (toName == "Value_001")
          toName = "Value2";
        else
          toName = "Value1";
      } break;
      case SH_NODE_VECTOR_MATH: {
        if (toName == "Vector_001")
          toName = "Vector2";
        else if (toName == "Vector_002")
          toName = "Vector3";
        else
          toName = "Vector1";
      } break;
      case SH_NODE_ADD_SHADER:
      case SH_NODE_MIX_SHADER: {
        if (toName == "Shader_001")
          toName = "Closure2";
        else if (toName == "Shader")
          toName = "Closure1";
      } break;
      // Only needed in 4.21?
      case SH_NODE_CURVE_RGB: {
        if (toName == "Color")
          toName = "value";
      } break;
      case SH_NODE_SEPRGB: {
        if (toName == "Image")
          toName = "color";
      } break;
    }
    to_lower(toName);

    // TODO CLEAN
    std::string fromName(from_sock->identifier);
    switch (from_node->type) {
      case SH_NODE_ADD_SHADER:
      case SH_NODE_MIX_SHADER: {
        fromName = "Closure";
      } break;
      // Only needed in 4.21?
      case SH_NODE_CURVE_RGB: {
        if (fromName == "Color")
          fromName = "value";
      } break;
    }
    to_lower(fromName);

    to_shader
        .CreateInput(pxr::TfToken(pxr::TfMakeValidIdentifier(toName)),
                     pxr::SdfValueTypeNames->Float)
        .ConnectToSource(from_shader, pxr::TfToken(pxr::TfMakeValidIdentifier(fromName)));
  }
}

/* Entry point to create USD Shade Material network from Cycles Node Graph
 * This is needed for re-importing in to Blender and for HdCycles. */
void create_usd_cycles_material(pxr::UsdStageRefPtr a_stage,
                                Material *material,
                                pxr::UsdShadeMaterial &usd_material,
                                const bool a_asOvers,
                                const bool export_animated_textures,
                                const double anim_tex_start,
                                const double anim_tex_end,
                                const double current_frame)
{
  PointerRNA id_ptr;
  RNA_id_pointer_create(&material->id, &id_ptr);
  BL::Material b_mat(id_ptr);

  PointerRNA cmat = RNA_pointer_get(&b_mat.ptr, "cycles");
  int pass_id = b_mat.pass_index();
  bool use_mis = RNA_boolean_get(&cmat, "sample_as_light");
  bool use_transparent_shadow = RNA_boolean_get(&cmat, "use_transparent_shadow");
  bool heterogeneous_volume = !RNA_boolean_get(&cmat, "homogeneous_volume");
  int volume_sampling_method = RNA_enum_get(&cmat,"volume_sampling");
  int volume_interpolation_method = RNA_enum_get(&cmat,"volume_interpolation");
  float volume_step_rate = RNA_float_get(&cmat, "volume_step_rate");
  int displacement_method = RNA_enum_get(&cmat,"displacement_method");

  usd_material.GetPrim().CreateAttribute(cyclestokens::material::pass_id,
    pxr::SdfValueTypeNames->Int, false, pxr::SdfVariabilityUniform).Set(pass_id);

  usd_material.GetPrim().CreateAttribute(cyclestokens::material::use_mis,
    pxr::SdfValueTypeNames->Bool, false, pxr::SdfVariabilityVarying).Set(use_mis);

  usd_material.GetPrim().CreateAttribute(cyclestokens::material::use_transparent_shadow,
    pxr::SdfValueTypeNames->Bool, false, pxr::SdfVariabilityVarying).Set(use_transparent_shadow);

  usd_material.GetPrim().CreateAttribute(cyclestokens::material::heterogeneous_volume,
    pxr::SdfValueTypeNames->Bool, false, pxr::SdfVariabilityUniform).Set(heterogeneous_volume);

  usd_handle_material_enum(cyclestokens::material::volume_sampling_method,
    material_volume_sampling_method_conversion, usd_material, volume_sampling_method);

  usd_handle_material_enum(cyclestokens::material::volume_interpolation_method,
    material_volume_interpolation_method_conversion, usd_material, volume_interpolation_method);

  usd_material.GetPrim().CreateAttribute(cyclestokens::material::volume_step_rate,
    pxr::SdfValueTypeNames->Float, false, pxr::SdfVariabilityUniform).Set(volume_step_rate);

  usd_handle_material_enum(cyclestokens::material::displacement_method,
    material_displacement_method_conversion, usd_material, displacement_method);

  create_usd_cycles_material(a_stage, material->nodetree, usd_material, a_asOvers,
    export_animated_textures, anim_tex_start, anim_tex_end, current_frame);
}

void create_usd_cycles_material(pxr::UsdStageRefPtr a_stage,
                                bNodeTree *ntree,
                                pxr::UsdShadeMaterial &usd_material,
                                bool a_asOvers,
                                bool export_animated_textures,
                                double anim_tex_start,
                                double anim_tex_end,
                                double current_frame)
{
  try {
    bNode *output = nullptr;

    bNodeTree *localtree = ntreeLocalize(ntree);

    ntree_shader_groups_expand_inputs(localtree);

    ntree_shader_groups_flatten(localtree);

    localize(localtree, localtree);

    store_cycles_nodes(a_stage,
                       localtree,
                       usd_material.GetPath(),
                       &output,
                       a_asOvers,
                       export_animated_textures,
                       anim_tex_start,
                       anim_tex_end,
                       current_frame);
    link_cycles_nodes(a_stage,
                      usd_material,
                      localtree,
                      usd_material.GetPath(),
                      a_asOvers);

    ntreeFreeLocalTree(localtree);
    MEM_freeN(localtree);
  }
  catch (const MaterialExportException &exception) {
    std::string error_str = std::string("USD Export: ") + exception.get_error_str();
    WM_reportf(RPT_ERROR, error_str.c_str());
    throw std::runtime_error(error_str.c_str());
  }
}

/* Entry point to create USD Shade Material network from Blender "Viewport Display" */
void create_usd_viewport_material(USDExporterContext const &usd_export_context_,
                                  Material *material,
                                  pxr::UsdShadeMaterial &usd_material)
{
  // Construct the shader.
  pxr::SdfPath shader_path = usd_material.GetPath().AppendChild(usdtokens::preview_shader);
  pxr::UsdShadeShader shader = (usd_export_context_.export_params.export_as_overs) ?
                                   pxr::UsdShadeShader(
                                       usd_export_context_.stage->OverridePrim(shader_path)) :
                                   pxr::UsdShadeShader::Define(usd_export_context_.stage,
                                                               shader_path);
  shader.CreateIdAttr(pxr::VtValue(usdtokens::preview_surface));
  shader.CreateInput(usdtokens::diffuse_color, pxr::SdfValueTypeNames->Color3f)
      .Set(pxr::GfVec3f(material->r, material->g, material->b));
  shader.CreateInput(usdtokens::roughness, pxr::SdfValueTypeNames->Float).Set(material->roughness);
  shader.CreateInput(usdtokens::metallic, pxr::SdfValueTypeNames->Float).Set(material->metallic);

  // Connect the shader and the material together.
  usd_material.CreateSurfaceOutput().ConnectToSource(shader, usdtokens::surface);
}

}  // namespace USD