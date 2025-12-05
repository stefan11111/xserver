/*
 * Copyright (c) 2000-2001 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/*
 * This file contains the interfaces to the bus-specific code
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/X.h>
#include "os.h"
#include "xf86.h"
#include "xf86Priv.h"

#include "xf86Bus.h"
#include "xf86_OSproc.h"

int fbSlotClaimed = 0;

static Bool
xf86_check_fb_slot(GDevPtr dev, const char* driver_name)
{
    if (dev == NULL) {
        return FALSE;
    }

#ifdef XSERVER_LIBPCIACCESS
    int i, j, k;
    struct pci_device fake, *pdev;

    if (dev->busID &&
        xf86ParsePciBusString(dev->busID, &i, &j, &k)) {
        /**
         * These option names are driver specific.
         * If we need to handle another driver claiming fb slots,
         * we'll have to add it's device path option name here.
         */
        const char* ms_path = xf86FindOptionValue(dev->options, "kmsdev");
        const char* fb_path = xf86FindOptionValue(dev->options, "fbdev");
        const char* device_path = NULL;
        if (ms_path) {
            device_path = ms_path;
        } else if (fb_path) {
            device_path = fb_path;
        } else {
            /* See the fbdevhw module for more info */
            device_path = getenv("FRAMEBUFFER");
        }

        if (device_path) {
            /* If we have both a busID and a device path configured, assume the user knows what they are doing */
            LogMessageVerb(X_INFO, 0, "Using driver: %s in framebuffer mode without collision checks "
                                      "for device: %s with busID: %s.\n", driver_name, device_path, dev->busID);
            return TRUE;
        }

        fake.domain = 0;
        fake.bus = i;
        fake.dev = j;
        fake.func = k;
    } else {
        if (pciSlotClaimed
#ifdef XSERVER_PLATFORM_BUS
            || platformSlotClaimed
#endif
#if defined(__sparc__) || defined(__sparc)
            || sbusSlotClaimed
#endif
        ) {
            /**
             * XXX This is a hack XXX
             * Drivers like fbdev and modesetting, when in framebuffer mode, can drive almos anything.
             * However, this also means that, when autoconfiguring, the X server may pick
             * one of these drivers and use it alongside another DDX driver, causing issues.
             */
            LogMessageVerb(X_ERROR, 0, "Refusing to use driver: %s in framebuffer mode.\n", driver_name);
            LogMessageVerb(X_ERROR, 0, "Please specify busIDs for all framebuffer devices.\n");
            LogMessageVerb(X_ERROR, 0, "If a slot collision is desired, please specify busIDs and device paths "
                                       "for all framebuffer devices where collisions are desired.\n");
            return FALSE;
        }

        /* If no slots were claimed, we can't conflict with anything */
        return TRUE;
    }

    for (i = 0; i < xf86NumEntities; i++) {
        const EntityPtr pent = xf86Entities[i];

        if (pent->numInstances <= 0) {
        /* All devices are unclaimed, ignoring this entity */
            continue;
        }

        if (fake.domain != PCI_MATCH_ANY) {
            pdev = xf86GetPciInfoForEntity(i);
            if (pdev != NULL) {
                if (MATCH_PCI_DEVICES(pdev, &fake))
                    return FALSE;
            }
        }
    }
#endif
    return TRUE;
}


int
xf86ClaimFbSlot(DriverPtr drvp, int chipset, GDevPtr dev, Bool active)
{
    EntityPtr p;
    int num;

    if (xf86_check_fb_slot(dev, drvp->driverName)) {
        num = xf86AllocateEntity();
        p = xf86Entities[num];
        p->driver = drvp;
        p->chipset = 0;
        p->bus.type = BUS_NONE;
        p->active = active;
        p->inUse = FALSE;
        xf86AddDevToEntity(num, dev);

        fbSlotClaimed++;
        return num;
    } else {
        return -1;
    }
}
