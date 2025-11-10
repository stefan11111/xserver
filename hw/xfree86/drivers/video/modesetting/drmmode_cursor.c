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

#include <dix-config.h>

#include "inputstr.h"

#include "drmmode_bo.h"
#include "drmmode_cursor.h"
#include "driver.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define msGetSpritePriv(dev, ms, screen) dixLookupScreenPrivate(&(dev)->devPrivates, &(ms)->drmmode.spritePrivateKeyRec, screen)


void
drmmode_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{

}

void
drmmode_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmModeMoveCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, x, y);
}

Bool
drmmode_set_cursor(xf86CrtcPtr crtc, int width, int height)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t handle = drmmode_crtc->cursor.bo->handle;
    CursorPtr cursor = xf86CurrentCursor(crtc->scrn->pScreen);
    int ret = -EINVAL;

    if (cursor == NullCursor)
        return TRUE;

    ret = drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                            handle, width, height,
                            cursor->bits->xhot, cursor->bits->yhot);

    /* -EINVAL can mean that an old kernel supports drmModeSetCursor but
     * not drmModeSetCursor2, though it can mean other things too. */
    if (ret == -EINVAL)
        ret = drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                               handle, width, height);

    /* -ENXIO normally means that the current drm driver supports neither
     * cursor_set nor cursor_set2.  Disable hardware cursor support for
     * the rest of the session in that case. */
    if (ret == -ENXIO) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
        xf86CursorInfoPtr cursor_info = xf86_config->cursor_info;

        cursor_info->MaxWidth = cursor_info->MaxHeight = 0;
        drmmode_crtc->drmmode->sw_cursor = TRUE;
        drmmode_crtc->drmmode->set_cursor_failed = TRUE;
    }

    if (ret) {
        /* fallback to swcursor */
        return FALSE;
    }

    return TRUE;
}

static int
drmmode_cursor_get_pitch(drmmode_crtc_private_ptr drmmode_crtc, int idx)
{
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmmode_cursor_ptr drmmode_cursor = &drmmode_crtc->cursor;

    int width  = drmmode_cursor->dimensions[idx].width;
    int height = drmmode_cursor->dimensions[idx].height;

    int num_pitches = drmmode_cursor->num_dimensions;

    if (!drmmode_crtc->cursor_pitches) {
        drmmode_crtc->cursor_pitches = calloc(num_pitches, sizeof(int));
        if (!drmmode_crtc->cursor_pitches) {
            /* we couldn't allocate memory for the cache, so we don't cache the result */
            int ret;
            struct dumb_bo *bo = dumb_bo_create(drmmode->fd, width, height, drmmode->kbpp);
            if (!bo) {
                /* We couldn't allocate a bo, so we try to guess the pitch */
                return MAX(width, 64);
            }

            ret = bo->pitch / drmmode->cpp;

            dumb_bo_destroy(drmmode->fd, bo);
            return ret;
        }
    }

    if (drmmode_crtc->cursor_pitches[idx]) {
        /* return the cached pitch */
        return drmmode_crtc->cursor_pitches[idx];
    }

    struct dumb_bo *bo = dumb_bo_create(drmmode->fd, width, height, drmmode->kbpp);
    if (!bo) {
        /* We couldn't allocate a bo, so we try to guess the pitch */
        return MAX(width, 64);
    }

    drmmode_crtc->cursor_pitches[idx] = bo->pitch / drmmode->cpp;

    dumb_bo_destroy(drmmode->fd, bo);
    return drmmode_crtc->cursor_pitches[idx];
}

