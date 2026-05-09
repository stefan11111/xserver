/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2026 stefan11111 <stefan11111@shitposting.expert>
 */

#include <kdrive-config.h>

#include <X11/Xfuncproto.h>

#include "scrnintstr.h"

#include "glamor.h"
#include "glamor_egl.h"

#include "fbdev.h"

#ifdef XV
#include "kxv.h"
#endif

#ifdef WITH_LIBDRM
#include <xf86drm.h>
#endif

#include <errno.h>

char *fbdev_glvnd_provider = NULL;
char *fbdev_dri_path = NULL;

bool fbdev_auto_dri3 = FALSE;
bool fbdev_drm_master = FALSE;
bool es_allowed = TRUE;
bool force_es = FALSE;
bool fbGlamorAllowed = TRUE;
bool fbForceGlamor = FALSE;
bool fbXVAllowed = TRUE;

Bool
fbdevInitAccel(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (fbdev_dri_path) {
        scrpriv->dri_fd = open(fbdev_dri_path, O_RDWR);
        if (scrpriv->dri_fd >= 0) {
#ifdef WITH_LIBDRM
            if (fbdev_drm_master) {
                drmSetMaster(scrpriv->dri_fd);
            } else {
                drmDropMaster(scrpriv->dri_fd);
            }
#endif
        } else {
            LogMessage(X_WARNING, "Could not open %s: %s\n", fbdev_dri_path, strerror(errno));
        }
    } else {
        scrpriv->dri_fd = -1;
    }

    if (scrpriv->dri_fd >= 0) {
        fbdev_auto_dri3 = FALSE;
    }

    glamor_egl_conf_t glamor_egl_conf = {
                                         .screen = pScreen,
                                         .glvnd_vendor = fbdev_glvnd_provider,
                                         .fd = scrpriv->dri_fd,
                                         .auto_dri = fbdev_auto_dri3,
                                         .llvmpipe_allowed = TRUE,
                                         .force_glamor = TRUE,
                                         .es_disallowed = !es_allowed,
                                         .force_es = force_es,
                                        };

    if (!glamor_egl_init_internal(&glamor_egl_conf, NULL)) {
        return FALSE;
    }

    if (fbdev_auto_dri3) {
        scrpriv->dri_fd = glamor_egl_get_fd(pScreen);
    }

    const char *renderer = (const char*)glGetString(GL_RENDERER);

    int flags = GLAMOR_USE_EGL_SCREEN;
    if (!fbGlamorAllowed) {
        flags |= GLAMOR_NO_RENDER_ACCEL;
    } else if (!fbForceGlamor){
        if (!renderer ||
            strstr(renderer, "softpipe") ||
            strstr(renderer, "llvmpipe")) {
            flags |= GLAMOR_NO_RENDER_ACCEL;
        }
    }

    if (scrpriv->dri_fd < 0 ||
        flags & GLAMOR_NO_RENDER_ACCEL) {
        flags |= GLAMOR_NO_DRI3;
    }

    if (!glamor_init(pScreen, flags)) {
        return FALSE;
    }

#ifdef XV
    /* X-Video needs glamor render accel */
    if (fbXVAllowed && !(flags & GLAMOR_NO_RENDER_ACCEL)) {
        kd_glamor_xv_init(pScreen);
    }
#endif

    return TRUE;
}

void
fbdevEnableAccel(ScreenPtr pScreen)
{
#ifdef WITH_LIBDRM
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (fbdev_drm_master && scrpriv->dri_fd >= 0) {
        drmSetMaster(scrpriv->dri_fd);
    }
#endif
}

void
fbdevDisableAccel(ScreenPtr pScreen)
{
#ifdef WITH_LIBDRM
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (fbdev_drm_master && scrpriv->dri_fd >= 0) {
        drmDropMaster(scrpriv->dri_fd);
    }
#endif
}

void
fbdevFiniAccel(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (scrpriv->dri_fd >= 0) {
        close(scrpriv->dri_fd);
    }
}
