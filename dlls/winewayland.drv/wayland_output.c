/*
 * Wayland output handling
 *
 * Copyright (c) 2020 Alexandros Frantzis for Collabora Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include "waylanddrv.h"

#include "wine/debug.h"

#include <stdlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

static const int32_t default_refresh = 60000;
static uint32_t next_output_id = 0;

/**********************************************************************
 *          Output handling
 */

/* Compare a mode rb_tree key with the provided mode rb_entry and return -1 if
 * the key compares less than the entry, 0 if the key compares equal to the
 * entry, and 1 if the key compares greater than the entry.
 *
 * The comparison is based on comparing the width, height and refresh in that
 * order. */
static int wayland_output_mode_cmp_rb(const void *key,
                                      const struct rb_entry *entry)
{
    const struct wayland_output_mode *key_mode = key;
    const struct wayland_output_mode *entry_mode =
        RB_ENTRY_VALUE(entry, const struct wayland_output_mode, entry);

    if (key_mode->width < entry_mode->width) return -1;
    if (key_mode->width > entry_mode->width) return 1;
    if (key_mode->height < entry_mode->height) return -1;
    if (key_mode->height > entry_mode->height) return 1;
    if (key_mode->refresh < entry_mode->refresh) return -1;
    if (key_mode->refresh > entry_mode->refresh) return 1;

    return 0;
}

static void wayland_output_add_mode(struct wayland_output *output,
                                    int32_t width, int32_t height,
                                    int32_t refresh, BOOL current)
{
    struct rb_entry *mode_entry;
    struct wayland_output_mode *mode;
    struct wayland_output_mode key =
    {
        .width = width,
        .height = height,
        .refresh = refresh,
    };

    mode_entry = rb_get(&output->modes, &key);
    if (mode_entry)
    {
        mode = RB_ENTRY_VALUE(mode_entry, struct wayland_output_mode, entry);
    }
    else
    {
        mode = calloc(1, sizeof(*mode));
        if (!mode)
        {
            ERR("Failed to allocate space for wayland_output_mode\n");
            return;
        }
        mode->width = width;
        mode->height = height;
        mode->refresh = refresh;
        rb_put(&output->modes, mode, &mode->entry);
    }

    if (current) output->current_mode = mode;
}

static void wayland_output_done(struct wayland_output *output)
{
    struct wayland_output_mode *mode;

    TRACE("name=%s logical=%d,%d+%dx%d\n",
          output->name, output->logical_x, output->logical_y,
          output->logical_w, output->logical_h);

    RB_FOR_EACH_ENTRY(mode, &output->modes, struct wayland_output_mode, entry)
    {
        TRACE("mode %dx%d @ %d %s\n",
              mode->width, mode->height, mode->refresh,
              output->current_mode == mode ? "*" : "");
    }
}

static void output_handle_geometry(void *data, struct wl_output *wl_output,
                                   int32_t x, int32_t y,
                                   int32_t physical_width, int32_t physical_height,
                                   int32_t subpixel,
                                   const char *make, const char *model,
                                   int32_t output_transform)
{
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh)
{
    struct wayland_output *output = data;

    /* Windows apps don't expect a zero refresh rate, so use a default value. */
    if (refresh == 0) refresh = default_refresh;

    wayland_output_add_mode(output, width, height, refresh,
                            (flags & WL_OUTPUT_MODE_CURRENT));
}

static void output_handle_done(void *data, struct wl_output *wl_output)
{
    struct wayland_output *output = data;

    if (!output->zxdg_output_v1 ||
        zxdg_output_v1_get_version(output->zxdg_output_v1) >= 3)
    {
        wayland_output_done(output);
    }
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t scale)
{
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale
};

static void zxdg_output_v1_handle_logical_position(void *data,
                                                   struct zxdg_output_v1 *zxdg_output_v1,
                                                   int32_t x,
                                                   int32_t y)
{
    struct wayland_output *output = data;
    TRACE("logical_x=%d logical_y=%d\n", x, y);
    output->logical_x = x;
    output->logical_y = y;
}

