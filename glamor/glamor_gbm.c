/*
 * Copyright © 2010 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */
#include <dix-config.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef WITH_LIBDRM
#include <xf86drm.h>
#include <drm_fourcc.h>
#endif

#define EGL_DISPLAY_NO_X_MESA

#ifdef GLAMOR_HAS_GBM
#include <gbm.h>
#endif

#include "dix/screen_hooks_priv.h"
#include "glamor/glamor_priv.h"
#include "os/bug_priv.h"

#include "glamor.h"
#include "glamor_egl.h"
#include "glamor_egl_ext.h"
#include "glamor_egl_priv.h"
#include "glamor_gbm.h"
#include "glamor_glx_provider.h"
#include "dri3.h"

struct gbm_device *
glamor_egl_get_gbm_device(ScreenPtr screen)
{
#ifdef GLAMOR_HAS_GBM
    return glamor_egl_get_screen_private(screen)->gbm;
#else
    return NULL;
#endif
}

#if defined(GLAMOR_HAS_GBM) && defined (WITH_LIBDRM)
static int
glamor_get_flink_name(int fd, int handle, int *name)
{
    struct drm_gem_flink flink;

    flink.handle = handle;
    if (ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink) < 0) {

        /*
         * Assume non-GEM kernels have names identical to the handle
         */
        if (errno == ENODEV) {
            *name = handle;
            return TRUE;
        } else {
            return FALSE;
        }
    }
    *name = flink.name;
    return TRUE;
}

static void
glamor_gbm_get_name_from_bo(int gbm_fd, struct gbm_bo *bo, int *name)
{
    union gbm_bo_handle handle;

    handle = gbm_bo_get_handle(bo);
    if (!glamor_get_flink_name(gbm_fd, handle.u32, name))
        *name = -1;
}
#endif

#ifdef GLAMOR_HAS_GBM
static EGLImageKHR
glamor_gbm_image_from_gbm_bo(ScreenPtr screen, struct gbm_bo *bo)
{
    glamor_egl_priv_t *glamor_egl =
        glamor_egl = glamor_egl_get_screen_private(screen);

    Bool tried_fast_import = FALSE;

    if (glamor_egl->fast_gbm_import) {
        EGLImageKHR image;
        image = eglCreateImageKHR(glamor_egl->display,
                                  EGL_NO_CONTEXT,
                                  EGL_NATIVE_PIXMAP_KHR, bo, NULL);
        if (image != EGL_NO_IMAGE_KHR) {
            return image;
        }
        tried_fast_import = TRUE;
    }

    if (glamor_egl->dmabuf_capable) {
        EGLImageKHR image;
        uint64_t modifier = gbm_bo_get_modifier(bo);
        uint32_t num_planes = gbm_bo_get_plane_count(bo);
        uint32_t format = gbm_bo_get_format(bo);
        int fds[GBM_MAX_PLANES];
        int strides[GBM_MAX_PLANES];
        int offsets[GBM_MAX_PLANES];

        uint32_t width = gbm_bo_get_width(bo);
        uint32_t height = gbm_bo_get_height(bo);

#ifdef GBM_BO_FD_FOR_PLANE
        if (num_planes > GBM_MAX_PLANES) {
            goto fallback;
        }
        for (int plane = 0; plane < num_planes; plane++) {
            fds[plane] = gbm_bo_get_fd_for_plane(bo, plane);
            offsets[plane] = gbm_bo_get_offset(bo, plane);
            strides[plane] = gbm_bo_get_stride_for_plane(bo, plane);
        }
#else
#ifdef GBM_BO_FD
        num_planes = 1;
        fds[0] = gbm_bo_get_fd(bo);
        offsets[0] = 0;
        strides[0] = gbm_bo_get_stride(bo);
#else
        goto fallback;
#endif
#endif

        /* -Werror=maybe-uninitialized */
        memset(fds + num_planes, -1, (GBM_MAX_PLANES - num_planes) * sizeof(*fds));
        memset(offsets + num_planes, 0, (GBM_MAX_PLANES - num_planes) * sizeof(*offsets));
        memset(strides + num_planes, 0, (GBM_MAX_PLANES - num_planes) * sizeof(*strides));

        image = glamor_egl_image_from_dma_bufs(screen,
                                               num_planes, fds,
                                               width, height,
                                               strides, offsets,
                                               format, modifier);

        for (int plane = 0; plane < num_planes; plane++) {
            close(fds[plane]);
            fds[plane] = -1;
        }

        if (image != EGL_NO_IMAGE_KHR) {
            glamor_egl->fast_gbm_import = FALSE;
            return image;
        }
    }

#if defined(GBM_BO_FD_FOR_PLANE) || !defined(GBM_BO_FD) /* -Werror=unused-label */
fallback:
#endif
    if (tried_fast_import) {
        return EGL_NO_IMAGE_KHR;
    }

    return eglCreateImageKHR(glamor_egl->display,
                             EGL_NO_CONTEXT,
                             EGL_NATIVE_PIXMAP_KHR, bo, NULL);
}
#endif

