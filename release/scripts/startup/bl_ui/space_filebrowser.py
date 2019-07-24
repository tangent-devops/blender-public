# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>
from bpy.types import Header, Panel, Menu, UIList


class FILEBROWSER_HT_header(Header):
    bl_space_type = 'FILE_BROWSER'

    def draw(self, context):
        layout = self.layout

        st = context.space_data
        params = st.params

        if st.active_operator is None:
            layout.template_header()

        layout.menu("FILEBROWSER_MT_view")

        # can be None when save/reload with a file selector open
        if params:
            is_lib_browser = params.use_library_browsing

            layout.prop(params, "display_type", expand=True, text="")
            layout.prop(params, "sort_method", expand=True, text="")
            layout.prop(params, "show_hidden", text="", icon='FILE_HIDDEN')

            row = layout.row(align=True)
            row.prop(params, "show_details_size", text="Size")
            row.prop(params, "show_details_datetime", text="Date")

        layout.separator_spacer()

        layout.template_running_jobs()

        if params:
            layout.prop(params, "use_filter", text="", icon='FILTER')

            row = layout.row(align=True)
            row.active = params.use_filter
            row.prop(params, "use_filter_folder", text="")

            if params.filter_glob:
                # if st.active_operator and hasattr(st.active_operator, "filter_glob"):
                #     row.prop(params, "filter_glob", text="")
                row.label(text=params.filter_glob)
            else:
                row.prop(params, "use_filter_blender", text="")
                row.prop(params, "use_filter_backup", text="")
                row.prop(params, "use_filter_image", text="")
                row.prop(params, "use_filter_movie", text="")
                row.prop(params, "use_filter_script", text="")
                row.prop(params, "use_filter_font", text="")
                row.prop(params, "use_filter_sound", text="")
                row.prop(params, "use_filter_text", text="")

            if is_lib_browser:
                row.prop(params, "use_filter_blendid", text="")
                if params.use_filter_blendid:
                    row.separator()
                    row.prop(params, "filter_id_category", text="")

            row.separator()
            row.prop(params, "filter_search", text="", icon='VIEWZOOM')


