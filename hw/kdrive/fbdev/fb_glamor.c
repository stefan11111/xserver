/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2026 stefan11111 <stefan11111@shitposting.expert>
 */

#include <kdrive-config.h>

#include <X11/Xfuncproto.h>

#include <epoxy/egl.h>
#include "dix.h"
#include "scrnintstr.h"
#include "glamor_priv.h"
#include "glamor_egl.h"
#include "glamor_glx_provider.h"
#include "glx_extinit.h"

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

    scrpriv->glamor_egl.fd = -1;
    scrpriv->glamor_egl.glvnd_vendor = fbdev_glvnd_provider;
    scrpriv->glamor_egl.force_es = force_es;
    scrpriv->glamor_egl.es_disallowed = !es_allowed;
    scrpriv->glamor_egl.llvmpipe_allowed = TRUE;
    scrpriv->glamor_egl.GLAMOR_EGL_PRIV_PROC = fbdev_glamor_egl_get_screen_private;

    if (!glamor_egl_init_internal(&scrpriv->glamor_egl, NULL)) {
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

    glamor_egl_screen_init2 = fbdev_glamor_egl_screen_init;

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
