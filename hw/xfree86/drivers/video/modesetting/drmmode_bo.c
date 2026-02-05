/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2026 stefan11111 <stefan11111@shitposting.expert>
 */

#include <stddef.h>
#include <stdint.h>

#include "dix-config.h"
#include "drmmode_bo.h"

#include "dix.h" /* ARRAY_SIZE() */

/**
 * Thin wrapper around gbm_bo_{create,map,unmap}
 * that creates and maps (if necessary) the "best"
 * buffer of a certain type that we can create.
 *
 * Any needed mapping is done when creating the buffer,
 * and unmapping is handeled automatically by the gbm
 * loader through the destroy_user_data callback.
 */

#ifndef GBM_BO_USE_LINEAR
#define GBM_BO_USE_LINEAR 0
#endif

#ifndef GBM_BO_USE_FRONT_RENDERING
#define GBM_BO_USE_FRONT_RENDERING 0
#endif

static void
destroy_user_data(struct gbm_bo *bo, void* _data)
{
    bo_user_data_t *data = _data;
    if (!data) {
        /* Should never happen */
        return;
    }

    if (data->map_data) {
        gbm_bo_unmap(bo, data->map_data);
    }
    free(data);
}

static uint32_t
get_opaque_format(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        return DRM_FORMAT_XRGB8888;
    case DRM_FORMAT_ARGB2101010:
        return DRM_FORMAT_XRGB2101010;
    default:
        return format;
    }
}

static inline Bool
gbm_bo_map_all(struct gbm_bo *bo, bo_user_data_t *user_data)
{
    uint32_t stride = 0;

    if (!bo || !user_data) {
        return FALSE;
    }

    if (user_data->map_addr) {
        return TRUE;
    }

    /* must be NULL before the map call */
    user_data->map_data = NULL;

    /* While reading from gpu memory is often very slow, we do allow it */
    user_data->map_addr = gbm_bo_map(bo->gbm, 0, 0, user_data->width, user_data->height,
                                     GBM_BO_TRANSFER_READ_WRITE, &stride, &user_data->map_data);

    return !!user_data->map_addr;
}

static inline Bool
gbm_bo_map_or_free(struct gbm_bo *bo, bo_user_data_t *user_data)
{
    if (gbm_bo_map_all(bo, user_data) {
        return TRUE;
    }

    if (bo) {
        gbm_bo_destroy(bo);
    }
    return FALSE;
}

static inline gbm_bo*
gbm_bo_create_and_map(struct gbm_device *gbm,
                      bo_user_data_t *user_data,
                      Bool do_map,
                      uint32_t width, uint32_t height,
                      uint32_t format,
                      const uint64_t *modifiers,
                      const unsigned int count,
                      uint32_t flags)
{
    struct gbm_bo *ret = NULL;

    if (!user_data) {
        return NULL;
    }

#ifdef GBM_BO_WITH_MODIFIERS
    if (count && modifiers) {
        user_data->used_modifiers = TRUE;
#ifdef GBM_BO_WITH_MODIFIERS2
        ret = gbm_bo_create_with_modifiers2(gbm, width, height, format,
                                            modifiers, count, flags);
        if (ret && (!do_map || gbm_bo_map_or_free(ret, user_data))) {
            return ret;
        }
#endif
        ret = gbm_bo_create_with_modifiers(gbm, width, height, format,
                                           modifiers, count);

        if (ret && (!do_map || gbm_bo_map_or_free(ret, user_data))) {
            return ret;
        }
    }
#endif

    user_data->used_modifiers = FALSE;
    ret = gbm_bo_create(gbm, width, height, format, flags);

    if (ret && (!do_map || gbm_bo_map_or_free(ret, user_data))) {
        return ret;
    }

    return NULL;
}

static inline gbm_bo*
gbm_bo_create_and_map_with_flag_list(struct gbm_device *gbm,
                                     bo_user_data_t *user_data,
                                     Bool do_map,
                                     uint32_t width, uint32_t height,
                                     uint32_t format,
                                     const uint64_t *modifiers,
                                     const unsigned int count,
                                     const uint32_t *flag_list,
                                     unsigned int flag_count)
{
    struct gbm_bo *ret = NULL;
    for (unsigned int i = 0; i < flag_count && !ret; i++) {
        ret = gbm_bo_create_and_map(gbm, user_data, do_map,
                                    width, height, format,
                                    modifiers, count,
                                    flag_list[i]);
    }

    return ret;
}

static inline Bool
drmmode_create_front_bo(drmmode_ptr drmmode, Bool do_map,
                        gbm_user_data_t *user_data,
                        unsigned width, unsigned height)
{
    uint32_t format = drmmode_gbm_format_for_depth(drmmode->scrn->depth);

    uint32_t num_modifiers;
    int64_t *modifiers = NULL;

    static const front_flag_list[] = { /* best flags */
                                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT |
                                       GBM_BO_USE_FRONT_RENDERING,

                                       /* nvidia compat */
                                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT,

                                       /* linear buffers */
                                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT |
                                       GBM_BO_USE_LINEAR,
                                       GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT |
                                       GBM_BO_USE_WRITE,
                                     };

#ifdef GBM_BO_WITH_MODIFIERS
    num_modifiers = get_modifiers_set(drmmode->scrn, format, &modifiers,
                                      FALSE, TRUE, TRUE);
#endif

    return gbm_bo_create_and_map_with_flag_list(drmmode->gbm,
                                                user_data,
                                                do_map,
                                                width, height,
                                                format,
                                                modifiers, num_modifiers,
                                                front_flag_list,
                                                ARRAY_SIZE(front_flag_list));
}

static inline struct gbm_bo*
drmmode_create_cursor_bo(drmmode_ptr drmmode, gbm_user_data_t *user_data,
                         uint32_t width, uint32_t height)
{
    static const cursor_flag_list[] = { /* best flags */
                                        GBM_BO_USE_CURSOR,

#if 0 /* Use these ones too if we ever need to */
                                        GBM_BO_USE_CURSOR | GBM_BO_USE_LINEAR,
                                        GBM_BO_USE_LINEAR,
#endif

                                        /* For older mesa */
                                        GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE,
                                      };
    struct gbm_bo *ret = NULL;

    /* Assume whatever bpp we have for the primary plane, we also have for the cursor plane */
    int bpp = drmmode->kbpp;

    /**
     * Assume the depth for the cursor is the same as the bpp,
     * even if this is not true for the primary plane (e.g., even if bpp is 32, but drmmode->scrn->depth is 24).
     */
    uint32_t format = drmmode_gbm_format_for_depth(bpp);

    return gbm_bo_create_and_map_with_flag_list(drmmode->gbm,
                                                user_data,
                                                TRUE,
                                                width, height,
                                                format,
                                                NULL, 0,
                                                cursor_flag_list,
                                                ARRAY_SIZE(cursor_flag_list));
}

struct gbm_bo*
gbm_create_best_bo(drmmode_ptr drmmode, Bool do_map,
                   uint32_t width, uint32_t height,
                   int type)
{
    struct gbm_bo *ret = NULL;
    gbm_user_data_t* data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }

    switch (type) {
    case DRMMODE_FRONT_BO:
        ret = drmmode_create_front_bo(drmmode, do_map, data, width, height);
        break;
    case DRMMODE_CURSOR_BO:
        /* We always want to map cursor buffers */
        ret = drmmode_create_cursor_bo(drmmode, data, width, height);
        break;
    }

    if (!ret) {
        free(data);
        return NULL;
    }

    gbm_bo_set_user_data(ret, data, destroy_user_data);
    return ret;
}