class FILEBROWSER_PT_filter(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'UI'
    bl_label = "Filter"
    bl_options = {'HIDDEN'}

    @classmethod
    def poll(cls, context):
        # can be None when save/reload with a file selector open
        return context.space_data.params is not None

    def draw(self, context):
        layout = self.layout

        space = context.space_data
        params = space.params
        is_lib_browser = params.use_library_browsing

        layout.label(text="Display Type:")
        layout.prop(params, "display_type", expand=True, text="")

        layout.label(text="Sort By:")
        layout.prop(params, "sort_method", expand=True, text="")

        layout.separator()

        layout.prop(params, "show_hidden")

        layout.separator()

        layout.label(text="Show Details:")
        row = layout.row(align=True)
        row.prop(params, "show_details_size", text="Size")
        row.prop(params, "show_details_datetime", text="Date")

        layout.separator()

        row = layout.row(align=True)
        row.prop(params, "use_filter", text="", toggle=0)
        row.label(text="Filter:")

        col = layout.column()
        col.active = params.use_filter

        row = col.row()
        row.label(icon='FILE_FOLDER')
        row.prop(params, "use_filter_folder", text="Folders", toggle=0)

        if params.filter_glob:
            col.label(text=params.filter_glob)
        else:
            row = col.row()
            row.label(icon='FILE_BLEND')
            row.prop(params, "use_filter_blender", text=".blend Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_BACKUP')
            row.prop(params, "use_filter_backup", text="Backup .blend Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_IMAGE')
            row.prop(params, "use_filter_image", text="Image Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_MOVIE')
            row.prop(params, "use_filter_movie", text="Movie Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_SCRIPT')
            row.prop(params, "use_filter_script", text="Script Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_FONT')
            row.prop(params, "use_filter_font", text="Font Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_SOUND')
            row.prop(params, "use_filter_sound", text="Sound Files", toggle=0)
            row = col.row()
            row.label(icon='FILE_TEXT')
            row.prop(params, "use_filter_text", text="Text Files", toggle=0)

        col.separator()

        if is_lib_browser:
            row = col.row()
            row.label(icon='BLANK1')  # Indentation
            row.prop(params, "use_filter_blendid", text="Blender IDs", toggle=0)
            if params.use_filter_blendid:
                row = col.row()
                row.label(icon='BLANK1')  # Indentation
                row.prop(params, "filter_id_category", text="")

                col.separator()

        col.prop(params, "filter_search", text="", icon='VIEWZOOM')

        layout.separator()

        layout.label(text="Display Size:")
        layout.prop(params, "display_size", text="")
        layout.label(text="Recursion Level:")
        layout.prop(params, "recursion_level", text="")


class FILEBROWSER_UL_dir(UIList):
    def draw_item(self, _context, layout, _data, item, icon, _active_data, active_propname, _index):
        direntry = item
        # space = context.space_data
        icon = 'NONE'
        if active_propname == "system_folders_active":
            icon = 'DISK_DRIVE'
        if active_propname == "system_bookmarks_active":
            icon = 'BOOKMARKS'
        if active_propname == "bookmarks_active":
            icon = 'BOOKMARKS'
        if active_propname == "recent_folders_active":
            icon = 'FILE_FOLDER'

        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row = layout.row(align=True)
            row.enabled = direntry.is_valid
            # Non-editable entries would show grayed-out, which is bad in this specific case, so switch to mere label.
            if direntry.is_property_readonly("name"):
                row.label(text=direntry.name, icon=icon)
            else:
                row.prop(direntry, "name", text="", emboss=False, icon=icon)

        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.prop(direntry, "path", text="")


class FILEBROWSER_PT_bookmarks_volumes(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_options = {'DEFAULT_CLOSED'}
    bl_category = "Bookmarks"
    bl_label = "Volumes"

    def draw(self, context):
        layout = self.layout
        space = context.space_data

        if space.system_folders:
            row = layout.row()
            row.template_list("FILEBROWSER_UL_dir", "system_folders", space, "system_folders",
                              space, "system_folders_active", item_dyntip_propname="path", rows=1, maxrows=10)


class FILEBROWSER_PT_bookmarks_system(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Bookmarks"
    bl_label = "System"

    @classmethod
    def poll(cls, context):
        return not context.preferences.filepaths.hide_system_bookmarks

    def draw(self, context):
        layout = self.layout
        space = context.space_data

        if space.system_bookmarks:
            row = layout.row()
            row.template_list("FILEBROWSER_UL_dir", "system_bookmarks", space, "system_bookmarks",
                              space, "system_bookmarks_active", item_dyntip_propname="path", rows=1, maxrows=10)


class FILEBROWSER_MT_bookmarks_context_menu(Menu):
    bl_label = "Bookmarks Specials"

    def draw(self, _context):
        layout = self.layout
        layout.operator("file.bookmark_cleanup", icon='X', text="Cleanup")

        layout.separator()
        layout.operator("file.bookmark_move", icon='TRIA_UP_BAR', text="Move To Top").direction = 'TOP'
        layout.operator("file.bookmark_move", icon='TRIA_DOWN_BAR', text="Move To Bottom").direction = 'BOTTOM'


class FILEBROWSER_PT_bookmarks_favorites(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Bookmarks"
    bl_label = "Favorites"

    def draw(self, context):
        layout = self.layout
        space = context.space_data

        if space.bookmarks:
            row = layout.row()
            num_rows = len(space.bookmarks)
            row.template_list("FILEBROWSER_UL_dir", "bookmarks", space, "bookmarks",
                              space, "bookmarks_active", item_dyntip_propname="path",
                              rows=(2 if num_rows < 2 else 4), maxrows=10)

            col = row.column(align=True)
            col.operator("file.bookmark_add", icon='ADD', text="")
            col.operator("file.bookmark_delete", icon='REMOVE', text="")
            col.menu("FILEBROWSER_MT_bookmarks_context_menu", icon='DOWNARROW_HLT', text="")

            if num_rows > 1:
                col.separator()
                col.operator("file.bookmark_move", icon='TRIA_UP', text="").direction = 'UP'
                col.operator("file.bookmark_move", icon='TRIA_DOWN', text="").direction = 'DOWN'
        else:
            layout.operator("file.bookmark_add", icon='ADD')


class FILEBROWSER_PT_bookmarks_recents(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Bookmarks"
    bl_label = "Recents"

    @classmethod
    def poll(cls, context):
        return not context.preferences.filepaths.hide_recent_locations

    def draw(self, context):
        layout = self.layout
        space = context.space_data

        if space.recent_folders:
            row = layout.row()
            row.template_list("FILEBROWSER_UL_dir", "recent_folders", space, "recent_folders",
                              space, "recent_folders_active", item_dyntip_propname="path", rows=1, maxrows=10)

            col = row.column(align=True)
            col.operator("file.reset_recent", icon='X', text="")


class FILEBROWSER_PT_advanced_filter(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Filter"
    bl_label = "Advanced Filter"

    @classmethod
    def poll(cls, context):
        # only useful in append/link (library) context currently...
        return context.space_data.params.use_library_browsing

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        params = space.params

        if params and params.use_library_browsing:
            layout.prop(params, "use_filter_blendid")
            if params.use_filter_blendid:
                layout.separator()
                col = layout.column()
                col.prop(params, "filter_id")


class FILEBROWSER_PT_directory_path(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'UI'
    bl_label = "Directory Path"
    bl_options = {'HIDE_HEADER'}

    def is_header_visible(self, context):
        for region in context.area.regions:
            if region.type == 'HEADER' and region.height <= 1:
                return False

        return True

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        params = space.params

        layout.scale_x = 1.3
        layout.scale_y = 1.3

        row = layout.row()

        subrow = row.row(align=True)
        subrow.operator("file.previous", text="", icon='BACK')
        subrow.operator("file.next", text="", icon='FORWARD')
        subrow.operator("file.parent", text="", icon='FILE_PARENT')
        subrow.operator("file.refresh", text="", icon='FILE_REFRESH')
        # TODO proper directory input text field

        subrow = row.row()
        subrow.prop(params, "directory", text="")

        # TODO down triangle only created for UI_LAYOUT_HEADER
        if self.is_header_visible(context) is False:
            row.popover(
                panel="FILEBROWSER_PT_filter",
                text="",
                icon='FILTER',
            )

        subrow = row.row(align=True)
        subrow.operator("file.directory_new", icon='NEWFOLDER', text="")


class FILEBROWSER_PT_file_operation(Panel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'EXECUTE'
    bl_label = "Execute File Operation"
    bl_options = {'HIDE_HEADER'}

    @classmethod
    def poll(cls, context):
        return context.space_data.active_operator

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        params = space.params

        layout.scale_x = 1.3
        layout.scale_y = 1.3

        row = layout.row()
        sub = row.row()
        sub.prop(params, "filename", text="")
        sub = row.row()
        sub.ui_units_x = 5
        # TODO change to "Open Directory"/"Parent Directory" based on highlight.
        sub.operator("FILE_OT_execute", text=params.title)
        sub.operator("FILE_OT_cancel", text="Cancel")


class FILEBROWSER_MT_view(Menu):
    bl_label = "View"

    def draw(self, context):
        layout = self.layout
        st = context.space_data
        params = st.params

        layout.prop(st, "show_region_toolbar")
        layout.prop(st, "show_region_ui", text="File Path")

        layout.separator()

        layout.prop_menu_enum(params, "display_size")
        layout.prop_menu_enum(params, "recursion_level")

        layout.separator()

        layout.menu("INFO_MT_area")


classes = (
    FILEBROWSER_HT_header,
    FILEBROWSER_PT_filter,
    FILEBROWSER_UL_dir,
    FILEBROWSER_PT_bookmarks_volumes,
    FILEBROWSER_PT_bookmarks_system,
    FILEBROWSER_MT_bookmarks_context_menu,
    FILEBROWSER_PT_bookmarks_favorites,
    FILEBROWSER_PT_bookmarks_recents,
    FILEBROWSER_PT_advanced_filter,
    FILEBROWSER_PT_directory_path,
    FILEBROWSER_PT_file_operation,
    FILEBROWSER_MT_view,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
