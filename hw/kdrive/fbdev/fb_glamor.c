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

char *fbdev_glvnd_provider = NULL;

bool es_allowed = TRUE;
bool force_es = FALSE;
bool fbGlamorAllowed = TRUE;
bool fbForceGlamor = FALSE;
bool fbXVAllowed = TRUE;

static glamor_egl_priv_t*
fbdev_glamor_egl_get_screen_private(ScreenPtr pScreen)
{
     KdScreenPriv(pScreen);
     KdScreenInfo *screen = pScreenPriv->screen;
     FbdevScrPriv *scrpriv = screen->driver;

     return &scrpriv->glamor_egl;
}

Bool
fbdevInitAccel(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    memset(&scrpriv->glamor_egl, 0, sizeof(scrpriv->glamor_egl));

    glamor_egl_conf_t glamor_egl_conf = {
                                         .glamor_egl_priv = &scrpriv->glamor_egl,
                                         .GLAMOR_EGL_PRIV_PROC = fbdev_glamor_egl_get_screen_private,
                                         .glvnd_vendor = fbdev_glvnd_provider,
                                         .fd = -1,
                                         .llvmpipe_allowed = TRUE,
                                         .force_glamor = TRUE,
                                         .es_disallowed = !es_allowed,
                                         .force_es = force_es,
                                        };

    if (!glamor_egl_init2(&glamor_egl_conf, NULL)) {
        return FALSE;
    }

    const char *renderer = (const char*)glGetString(GL_RENDERER);

    int flags = GLAMOR_USE_EGL_SCREEN | GLAMOR_NO_DRI3;
    if (!fbGlamorAllowed) {
        flags |= GLAMOR_NO_RENDER_ACCEL;
    } else if (!fbForceGlamor){
        if (!renderer ||
            strstr(renderer, "softpipe") ||
            strstr(renderer, "llvmpipe")) {
            flags |= GLAMOR_NO_RENDER_ACCEL;
        }
    }

    if (!glamor_init(pScreen, flags)) {
        glamor_egl_cleanup(&scrpriv->glamor_egl);
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
    /* Handled by CloseScreen */
    (void)pScreen;
}
