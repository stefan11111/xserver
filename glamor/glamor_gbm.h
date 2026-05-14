/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2026 stefan11111 <stefan11111@shitposting.expert>
 */

#ifndef GLAMOR_GBM_H
#define GLAMOR_GBM_H

#include <stdint.h>

#include "scrnintstr.h"

#ifdef GLAMOR_HAS_GBM
#include <gbm.h>

struct gbm_device*
glamor_gbm_create_gbm_device(int fd);
#endif

int
glamor_gbm_fds_from_pixmap_slow(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                                uint32_t *strides, uint32_t *offsets,
                                uint64_t *modifier);

int
glamor_gbm_fd_from_pixmap_slow(ScreenPtr screen, PixmapPtr pixmap,
                               CARD16 *stride, CARD32 *size);

Bool
glamor_gbm_can_texture_gbm_bo(glamor_egl_priv_t *glamor_egl, int linear_only);

const char*
glamor_gbm_get_gl_vendor(glamor_egl_priv_t *glamor_egl);

#endif /* GLAMOR_GBM_H */