static void
drmmode_paint_cursor(struct dumb_bo *cursor_bo, int cursor_pitch, int cursor_width, int cursor_height,
                     const CARD32 * restrict image, int image_width, int image_height,
                     drmmode_crtc_private_ptr restrict drmmode_crtc, int glyph_width, int glyph_height)
{
    int width_todo;
    int height_todo;

    CARD32 *cursor = cursor_bo->ptr;

    /*
     * The image buffer can be smaller than the cursor buffer.
     * This means that we can't clear the cursor by copying '\0' bytes
     * from the image buffer, because we might read out of bounds.
     */
    if (
        /* If the buffer is uninitialized, assume it is dirty */
        (drmmode_crtc->cursor_glyph_width == 0 &&
         drmmode_crtc->cursor_glyph_height == 0) ||

        /* Sanity check so we don't read from the image out of bounds */
        (drmmode_crtc->cursor_glyph_width > image_width ||
         drmmode_crtc->cursor_glyph_height > image_height) ||

        /* If the pitch changed, the memory layout of the cursor data changed, so the buffer is dirty */
        /* See: https://github.com/X11Libre/xserver/pull/1234 */
        (drmmode_crtc->old_pitch != cursor_pitch)
       ) {
        memset(cursor, 0, cursor_bo->size);

        /* Since we already cleared the buffer, no need to clear it again bellow */
        drmmode_crtc->cursor_glyph_width = 0;
        drmmode_crtc->cursor_glyph_height = 0;
    }

    drmmode_crtc->old_pitch = cursor_pitch;

    /* Paint only what we need to */
    width_todo = MAX(drmmode_crtc->cursor_glyph_width, glyph_width);
    height_todo = MAX(drmmode_crtc->cursor_glyph_height, glyph_height);

    /* remember the size of the current cursor glyph */
    drmmode_crtc->cursor_glyph_width = glyph_width;
    drmmode_crtc->cursor_glyph_height = glyph_height;

    for (int i = 0; i < height_todo; i++) {
        memcpy(cursor + i * cursor_pitch, image + i * image_width, width_todo * sizeof(*cursor));
    }
}

static void drmmode_probe_cursor_size(xf86CrtcPtr crtc);

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by calling the drmModeSetCursor2 ioctl.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
Bool
drmmode_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    CursorPtr cursor = xf86CurrentCursor(crtc->scrn->pScreen);
    int i;

    if (drmmode_crtc->cursor_up) {
        /* we probe the cursor so late, because we want to make sure that
           the screen is fully initialized and something is already drawn on it.
           Otherwise, we can't get reliable results with the probe. */
        drmmode_probe_cursor_size(crtc);
    }

    drmmode_cursor_rec drmmode_cursor = drmmode_crtc->cursor;

    /* Find the most compatiable size. */
    for (i = 0; i < drmmode_cursor.num_dimensions; i++)
    {
        drmmode_cursor_dim_rec dimensions = drmmode_cursor.dimensions[i];

        if (dimensions.width >= cursor->bits->width &&
            dimensions.height >= cursor->bits->height) {
                break;
        }
    }

    const int cursor_pitch = drmmode_cursor_get_pitch(drmmode_crtc, i);

    /* Get the resolution of the cursor. */
    int cursor_width  = drmmode_cursor.dimensions[i].width;
    int cursor_height = drmmode_cursor.dimensions[i].height;

    /* Get the size of the cursor image buffer */
    int image_width  = ms->cursor_image_width;
    int image_height = ms->cursor_image_height;

    /* cursor should be mapped already */
    drmmode_paint_cursor(drmmode_cursor.bo, cursor_pitch, cursor_width, cursor_height,
                         image, image_width, image_height,
                         drmmode_crtc, cursor->bits->width, cursor->bits->height);

    /* set cursor width and height here for drmmode_show_cursor */
    drmmode_crtc->cursor_width  = cursor_width;
    drmmode_crtc->cursor_height = cursor_height;

    return drmmode_crtc->cursor_up ? drmmode_set_cursor(crtc, cursor_width, cursor_height) : TRUE;
}

void
drmmode_hide_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmmode_crtc->cursor_up = FALSE;
    drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0,
                     drmmode_crtc->cursor_width, drmmode_crtc->cursor_height);
}

Bool
drmmode_show_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_crtc->cursor_up = TRUE;
    return drmmode_set_cursor(crtc, drmmode_crtc->cursor_width, drmmode_crtc->cursor_height);
}

