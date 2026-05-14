/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2026 stefan11111 <stefan11111@shitposting.expert>
 */

/**
 * Private definitions to be used by glamor and the xf86 server
 */

#ifndef GLAMOR_EGL_PRIV_H
#define GLAMOR_EGL_PRIV_H

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include "scrnintstr.h"
#include "glamor_egl_ext.h"

#ifdef GLAMOR_HAS_GBM
#include <gbm.h>
#endif

typedef struct glamor_egl_screen_private {
    EGLDisplay display;
    EGLContext context;
    char *device_path;
    char *glvnd_vendor; /* glvnd vendor library name or driver name */
    int exact_glvnd_vendor; /* If the glvnd vendor should be assumed valid with no checks */
    void* server_private;

#ifdef GLAMOR_HAS_GBM
    struct gbm_device *gbm;
    int fast_gbm_import;
    int can_texture_gbm_bo; /* If gbm bo's can be textured to create pixmaps from */
#endif
    int fd;
    int dmabuf_capable;
} glamor_egl_priv_t;

/* Get a screen's glamor egl private */
extern glamor_egl_priv_t*
(*glamor_egl_get_screen_private)(ScreenPtr screen);

/* Create an EGLImageKHR from dma bufs */
EGLImageKHR
glamor_egl_image_from_dma_bufs(ScreenPtr screen,
                               uint32_t num_fds, const int *fds,
                               int width, int height,
                               const int *strides, const int *offsets,
                               uint32_t format, uint64_t modifier);

/* Create a texture from an image an map it to a pixmap */
Bool
glamor_egl_create_textured_pixmap_from_egl_image(PixmapPtr pixmap,
                                                 EGLImageKHR image,
                                                 Bool used_modifiers);

/* Query the formats supported by egl */
Bool
glamor_get_formats_internal(glamor_egl_priv_t *glamor_egl,
                            CARD32 *num_formats, CARD32 **formats);

/* Query the modifiers supported by egl */
Bool
glamor_get_modifiers_internal(glamor_egl_priv_t *glamor_egl, uint32_t format,
                              uint32_t *num_modifiers, uint64_t **modifiers);


/**
 * Deinitialize an egl context created by glamor egl
 * and free associated resources.
 */
void glamor_egl_cleanup(glamor_egl_priv_t *glamor_egl);

/**
 * Deinitialize an egl context created by glamor egl
 * and free associated resources.
 */
void glamor_egl_cleanup_screen(ScreenPtr screen);


#endif /* GLAMOR_EGL_PRIV_H */
