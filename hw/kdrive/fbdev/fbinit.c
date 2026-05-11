/*
 * Copyright © 1999 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <kdrive-config.h>
#include "fbdev.h"

#include "os/cmdline.h"
#include "os/ddx_priv.h"

#include <string.h>

static FbScreenConf *fbCurrScreen = NULL;

void
InitCard(char *name)
{
    fbCurrScreen = XNFalloc(sizeof(*fbCurrScreen));
    *fbCurrScreen = (FbScreenConf) {
                                    .fbdevDevicePath = NULL,
                                    .fbDisableShadow = FALSE,
#ifdef GLAMOR
                                    .fbdev_glvnd_provider = NULL,

                                    .fbdev_dri_path = NULL,
                                    .fbdev_auto_dri3 = FALSE,
                                    .fbdev_drm_master = FALSE,

                                    .es_allowed = TRUE,
                                    .force_es = FALSE,

                                    .fbGlamorAllowed = TRUE,
                                    .fbForceGlamor = FALSE,
#ifdef XV
                                    .fbXVAllowed = TRUE,
#endif
#endif
                                   };

    KdCardInfoAdd(&fbdevFuncs, fbCurrScreen);
}

#if INPUTTHREAD
/** This function is called in Xserver/os/inputthread.c when starting
    the input thread. */
void
ddxInputThreadInit(void)
{
}
#endif

void
InitOutput(int argc, char **argv)
{
    KdInitOutput(argc, argv);
}

void
InitInput(int argc, char **argv)
{
    KdOsAddInputDrivers();
    KdAddConfigInputDrivers();
    KdInitInput();
}

void
CloseInput(void)
{
    KdCloseInput();
}

void
ddxUseMsg(void)
{
    KdUseMsg();
    ErrorF("\nXfbdev Device Usage:\n");
    ErrorF
        ("-fb <path>           Framebuffer device to use. Defaults to /dev/fb0\n");
    ErrorF
        ("-dri [path|auto]     Optional drm device path to use\n");
    ErrorF
        ("-drm-master          Enable master permissions on the fd used for dri\n");
    ErrorF
        ("-noshadow            Disable the ShadowFB layer if possible\n");
    ErrorF
        ("-glamor              Force enable glamor render acceleration if possible\n");
    ErrorF
        ("-noglamor            Force disable glamor render acceleration\n");
    ErrorF
        ("-glvendor <string>   Suggest what glvnd vendor library should be used\n");
    ErrorF
        ("-force-gl            Force glamor to only use GL contexts\n");
    ErrorF
        ("-force-es            Force glamor to only use GLES contexts\n");
    ErrorF
        ("-noxv                Disable X-Video support\n");
    ErrorF("\n");
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
    if (!fbCurrScreen || !strcmp(argv[i], "-screen")) {
        /* Put each screen on a separate card */
        int implicit_first_screen = !fbCurrScreen;
        InitCard(NULL);
        if (implicit_first_screen) {
            /* This is what KdInitOutput would have done */
            KdCardInfo *card = KdCardInfoLast();
            KdScreenInfo *screen = KdScreenInfoAdd(card);
            KdParseScreen(screen, NULL);
        }
    }

    if (!strcmp(argv[i], "-fb")) {
        if (i + 1 < argc) {
            fbCurrScreen->fbdevDevicePath = argv[i + 1];
            return 2;
        }
        UseMsg();
        exit(1);
    }

    if (!strcmp(argv[i], "-noshadow")) {
        fbCurrScreen->fbDisableShadow = TRUE;
        return 1;
    }

#ifdef GLAMOR
    if (!strcmp(argv[i], "-glamor")) {
        fbCurrScreen->fbForceGlamor = TRUE;
        return 1;
    }

    if (!strcmp(argv[i], "-noglamor")) {
        fbCurrScreen->fbGlamorAllowed = FALSE;
        return 1;
    }

    if (!strcmp(argv[i], "-glvendor")) {
        if (i + 1 < argc) {
            fbCurrScreen->fbdev_glvnd_provider = strdup(argv[i + 1]);
            return 2;
        }
        UseMsg();
        exit(1);
    }

    if (!strcmp(argv[i], "-dri")) {
        if (i + 1 < argc) {
            if (argv[i + 1][0] == '-' || !strcmp(argv[i + 1], "auto")) {
                fbCurrScreen->fbdev_auto_dri3 = TRUE;
            } else {
                fbCurrScreen->fbdev_dri_path = strdup(argv[i + 1]);
            }
            return 2;
        } else {
            fbCurrScreen->fbdev_auto_dri3 = TRUE;
            return 1;
        }
    }

    if (!strcmp(argv[i], "-drm-master")) {
        fbCurrScreen->fbdev_drm_master = TRUE;
        return 1;
    }

    if (!strcmp(argv[i], "-force-gl")) {
        fbCurrScreen->es_allowed = FALSE;
        return 1;
    }

    if (!strcmp(argv[i], "-force-es")) {
        fbCurrScreen->force_es = TRUE;
        return 1;
    }

#ifdef XV
    if (!strcmp(argv[i], "-noxv")) {
        fbCurrScreen->fbXVAllowed = FALSE;
        return 1;
    }
#endif
#endif

    return KdProcessArgument(argc, argv, i);
}

KdCardFuncs fbdevFuncs = {
    .cardinit         = fbdevCardInit,
    .scrinit          = fbdevScreenInit,
    .initScreen       = fbdevInitScreen,
    .finishInitScreen = fbdevFinishInitScreen,
    .createRes        = fbdevCreateResources,
    .preserve         = fbdevPreserve,
    .enable           = fbdevEnable,
    .dpms             = fbdevDPMS,
    .disable          = fbdevDisable,
    .restore          = fbdevRestore,
    .scrfini          = fbdevScreenFini,
    .cardfini         = fbdevCardFini,

    /* no cursor funcs */

#ifdef GLAMOR
    .initAccel        = fbdevInitAccel,
    .enableAccel      = fbdevEnableAccel,
    .disableAccel     = fbdevDisableAccel,
    .finiAccel        = fbdevFiniAccel,
#endif

    .getColors        = fbdevGetColors,
    .putColors        = fbdevPutColors,

    /* no closescreen func */
};