#ifdef LIBDRM_HAS_PLANE_SIZE_HINTS
void
drmmode_populate_cursor_size_hints(drmmode_ptr drmmode, drmmode_crtc_private_ptr drmmode_crtc, int size_hints_blob)
{
    drmModePropertyBlobRes *blob;

    if (!drmmode_crtc)
        return;

    if (drmmode_crtc->cursor_probed)
        return;

    if (!size_hints_blob)
        return;

    blob = drmModeGetPropertyBlob(drmmode->fd, size_hints_blob);

    if (!blob)
        return;

    if (!blob->length)
        goto fail;

    const struct drm_plane_size_hint *size_hints = blob->data;
    size_t size_hints_len = blob->length / sizeof(size_hints[0]);

    if (!size_hints_len)
        goto fail;

    void *tmp = realloc(drmmode_crtc->cursor.dimensions, size_hints_len * sizeof(drmmode_cursor_dim_rec));
    if (!tmp)
        goto fail;

    drmmode_crtc->cursor.dimensions = tmp;
    drmmode_crtc->cursor.num_dimensions = size_hints_len;

    for (int idx = 0; idx < size_hints_len; idx++)
    {
        struct drm_plane_size_hint size_hint = size_hints[idx];

        drmmode_crtc->cursor.dimensions[idx].width = size_hint.width;
        drmmode_crtc->cursor.dimensions[idx].height = size_hint.height;
    }

    drmmode_crtc->cursor_probed = TRUE;
fail:
    drmModeFreePropertyBlob(blob);
}
#endif

static inline drmmode_cursor_dim_rec
drmmode_get_kms_default(drmmode_ptr drmmode)
{
    uint64_t value = 0;
    drmmode_cursor_dim_rec fallback;

    /* We begin by using the largest supported cursor, and change it later,
       when we can reliably probe for the smallest suppored cursor size */
    int ret1 = drmGetCap(drmmode->fd, DRM_CAP_CURSOR_WIDTH, &value);
    fallback.width = value;

    int ret2 = drmGetCap(drmmode->fd, DRM_CAP_CURSOR_HEIGHT, &value);
    fallback.height = value;

    /* 64x64 is the safest fallback value to use when we can't probe in any other way,
     * as it is the default value that KMS uses.  */
    if (ret1 || ret2) {
        fallback.width  = 64;
        fallback.height = 64;
    }

    return fallback;
}

drmmode_cursor_dim_rec
drmmode_cursor_get_fallback(drmmode_crtc_private_ptr drmmode_crtc)
{
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmmode_cursor_dim_rec fallback;

    const char *cursor_size_str = xf86GetOptValString(drmmode->Options,
                                                      OPTION_CURSOR_SIZE);

    char *height;

    if (!cursor_size_str) {
        return drmmode_get_kms_default(drmmode);
    }

    errno = 0;
    fallback.width = strtol(cursor_size_str, &height, 10);
    if (errno || fallback.width == 0) {
        return drmmode_get_kms_default(drmmode);
    }

    if (*height == '\0') {
        /* we have a width, but don't have a height */
        fallback.height = fallback.width;
        drmmode_crtc->cursor_probed = TRUE;
        return fallback;
    }

    fallback.height = strtol(height + 1, NULL, 10);
    if (errno || fallback.height == 0) {
        return drmmode_get_kms_default(drmmode);
    }

    drmmode_crtc->cursor_probed = TRUE;
    return fallback;
}

static inline void
drmmode_reset_cursor(drmmode_crtc_private_ptr drmmode_crtc)
{
    /* Mark the entire cursor buffer as dirty */
    drmmode_crtc->cursor_glyph_width = 0;
    drmmode_crtc->cursor_glyph_height = 0;
    drmmode_crtc->old_pitch = 0;

    /* If we had any cursor pitches for the old cursor, they are no longer valid now */
    free(drmmode_crtc->cursor_pitches);
    drmmode_crtc->cursor_pitches = NULL;
}

