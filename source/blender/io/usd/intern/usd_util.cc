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
#include "usd_util.h"
#include "usd_hierarchy_iterator.h"

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
#include "BKE_light.h"
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

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/curves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdLux/light.h>

namespace USD {

/* ===== Functions copied from inacessible source file
 * blender/nodes/shader/node_shader_tree.c */

void localize(bNodeTree *localtree, bNodeTree *UNUSED(ntree))
{
  bNode *node, *node_next;

  /* replace muted nodes and reroute nodes by internal links */
  for (node = (bNode *)localtree->nodes.first; node; node = node_next) {
    node_next = node->next;

    if (node->flag & NODE_MUTED || node->type == NODE_REROUTE) {
      nodeInternalRelink(localtree, node);
      ntreeFreeLocalNode(localtree, node);
    }
  }
}

/* Find an output node of the shader tree.
 *
 * NOTE: it will only return output which is NOT in the group, which isn't how
 * render engines works but it's how the GPU shader compilation works. This we
 * can change in the future and make it a generic function, but for now it stays
 * private here.
 */
bNode *ntreeShaderOutputNode(bNodeTree *ntree, int target)
{
  /* Make sure we only have single node tagged as output. */
  ntreeSetOutput(ntree);

  /* Find output node that matches type and target. If there are
   * multiple, we prefer exact target match and active nodes. */
  bNode *output_node = NULL;

  for (bNode *node = (bNode *)ntree->nodes.first; node; node = node->next) {
    if (!ELEM(node->type, SH_NODE_OUTPUT_MATERIAL, SH_NODE_OUTPUT_WORLD, SH_NODE_OUTPUT_LIGHT)) {
      continue;
    }

    if (node->custom1 == SHD_OUTPUT_ALL) {
      if (output_node == NULL) {
        output_node = node;
      }
      else if (output_node->custom1 == SHD_OUTPUT_ALL) {
        if ((node->flag & NODE_DO_OUTPUT) && !(output_node->flag & NODE_DO_OUTPUT)) {
          output_node = node;
        }
      }
    }
    else if (node->custom1 == target) {
      if (output_node == NULL) {
        output_node = node;
      }
      else if (output_node->custom1 == SHD_OUTPUT_ALL) {
        output_node = node;
      }
      else if ((node->flag & NODE_DO_OUTPUT) && !(output_node->flag & NODE_DO_OUTPUT)) {
        output_node = node;
      }
    }
  }

  return output_node;
}

/* Find socket with a specified identifier. */
bNodeSocket *ntree_shader_node_find_socket(ListBase *sockets, const char *identifier)
{
  for (bNodeSocket *sock = (bNodeSocket *)sockets->first; sock != NULL; sock = sock->next) {
    if (STREQ(sock->identifier, identifier)) {
      return sock;
    }
  }
  return NULL;
}

/* Find input socket with a specified identifier. */
bNodeSocket *ntree_shader_node_find_input(bNode *node, const char *identifier)
{
  return ntree_shader_node_find_socket(&node->inputs, identifier);
}

/* Find output socket with a specified identifier. */
bNodeSocket *ntree_shader_node_find_output(bNode *node, const char *identifier)
{
  return ntree_shader_node_find_socket(&node->outputs, identifier);
}

/* Return true on success. */
bool ntree_shader_expand_socket_default(bNodeTree *localtree, bNode *node, bNodeSocket *socket)
{
  bNode *value_node;
  bNodeSocket *value_socket;
  bNodeSocketValueVector *src_vector;
  bNodeSocketValueRGBA *src_rgba, *dst_rgba;
  bNodeSocketValueFloat *src_float, *dst_float;
  bNodeSocketValueInt *src_int;

  switch (socket->type) {
    case SOCK_VECTOR:
      value_node = nodeAddStaticNode(NULL, localtree, SH_NODE_RGB);
      value_socket = ntree_shader_node_find_output(value_node, "Color");
      BLI_assert(value_socket != NULL);
      src_vector = (bNodeSocketValueVector *)socket->default_value;
      dst_rgba = (bNodeSocketValueRGBA *)value_socket->default_value;
      copy_v3_v3(dst_rgba->value, src_vector->value);
      dst_rgba->value[3] = 1.0f; /* should never be read */
      break;
    case SOCK_RGBA:
      value_node = nodeAddStaticNode(NULL, localtree, SH_NODE_RGB);
      value_socket = ntree_shader_node_find_output(value_node, "Color");
      BLI_assert(value_socket != NULL);
      src_rgba = (bNodeSocketValueRGBA *)socket->default_value;
      dst_rgba = (bNodeSocketValueRGBA *)value_socket->default_value;
      copy_v4_v4(dst_rgba->value, src_rgba->value);
      break;
    case SOCK_INT:
      /* HACK: Support as float. */
      value_node = nodeAddStaticNode(NULL, localtree, SH_NODE_VALUE);
      value_socket = ntree_shader_node_find_output(value_node, "Value");
      BLI_assert(value_socket != NULL);
      src_int = (bNodeSocketValueInt *)socket->default_value;
      dst_float = (bNodeSocketValueFloat *)value_socket->default_value;
      dst_float->value = (float)(src_int->value);
      break;
    case SOCK_FLOAT:
      value_node = nodeAddStaticNode(NULL, localtree, SH_NODE_VALUE);
      value_socket = ntree_shader_node_find_output(value_node, "Value");
      BLI_assert(value_socket != NULL);
      src_float = (bNodeSocketValueFloat *)socket->default_value;
      dst_float = (bNodeSocketValueFloat *)value_socket->default_value;
      dst_float->value = src_float->value;
      break;
    default:
      return false;
  }
  nodeAddLink(localtree, value_node, value_socket, node, socket);
  return true;
}

void ntree_shader_unlink_hidden_value_sockets(bNode *group_node, bNodeSocket *isock)
{
  bNodeTree *group_ntree = (bNodeTree *)group_node->id;
  bNode *node;
  bool removed_link = false;

  for (node = (bNode *)group_ntree->nodes.first; node; node = node->next) {
    for (bNodeSocket *sock = (bNodeSocket *)node->inputs.first; sock; sock = sock->next) {
      if ((sock->flag & SOCK_HIDE_VALUE) == 0) {
        continue;
      }
      /* If socket is linked to a group input node and sockets id match. */
      if (sock && sock->link && sock->link->fromnode->type == NODE_GROUP_INPUT) {
        if (STREQ(isock->identifier, sock->link->fromsock->identifier)) {
          nodeRemLink(group_ntree, sock->link);
          removed_link = true;
        }
      }
    }
  }

  if (removed_link) {
    ntreeUpdateTree(G.main, group_ntree);
  }
}

/* Node groups once expanded looses their input sockets values.
 * To fix this, link value/rgba nodes into the sockets and copy the group sockets values. */
void ntree_shader_groups_expand_inputs(bNodeTree *localtree)
{
  bool link_added = false;

  LISTBASE_FOREACH (bNode *, node, &localtree->nodes) {
    const bool is_group = ELEM(node->type, NODE_GROUP, NODE_CUSTOM_GROUP) && (node->id != NULL);
    const bool is_group_output = node->type == NODE_GROUP_OUTPUT && (node->flag & NODE_DO_OUTPUT);

    if (is_group) {
      /* Do it recursively. */
      ntree_shader_groups_expand_inputs((bNodeTree *)node->id);
    }

    if (is_group || is_group_output) {
      LISTBASE_FOREACH (bNodeSocket *, socket, &node->inputs) {
        if (socket->link != NULL) {
          bNodeLink *link = socket->link;
          /* Fix the case where the socket is actually converting the data. (see T71374)
           * We only do the case of lossy conversion to float.*/
          if ((socket->type == SOCK_FLOAT) && (link->fromsock->type != link->tosock->type)) {
            bNode *tmp = nodeAddStaticNode(NULL, localtree, SH_NODE_RGBTOBW);
            nodeAddLink(
                localtree, link->fromnode, link->fromsock, tmp, (bNodeSocket *)tmp->inputs.first);
            nodeAddLink(localtree, tmp, (bNodeSocket *)tmp->outputs.first, node, socket);
          }
          continue;
        }

        if (is_group) {
          /* Detect the case where an input is plugged into a hidden value socket.
           * In this case we should just remove the link to trigger the socket default override. */
          ntree_shader_unlink_hidden_value_sockets(node, socket);
        }

        if (ntree_shader_expand_socket_default(localtree, node, socket)) {
          link_added = true;
        }
      }
    }
  }

  if (link_added) {
    ntreeUpdateTree(G.main, localtree);
  }
}

void flatten_group_do(bNodeTree *ntree, bNode *gnode)
{
  bNodeLink *link, *linkn, *tlink;
  bNode *node, *nextnode;
  bNodeTree *ngroup;
  LinkNode *group_interface_nodes = NULL;

  ngroup = (bNodeTree *)gnode->id;

  /* Add the nodes into the ntree */
  for (node = (bNode *)ngroup->nodes.first; node; node = nextnode) {
    nextnode = node->next;
    /* Remove interface nodes.
     * This also removes remaining links to and from interface nodes.
     * We must delay removal since sockets will reference this node. see: T52092 */
    if (ELEM(node->type, NODE_GROUP_INPUT, NODE_GROUP_OUTPUT)) {
      BLI_linklist_prepend(&group_interface_nodes, node);
    }
    /* migrate node */
    BLI_remlink(&ngroup->nodes, node);
    BLI_addtail(&ntree->nodes, node);
    /* ensure unique node name in the node tree */
    /* This is very slow and it has no use for GPU nodetree. (see T70609) */
    nodeUniqueName(ntree, node);
  }

  /* Save first and last link to iterate over flattened group links. */
  bNodeLink *glinks_first = (bNodeLink *)ntree->links.last;

  /* Add internal links to the ntree */
  for (link = (bNodeLink *)ngroup->links.first; link; link = linkn) {
    linkn = link->next;
    BLI_remlink(&ngroup->links, link);
    BLI_addtail(&ntree->links, link);
  }

  bNodeLink *glinks_last = (bNodeLink *)ntree->links.last;

  /* restore external links to and from the gnode */
  if (glinks_first != NULL) {
    /* input links */
    for (link = (bNodeLink *)glinks_first->next; link != glinks_last->next; link = link->next) {
      if (link->fromnode->type == NODE_GROUP_INPUT) {
        const char *identifier = link->fromsock->identifier;
        /* find external links to this input */
        for (tlink = (bNodeLink *)ntree->links.first; tlink != glinks_first->next;
             tlink = tlink->next) {
          if (tlink->tonode == gnode && STREQ(tlink->tosock->identifier, identifier)) {
            nodeAddLink(ntree, tlink->fromnode, tlink->fromsock, link->tonode, link->tosock);
          }
        }
      }
    }
    /* Also iterate over the new links to cover passthrough links. */
    glinks_last = (bNodeLink *)ntree->links.last;
    /* output links */
    for (tlink = (bNodeLink *)ntree->links.first; tlink != glinks_first->next;
         tlink = tlink->next) {
      if (tlink->fromnode == gnode) {
        const char *identifier = tlink->fromsock->identifier;
        /* find internal links to this output */
        for (link = glinks_first->next; link != glinks_last->next; link = link->next) {
          /* only use active output node */
          if (link->tonode->type == NODE_GROUP_OUTPUT && (link->tonode->flag & NODE_DO_OUTPUT)) {
            if (STREQ(link->tosock->identifier, identifier)) {
              nodeAddLink(ntree, link->fromnode, link->fromsock, tlink->tonode, tlink->tosock);
            }
          }
        }
      }
    }
  }

  while (group_interface_nodes) {
    node = (bNode *)BLI_linklist_pop(&group_interface_nodes);
    ntreeFreeLocalNode(ntree, node);
  }

  ntree->update |= NTREE_UPDATE_NODES | NTREE_UPDATE_LINKS;
}

/* Flatten group to only have a simple single tree */
void ntree_shader_groups_flatten(bNodeTree *localtree)
{
  /* This is effectively recursive as the flattened groups will add
   * nodes at the end of the list, which will also get evaluated. */
  for (bNode *node = (bNode *)localtree->nodes.first, *node_next; node; node = node_next) {
    if (ELEM(node->type, NODE_GROUP, NODE_CUSTOM_GROUP) && node->id != NULL) {
      flatten_group_do(localtree, node);
      /* Continue even on new flattened nodes. */
      node_next = node->next;
      /* delete the group instance and its localtree. */
      bNodeTree *ngroup = (bNodeTree *)node->id;
      ntreeFreeLocalNode(localtree, node);
      ntreeFreeTree(ngroup);
      MEM_freeN(ngroup);
    }
    else {
      node_next = node->next;
    }
  }

  ntreeUpdateTree(G.main, localtree);
}

/* ===== USD/Blender Material Interchange ===== */

/* Gets a NodeTexImage's filepath */
std::string get_node_tex_image_filepath(bNode *node, const int frame_num)
{
  NodeTexImage *tex_original = (NodeTexImage *)node->storage;

  Image *ima = (Image *)node->id;

  if (!ima)
    return "";
  if (sizeof(ima->name) == 0)
    return "";

  char filepath[1024] = "\0";

  strncpy(filepath, ima->name, sizeof(ima->name));

  BKE_image_user_file_path(&tex_original->iuser, ima, filepath);

  BLI_str_replace_char(filepath, '\\', '/');

  if (ima->source == IMA_SRC_TILED) {
    char head[FILE_MAX], tail[FILE_MAX];
    unsigned short numlen;

    BLI_path_sequence_decode(filepath, head, tail, &numlen);
    sprintf(filepath, "%s%s%s", head, "<UDIM>", tail);
  }
  else if (ima->source == IMA_SRC_SEQUENCE && frame_num != INT_MIN) {
    char head[FILE_MAX], tail[FILE_MAX];
    unsigned short numlen;

    BLI_path_sequence_decode(filepath, head, tail, &numlen);
    BLI_path_sequence_encode(filepath, head, tail, numlen, frame_num);
  }

  return std::string(filepath);
}

}  // Namespace USD