Bool
glamor_egl_create_textured_pixmap_from_gbm_bo(PixmapPtr pixmap,
                                              struct gbm_bo *bo,
                                              Bool used_modifiers)
{
#ifdef GLAMOR_HAS_GBM
    ScreenPtr screen = pixmap->drawable.pScreen;
    EGLImageKHR image = glamor_gbm_image_from_gbm_bo(screen, bo);

    return glamor_egl_create_textured_pixmap_from_egl_image(pixmap, image,
                                                            used_modifiers);
#else
    return FALSE;
#endif
}

#ifdef GLAMOR_HAS_GBM
static Bool
glamor_gbm_make_pixmap_exportable(PixmapPtr pixmap, Bool modifiers_ok)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_egl_priv_t *glamor_egl =
        glamor_egl_get_screen_private(screen);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);
    unsigned width = pixmap->drawable.width;
    unsigned height = pixmap->drawable.height;
    uint32_t format;
    struct gbm_bo *bo = NULL;
    Bool used_modifiers = FALSE;
    PixmapPtr exported;
    GCPtr scratch_gc;

    BUG_RETURN_VAL(!pixmap_priv, FALSE);

    if (pixmap_priv->image &&
        (modifiers_ok || !pixmap_priv->used_modifiers))
        return TRUE;

    if (!glamor_egl->gbm || !glamor_egl->can_texture_gbm_bo) {
        return FALSE;
    }

    switch (pixmap->drawable.depth) {
    case 30:
        format = GBM_FORMAT_ARGB2101010;
        break;
    case 32:
    case 24:
        format = GBM_FORMAT_ARGB8888;
        break;
    case 16:
        format = GBM_FORMAT_RGB565;
        break;
    case 15:
        format = GBM_FORMAT_ARGB1555;
        break;
    case 8:
        format = GBM_FORMAT_R8;
        break;
    default:
        LogMessage(X_ERROR,
                   "Failed to make %d depth, %dbpp pixmap exportable\n",
                   pixmap->drawable.depth, pixmap->drawable.bitsPerPixel);
        return FALSE;
    }