/*
 * This is the old probe method for the minimum cursor size.
 * This is only used if the SIZE_HINTS probe fails.
 */
static void drmmode_probe_cursor_size(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    uint32_t handle = drmmode_crtc->cursor.bo->handle;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmmode_cursor_ptr drmmode_cursor = &drmmode_crtc->cursor;
    int width, height, size;
    int max_width, max_height;
    int min_width, min_height;

    if (drmmode_crtc->cursor_probed) {
        return;
    }

    drmmode_crtc->cursor_probed = TRUE;

    xf86DrvMsg(crtc->scrn->scrnIndex, X_WARNING,
               "Probing the cursor size using the old method\n");

    /* If we're here, we only have one size, the fallback size */
    max_width = drmmode_cursor->dimensions[0].width;
    max_height = drmmode_cursor->dimensions[0].height;

    min_width = max_width;
    min_height = max_height;

    /* probe square min first */
    for (size = 1; size <= max_width &&
             size <= max_height; size *= 2) {
        int ret;

        ret = drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                                handle, size, size, 0, 0);
        if (ret == 0) {
            min_width = size;
            min_height = size;
            break;
        }
    }

    /* check if smaller width works with non-square */
    for (width = 1; width <= size; width *= 2) {
        int ret;

        ret = drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                                handle, width, size, 0, 0);
        if (ret == 0) {
            min_width = width;
            break;
        }
    }

    /* check if smaller height works with non-square */
    for (height = 1; height <= size; height *= 2) {
        int ret;

        ret = drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                                handle, size, height, 0, 0);
        if (ret == 0) {
            min_height = height;
            break;
        }
    }

    drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0, 0, 0, 0, 0);

    if (min_width == max_width && min_height == max_height) {
        xf86DrvMsgVerb(crtc->scrn->scrnIndex, X_INFO, MS_LOGLEVEL_DEBUG,
                       "Cursor size: %dx%d\n",
                       min_width, min_height);

        return;
    }

    drmmode_reset_cursor(drmmode_crtc);

    /*
     * We could add as many sizes as we want here.
     * We want the minimum size to be here, and we need the maximum size to be here,
     * because that's what we initialize the cursor image with, and we could theoretically
     * get cursor glyph sizes that big.
     *
     * There is no problem with multiple sizes being equal here.
     * We want dimensions[i] <= dimensions[i + 1] for all i, but even if
     * this doesn't happen, there shouldn't be any issues.
     */

    int num_dimensions = 1;
    if (min_width > min_height) {
        for(int j = min_height; j <= min_width; j *= 2) {
            num_dimensions++;
        }
    } else {
        for(int j = min_width; j <= min_height; j *= 2) {
            num_dimensions++;
        }

    }

    void *tmp = realloc(drmmode_cursor->dimensions, num_dimensions * sizeof(drmmode_cursor_dim_rec));
    if (!tmp) {
        xf86DrvMsgVerb(crtc->scrn->scrnIndex, X_INFO, MS_LOGLEVEL_DEBUG,
                       "Cursor size: %dx%d\n",
                       max_width, max_height);
        return;
    }

    drmmode_cursor->dimensions = tmp;
    drmmode_cursor->num_dimensions = num_dimensions;

    if (min_width > min_height) {
        int idx = 0;
        for(int j = min_height; j <= min_width; j *= 2) {
            drmmode_cursor->dimensions[idx].width = min_width;
            drmmode_cursor->dimensions[idx].height = j;
            idx++;
        }
    } else {
        int idx = 0;
        for(int j = min_width; j <= min_height; j *= 2) {
            drmmode_cursor->dimensions[idx].width = j;
            drmmode_cursor->dimensions[idx].height = min_height;
            idx++;
        }
    }

    /* maximum size */
    drmmode_cursor->dimensions[num_dimensions - 1].width = max_width;
    drmmode_cursor->dimensions[num_dimensions - 1].height = max_height;

    xf86DrvMsgVerb(crtc->scrn->scrnIndex, X_INFO, MS_LOGLEVEL_DEBUG,
                   "Minimum cursor size: %dx%d\n",
                   min_width, min_height);
}