/* dmabuf import */
struct gbm_bo*
gbm_back_bo_from_fd(drmmode_ptr drmmode, Bool do_map, int fd_handle, uint32_t pitch, uint32_t size)
{
    struct gbm_bo *ret = NULL;

    /* pitch == width * cpp */
    int width = pitch / drmmode->cpp;
    /* size == pitch * height */
    int height = size / pitch;

    int depth = drmmode->scrn->depth > 0 ?
                drmmode->scrn->depth : drmmode->kbpp;

    uint32_t format = drmmode_gbm_format_for_depth(depth);

    gbm_user_data_t* data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }

    data->used_modifiers = FALSE;

    struct gbm_import_fd_data import_data = {.fd = fd_handle,
                                             .width = width,
                                             .height = height,
                                             .stride = pitch,
                                             .format = format,
                                            };

    ret = gbm_bo_import(drmmode->gbm, GBM_BO_IMPORT_FD, &import_data,
                        GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);

    if (ret && (!do_map || gbm_bo_map_or_free(ret, user_data))) {
        return ret;
    }

    ret = gbm_bo_import(drmmode->gbm, GBM_BO_IMPORT_FD, &import_data,
                        GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_WRITE);

    if (ret && (!do_map || gbm_bo_map_or_free(ret, user_data))) {
        return ret;
    }

    return NULL;
}

/* A bit of a misnomer, this is a dmabuf export */
int
drmmode_bo_import(drmmode_ptr drmmode, struct gbm_bo *bo,
                  uint32_t *fb_id)
{
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);

#ifdef GBM_BO_WITH_MODIFIERS
    modesettingPtr ms = modesettingPTR(drmmode->scrn);
    if (ms->kms_has_modifiers &&
        gbm_bo_get_modifier(bo) != DRM_FORMAT_MOD_INVALID) {
        int num_fds;

        num_fds = gbm_bo_get_plane_count(bo);
        if (num_fds > 0) {
            int i;
            uint32_t format;
            uint32_t handles[4];
            uint32_t strides[4];
            uint32_t offsets[4];
            uint64_t modifiers[4];

            memset(handles, -1, sizeof(handles));
            memset(strides, 0, sizeof(strides));
            memset(offsets, 0, sizeof(offsets));
            memset(modifiers, 0, sizeof(modifiers));

            format = gbm_bo_get_format(bo);
            format = get_opaque_format(format);
            for (i = 0; i < num_fds; i++) {
                handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
                strides[i] = gbm_bo_get_stride_for_plane(bo, i);
                offsets[i] = gbm_bo_get_offset(bo, i);
                modifiers[i] = gbm_bo_get_modifier(bo);
            }

            return drmModeAddFB2WithModifiers(drmmode->fd, width, height,
                                              format, handles, strides,
                                              offsets, modifiers, fb_id,
                                              DRM_MODE_FB_MODIFIERS);
        }
    }
#endif
    return drmModeAddFB(drmmode->fd, width, height,
                        drmmode->scrn->depth, drmmode->kbpp,
                        gbm_bo_get_stride(bo),
                        gbm_bo_get_handle(bo).u32, fb_id);
}
