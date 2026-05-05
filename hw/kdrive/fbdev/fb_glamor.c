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

char *fbdev_glvnd_provider = NULL;
char *fbdev_dri_path = NULL;

bool es_allowed = TRUE;
bool force_es = FALSE;
bool fbGlamorAllowed = TRUE;
bool fbForceGlamor = FALSE;
bool fbXVAllowed = TRUE;

Bool
fbdevInitAccel(ScreenPtr pScreen)
{
#ifdef WITH_LIBDRM
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (fbdev_dri_path) {
        scrpriv->dri_fd = open(fbdev_dri_path, O_RDWR);
        if (scrpriv->dri_fd >= 0) {
            drmSetMaster(scrpriv->dri_fd);
        } else {
            perror("open");
        }
    } else {
        scrpriv->dri_fd = -1;
    }
#endif

    glamor_egl_conf_t glamor_egl_conf = {
                                         .screen = pScreen,
                                         .glvnd_vendor = fbdev_glvnd_provider,
#ifdef WITH_LIBDRM
                                         .fd = scrpriv->dri_fd,
#else
                                         .fd = -1;
#endif
                                         .llvmpipe_allowed = TRUE,
                                         .force_glamor = TRUE,
                                         .es_disallowed = !es_allowed,
                                         .force_es = force_es,
                                        };

    if (!glamor_egl_init_internal(&glamor_egl_conf, NULL)) {
        return FALSE;
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

#ifdef WITH_LIBDRM
    if (scrpriv->dri_fd < 0 ||
        flags & GLAMOR_NO_RENDER_ACCEL)
#endif
    {
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
fbdevEnableAccel(ScreenPtr screen)
{
    (void)screen;
}

void
fbdevDisableAccel(ScreenPtr screen)
{
    (void)screen;
}

void
fbdevFiniAccel(ScreenPtr pScreen)
{
#ifdef WITH_LIBDRM
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (scrpriv->dri_fd >= 0) {
        close(scrpriv->dri_fd);
    }
#endif
}