Bool
drmmode_map_cursor_bos(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int i, ret;

    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        ret = dumb_bo_map(drmmode->fd, drmmode_crtc->cursor.bo);
        if (ret)
            return FALSE;
    }
    return TRUE;
}


/* SW Cursor */

/*
 * We hook the screen's cursor-sprite (swcursor) functions to see if a swcursor
 * is active. When a swcursor is active we disable page-flipping.
 */

static void drmmode_sprite_do_set_cursor(msSpritePrivPtr sprite_priv,
                                         ScrnInfoPtr scrn, int x, int y)
{
    modesettingPtr ms = modesettingPTR(scrn);
    CursorPtr cursor = sprite_priv->cursor;
    Bool sprite_visible = sprite_priv->sprite_visible;

    if (cursor) {
        x -= cursor->bits->xhot;
        y -= cursor->bits->yhot;

        sprite_priv->sprite_visible =
            x < scrn->virtualX && y < scrn->virtualY &&
            (x + cursor->bits->width > 0) &&
            (y + cursor->bits->height > 0);
    } else {
        sprite_priv->sprite_visible = FALSE;
    }

    ms->drmmode.sprites_visible += sprite_priv->sprite_visible - sprite_visible;
}

static void drmmode_sprite_set_cursor(DeviceIntPtr pDev, ScreenPtr pScreen,
                                      CursorPtr pCursor, int x, int y)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);
    msSpritePrivPtr sprite_priv = msGetSpritePriv(pDev, ms, pScreen);

    sprite_priv->cursor = pCursor;
    drmmode_sprite_do_set_cursor(sprite_priv, scrn, x, y);

    ms->SpriteFuncs->SetCursor(pDev, pScreen, pCursor, x, y);
}

static void drmmode_sprite_move_cursor(DeviceIntPtr pDev, ScreenPtr pScreen,
                                       int x, int y)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);
    msSpritePrivPtr sprite_priv = msGetSpritePriv(pDev, ms, pScreen);

    drmmode_sprite_do_set_cursor(sprite_priv, scrn, x, y);

    ms->SpriteFuncs->MoveCursor(pDev, pScreen, x, y);
}

static Bool drmmode_sprite_realize_realize_cursor(DeviceIntPtr pDev,
                                                  ScreenPtr pScreen,
                                                  CursorPtr pCursor)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);

    return ms->SpriteFuncs->RealizeCursor(pDev, pScreen, pCursor);
}

static Bool drmmode_sprite_realize_unrealize_cursor(DeviceIntPtr pDev,
                                                    ScreenPtr pScreen,
                                                    CursorPtr pCursor)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);

    return ms->SpriteFuncs->UnrealizeCursor(pDev, pScreen, pCursor);
}

static Bool drmmode_sprite_device_cursor_initialize(DeviceIntPtr pDev,
                                                    ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);

    return ms->SpriteFuncs->DeviceCursorInitialize(pDev, pScreen);
}

static void drmmode_sprite_device_cursor_cleanup(DeviceIntPtr pDev,
                                                 ScreenPtr pScreen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(scrn);

    ms->SpriteFuncs->DeviceCursorCleanup(pDev, pScreen);
}

miPointerSpriteFuncRec drmmode_sprite_funcs = {
    .RealizeCursor = drmmode_sprite_realize_realize_cursor,
    .UnrealizeCursor = drmmode_sprite_realize_unrealize_cursor,
    .SetCursor = drmmode_sprite_set_cursor,
    .MoveCursor = drmmode_sprite_move_cursor,
    .DeviceCursorInitialize = drmmode_sprite_device_cursor_initialize,
    .DeviceCursorCleanup = drmmode_sprite_device_cursor_cleanup,
};
