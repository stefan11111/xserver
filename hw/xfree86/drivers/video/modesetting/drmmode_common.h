/*
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2019 NVIDIA CORPORATION
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Aaron Plattner <aplattner@nvidia.com>
 *     stefan11111 <stefan11111@shitposting.expert>
 *
 */
#ifndef DRMMODE_COMMON_H
#define DRMMODE_COMMON_H

#include <stdint.h>

#ifdef GLAMOR_HAS_GBM
#include <gbm.h>
#endif

#include <xf86drm.h>
#include <xf86Crtc.h>

#include "xf86drmMode.h"

#ifdef CONFIG_UDEV_KMS
#include "libudev.h"
#endif

#include "dumb_bo.h"

struct gbm_device;

typedef struct {
    uint32_t width;
    uint32_t height;
    struct dumb_bo *dumb;
#ifdef GLAMOR_HAS_GBM
    Bool used_modifiers;
    struct gbm_bo *gbm;
#endif
} drmmode_bo;

typedef struct {
    uint16_t width, height;
} drmmode_cursor_dim_rec, *drmmode_cursor_dim_ptr;

typedef struct {
    uint16_t num_dimensions;

    /* Sorted from smallest to largest. */
    drmmode_cursor_dim_rec* dimensions;
    struct dumb_bo *bo;
} drmmode_cursor_rec, *drmmode_cursor_ptr;

typedef struct _msSpritePriv {
    CursorPtr cursor;
    Bool sprite_visible;
} msSpritePrivRec, *msSpritePrivPtr;

typedef struct {
    int fd;
    unsigned fb_id;
    drmModeFBPtr mode_fb;
    int cpp;
    int kbpp;
    ScrnInfoPtr scrn;

    struct gbm_device *gbm;

#ifdef CONFIG_UDEV_KMS
    struct udev_monitor *uevent_monitor;
    InputHandlerProc uevent_handler;
#endif
    drmEventContext event_context;
    drmmode_bo front_bo;
    Bool sw_cursor;
    Bool set_cursor_failed;

    /* Broken-out options. */
    OptionInfoPtr Options;

    Bool glamor;
    Bool shadow_enable;
    Bool shadow_enable2;
    /** Is Option "PageFlip" enabled? */
    Bool pageflip;
    Bool force_24_32;
    void *shadow_fb;
    void *shadow_fb2;

    DevPrivateKeyRec pixmapPrivateKeyRec;
    DevScreenPrivateKeyRec spritePrivateKeyRec;
    DevPrivateKeyRec vrrPrivateKeyRec;
    /* Number of SW cursors currently visible on this screen */
    int sprites_visible;

    Bool reverse_prime_offload_mode;

    Bool is_secondary;

    PixmapPtr fbcon_pixmap;

    Bool dri2_flipping;
    Bool present_flipping;
    Bool flip_bo_import_failed;

    Bool can_async_flip;
    Bool async_flip_secondaries;
    Bool dri2_enable;
    Bool present_enable;
    Bool tearfree_enable;

    uint32_t vrr_prop_id;
    Bool use_ctm;

    Bool pending_modeset;
} drmmode_rec, *drmmode_ptr;

#endif /* DRMMODE_COMMON_H */
