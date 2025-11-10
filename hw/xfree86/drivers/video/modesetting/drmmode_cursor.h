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
 *    Dave Airlie <airlied@redhat.com>
 *    Aaron Plattner <aplattner@nvidia.com>
 *    stefan11111 <stefan11111@shitposting.expert>
 *
 */
#ifndef DRMMODE_CURSOR_H
#define DRMMODE_CURSOR_H

#include <stdint.h>

#include "mipointer.h"

#include <xf86drm.h>
#include "xf86Crtc.h"

#include <cursorstr.h>

#include "drmmode_common.h"
#include "drmmode_display.h"

void drmmode_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg);
void drmmode_set_cursor_position(xf86CrtcPtr crtc, int x, int y);
Bool drmmode_set_cursor(xf86CrtcPtr crtc, int width, int height);
Bool drmmode_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image);
void drmmode_hide_cursor(xf86CrtcPtr crtc);
Bool drmmode_show_cursor(xf86CrtcPtr crtc);

#ifdef LIBDRM_HAS_PLANE_SIZE_HINTS
void drmmode_populate_cursor_size_hints(drmmode_ptr drmmode, drmmode_crtc_private_ptr drmmode_crtc, int size_hints_blob);
#endif

drmmode_cursor_dim_rec drmmode_cursor_get_fallback(drmmode_crtc_private_ptr drmmode_crtc);
Bool drmmode_map_cursor_bos(ScrnInfoPtr pScrn, drmmode_ptr drmmode);

extern miPointerSpriteFuncRec drmmode_sprite_funcs;

#endif /* DRMMODE_CURSOR_H */