#ifdef GBM_BO_WITH_MODIFIERS
    if (modifiers_ok && glamor_egl->dmabuf_capable) {
        uint32_t num_modifiers;
        uint64_t *modifiers = NULL;

        if (!glamor_get_modifiers(screen, format, &num_modifiers, &modifiers)) {
            return FALSE;
        }

        if (num_modifiers > 0) {
#ifdef GBM_BO_WITH_MODIFIERS2
            /* TODO: Is scanout ever used? If so, where? */
            bo = gbm_bo_create_with_modifiers2(glamor_egl->gbm, width, height,
                                               format, modifiers, num_modifiers,
                                               GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
            if (!bo) {
                /* something failed, try again without GBM_BO_USE_SCANOUT */
                /* maybe scanout does work, but modifiers aren't supported */
                /* we handle this case on the fallback path */
                bo = gbm_bo_create_with_modifiers2(glamor_egl->gbm, width, height,
                                                   format, modifiers, num_modifiers,
                                                   GBM_BO_USE_RENDERING);
#if 0
                if (bo) {
                    /* TODO: scanout failed, but regular buffer succeeded, maybe log something? */
                }
#endif
            }
#else
            bo = gbm_bo_create_with_modifiers(glamor_egl->gbm, width, height,
                                              format, modifiers, num_modifiers);
#endif
        }
        if (bo)
            used_modifiers = TRUE;
        free(modifiers);
    }
#endif

    if (!bo)
    {
        /* TODO: Is scanout ever used? If so, where? */
        bo = gbm_bo_create(glamor_egl->gbm, width, height, format,
#ifdef GBM_BO_USE_LINEAR
                (pixmap->usage_hint == CREATE_PIXMAP_USAGE_SHARED ?
                 GBM_BO_USE_LINEAR : 0) |
#endif
                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
        if (!bo) {
            /* something failed, try again without GBM_BO_USE_SCANOUT */
            bo = gbm_bo_create(glamor_egl->gbm, width, height, format,
#ifdef GBM_BO_USE_LINEAR
                    (pixmap->usage_hint == CREATE_PIXMAP_USAGE_SHARED ?
                     GBM_BO_USE_LINEAR : 0) |
#endif
                     GBM_BO_USE_RENDERING);
#if 0
            if (bo) {
                /* TODO: scanout failed, but regular buffer succeeded, maybe log something? */
            }
#endif
        }
    }

    if (!bo) {
        LogMessage(X_ERROR,
                   "Failed to make %dx%dx%dbpp GBM bo\n",
                   width, height, pixmap->drawable.bitsPerPixel);
        return FALSE;
    }

    exported = screen->CreatePixmap(screen, 0, 0, pixmap->drawable.depth, 0);
    screen->ModifyPixmapHeader(exported, width, height, 0, 0,
                               gbm_bo_get_stride(bo), NULL);
    if (!glamor_egl_create_textured_pixmap_from_gbm_bo(exported, bo,
                                                       used_modifiers)) {
        LogMessage(X_ERROR,
                   "Failed to make %dx%dx%dbpp pixmap from GBM bo\n",
                   width, height, pixmap->drawable.bitsPerPixel);
        dixDestroyPixmap(exported, 0);
        gbm_bo_destroy(bo);
        return FALSE;
    }
    gbm_bo_destroy(bo);

    scratch_gc = GetScratchGC(pixmap->drawable.depth, screen);
    ValidateGC(&pixmap->drawable, scratch_gc);
    (void) scratch_gc->ops->CopyArea(&pixmap->drawable, &exported->drawable,
                              scratch_gc,
                              0, 0, width, height, 0, 0);
    FreeScratchGC(scratch_gc);

    /* Now, swap the tex/gbm/EGLImage/etc. of the exported pixmap into
     * the original pixmap struct.
     */
    glamor_egl_exchange_buffers(pixmap, exported);

    /* Swap the devKind into the original pixmap, reflecting the bo's stride */
    screen->ModifyPixmapHeader(pixmap, 0, 0, 0, 0, exported->devKind, NULL);

    dixDestroyPixmap(exported, 0);

    return TRUE;
}

static struct gbm_bo *
glamor_gbm_bo_from_pixmap_internal(ScreenPtr screen, PixmapPtr pixmap)
{
    glamor_egl_priv_t *glamor_egl =
        glamor_egl_get_screen_private(screen);
    struct glamor_pixmap_private *pixmap_priv =
        glamor_get_pixmap_private(pixmap);

    BUG_RETURN_VAL(!pixmap_priv, NULL);

    if (!pixmap_priv->image)
        return NULL;

    if (!glamor_egl->gbm) {
        return NULL;
    }

    return gbm_bo_import(glamor_egl->gbm, GBM_BO_IMPORT_EGL_IMAGE,
                         pixmap_priv->image, GBM_BO_USE_RENDERING);
}
#endif

int
glamor_egl_fd_name_from_pixmap(ScreenPtr screen,
                               PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size)
{
#if defined(GLAMOR_HAS_GBM) && defined(WITH_LIBDRM)
    glamor_egl_priv_t *glamor_egl;
    struct gbm_bo *bo;
    int fd = -1;

    glamor_egl = glamor_egl_get_screen_private(screen);

    if (!glamor_gbm_make_pixmap_exportable(pixmap, FALSE))
        goto failure;

    bo = glamor_gbm_bo_from_pixmap_internal(screen, pixmap);
    if (!bo)
        goto failure;

    pixmap->devKind = gbm_bo_get_stride(bo);

    glamor_gbm_get_name_from_bo(glamor_egl->fd, bo, &fd);
    *stride = pixmap->devKind;
    *size = pixmap->devKind * gbm_bo_get_height(bo);

    gbm_bo_destroy(bo);
 failure:
    return fd;
#else
    return -1;
#endif
}

struct gbm_bo *
glamor_gbm_bo_from_pixmap(ScreenPtr screen, PixmapPtr pixmap)
{
#ifdef GLAMOR_HAS_GBM
    if (!glamor_gbm_make_pixmap_exportable(pixmap, TRUE))
        return NULL;

    return glamor_gbm_bo_from_pixmap_internal(screen, pixmap);
#else
    return NULL;
#endif
}

/**
 * Used for untextured pixmaps
 *
 * XXX Do we still need this? XXX
 * Looking at the rest of the code, exprting untextured pixmaps does not seem supported
 * (nor should any exist other that the root pixmap).
 */
int
glamor_gbm_fds_from_pixmap_slow(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                                uint32_t *strides, uint32_t *offsets,
                                uint64_t *modifier)
{
#if defined(GLAMOR_BO_FD) && defined (WITH_LIBDRM)
    struct gbm_bo *bo;
    int num_fds;
#ifdef GBM_BO_WITH_MODIFIERS
#ifndef GBM_BO_FD_FOR_PLANE
    int32_t first_handle;
#endif
    int i;
#endif

    if (!glamor_gbm_make_pixmap_exportable(pixmap, TRUE))
        return 0;

    bo = glamor_gbm_bo_from_pixmap_internal(screen, pixmap);
    if (!bo)
        return 0;

#ifdef GBM_BO_WITH_MODIFIERS
    num_fds = gbm_bo_get_plane_count(bo);
    for (i = 0; i < num_fds; i++) {
#ifdef GBM_BO_FD_FOR_PLANE
        fds[i] = gbm_bo_get_fd_for_plane(bo, i);
#else
        union gbm_bo_handle plane_handle = gbm_bo_get_handle_for_plane(bo, i);

        if (i == 0)
            first_handle = plane_handle.s32;

        /* If all planes point to the same object as the first plane, i.e. they
         * all have the same handle, we can fall back to the non-planar
         * gbm_bo_get_fd without losing information. If they point to different
         * objects we are out of luck and need to give up.
         */
	if (first_handle == plane_handle.s32)
            fds[i] = gbm_bo_get_fd(bo);
        else
            fds[i] = -1;
#endif
        if (fds[i] == -1) {
            while (--i >= 0)
                close(fds[i]);
            return 0;
        }
        strides[i] = gbm_bo_get_stride_for_plane(bo, i);
        offsets[i] = gbm_bo_get_offset(bo, i);
    }
    *modifier = gbm_bo_get_modifier(bo);
#else
    num_fds = 1;
    fds[0] = gbm_bo_get_fd(bo);
    if (fds[0] == -1)
        return 0;
    strides[0] = gbm_bo_get_stride(bo);
    offsets[0] = 0;
    *modifier = DRM_FORMAT_MOD_INVALID;
#endif

    gbm_bo_destroy(bo);
    return num_fds;
#else
    return 0;
#endif
}

/**
 * Used for untextured pixmaps
 *
 * XXX Do we still need this?
 * Looking at the rest of the code, exprting untextured pixmaps does not seem supported
 * (nor should any exist other that the root pixmap).
 */
int
glamor_gbm_fd_from_pixmap_slow(ScreenPtr screen, PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size)
{
#ifdef GBM_BO_FD
    struct gbm_bo *bo;
    int fd;

    if (!glamor_gbm_make_pixmap_exportable(pixmap, FALSE))
        return -1;

    bo = glamor_gbm_bo_from_pixmap_internal(screen, pixmap);
    if (!bo)
        return -1;

    fd = gbm_bo_get_fd(bo);
    *stride = gbm_bo_get_stride(bo);
    *size = *stride * gbm_bo_get_height(bo);
    gbm_bo_destroy(bo);

    return fd;
#else
    return -1;
#endif
}

#ifdef GLAMOR_HAS_GBM
static inline struct gbm_device*
gbm_create_device_by_name(int fd, const char* name)
{
    struct gbm_device* ret = NULL;
    const char* old_backend = getenv("GBM_BACKEND");
    setenv("GBM_BACKEND", name, 1);
    ret = gbm_create_device(fd);
    unsetenv("GBM_BACKEND");
    if (old_backend) {
        setenv("GBM_BACKEND", old_backend, 1);
    }
    return ret;
}

struct gbm_device*
glamor_gbm_create_gbm_device(int fd)
{
    struct gbm_device *ret = gbm_create_device(fd);
    if (ret) {
        return ret;
    }

    return gbm_create_device_by_name(fd, "dumb");
}
#endif

Bool
glamor_gbm_can_texture_gbm_bo(glamor_egl_priv_t *glamor_egl, int linear_only)
{
    /* Check if at least one combination of format + modifier is supported */
    CARD32 *formats = NULL;
    CARD32 num_formats = 0;
    Bool found = FALSE;
    if (!glamor_get_formats_internal(glamor_egl, &num_formats, &formats)) {
        return FALSE;
    }

    if (num_formats == 0) {
        /* Everything is supported (unlikely) */
        return TRUE;
    }

    for (uint32_t i = 0; i < num_formats; i++) {
        uint64_t *modifiers = NULL;
        uint32_t num_modifiers = 0;
        if (glamor_get_modifiers_internal(glamor_egl, formats[i],
                                          &num_modifiers, &modifiers)) {
            if (linear_only) {
                for (uint32_t j = 0; j < num_modifiers; j++) {
                    if (
#ifdef WITH_LIBDRM
                        (modifiers[i] != DRM_FORMAT_MOD_LINEAR) &&
                        (modifiers[i] != DRM_FORMAT_MOD_INVALID))
#else
                        modifiers[i] != 0) /* DRM_FORMAT_MOD_LINEAR */
#endif
                    {
                        found = TRUE;
                        break;
                    }
                }
            }
            free(modifiers);
            if (found) {
                break;
            }
        }
    }
    free(formats);

    return found;
}

const char*
glamor_gbm_get_gl_vendor(glamor_egl_priv_t *glamor_egl)
{
#ifdef GLAMOR_HAS_GBM
    if (glamor_egl->gbm) {
        const char *gbm_backend_name;
        gbm_backend_name = gbm_device_get_backend_name(glamor_egl->gbm);
        if (gbm_backend_name) {
            if (!strncmp(gbm_backend_name, "nvidia", sizeof("nvidia") - 1)) {
                 return "nvidia";
            } else if (!strcmp(gbm_backend_name, "drm")) {
                 /* Mesa uses "drm" as the gbm backend name */
                 return "mesa";
            }
        }
    }
#endif
    return NULL;
}