static void zxdg_output_v1_handle_logical_size(void *data,
                                               struct zxdg_output_v1 *zxdg_output_v1,
                                               int32_t width,
                                               int32_t height)
{
    struct wayland_output *output = data;
    TRACE("logical_w=%d logical_h=%d\n", width, height);
    output->logical_w = width;
    output->logical_h = height;
}

static void zxdg_output_v1_handle_done(void *data,
                                       struct zxdg_output_v1 *zxdg_output_v1)
{
    if (zxdg_output_v1_get_version(zxdg_output_v1) < 3)
    {
        struct wayland_output *output = data;
        wayland_output_done(output);
    }
}

static void zxdg_output_v1_handle_name(void *data,
                                       struct zxdg_output_v1 *zxdg_output_v1,
                                       const char *name)
{
    struct wayland_output *output = data;

    free(output->name);
    output->name = strdup(name);
}

static void zxdg_output_v1_handle_description(void *data,
                                              struct zxdg_output_v1 *zxdg_output_v1,
                                              const char *description)
{
}

static const struct zxdg_output_v1_listener zxdg_output_v1_listener = {
    zxdg_output_v1_handle_logical_position,
    zxdg_output_v1_handle_logical_size,
    zxdg_output_v1_handle_done,
    zxdg_output_v1_handle_name,
    zxdg_output_v1_handle_description,
};

/**********************************************************************
 *          wayland_output_create
 *
 *  Creates a wayland_output and adds it to the output list.
 */
BOOL wayland_output_create(uint32_t id, uint32_t version)
{
    struct wayland_output *output = calloc(1, sizeof(*output));
    int name_len;

    if (!output)
    {
        ERR("Failed to allocate space for wayland_output\n");
        goto err;
    }

    output->wl_output = wl_registry_bind(process_wayland->wl_registry, id,
                                         &wl_output_interface,
                                         version < 2 ? version : 2);
    output->global_id = id;
    wl_output_add_listener(output->wl_output, &output_listener, output);

    wl_list_init(&output->link);
    rb_init(&output->modes, wayland_output_mode_cmp_rb);

    /* Have a fallback while we don't have compositor given name. */
    name_len = snprintf(NULL, 0, "WaylandOutput%d", next_output_id);
    output->name = malloc(name_len + 1);
    if (output->name)
    {
        snprintf(output->name, name_len + 1, "WaylandOutput%d", next_output_id++);
    }
    else
    {
        ERR("Couldn't allocate space for output name\n");
        goto err;
    }

    if (process_wayland->zxdg_output_manager_v1)
        wayland_output_use_xdg_extension(output);

    wl_list_insert(process_wayland->output_list.prev, &output->link);

    return TRUE;

err:
    if (output) wayland_output_destroy(output);
    return FALSE;
}

static void wayland_output_mode_free_rb(struct rb_entry *entry, void *ctx)
{
    free(RB_ENTRY_VALUE(entry, struct wayland_output_mode, entry));
}

/**********************************************************************
 *          wayland_output_destroy
 *
 *  Destroys a wayland_output.
 */
void wayland_output_destroy(struct wayland_output *output)
{
    rb_destroy(&output->modes, wayland_output_mode_free_rb, NULL);
    wl_list_remove(&output->link);
    if (output->zxdg_output_v1)
        zxdg_output_v1_destroy(output->zxdg_output_v1);
    wl_output_destroy(output->wl_output);
    free(output->name);
    free(output);
}

/**********************************************************************
 *          wayland_output_use_xdg_extension
 *
 *  Use the zxdg_output_v1 extension to get output information.
 */
void wayland_output_use_xdg_extension(struct wayland_output *output)
{
    output->zxdg_output_v1 =
        zxdg_output_manager_v1_get_xdg_output(process_wayland->zxdg_output_manager_v1,
                                              output->wl_output);
    zxdg_output_v1_add_listener(output->zxdg_output_v1, &zxdg_output_v1_listener,
                                output);
}