/*
 * usb.c - usb probing/module loading functionality
 *
 * Erik Troan <ewt@redhat.com>
 * Matt Wilson <msw@redhat.com>
 * Michael Fulbright <msf@redhat.com>
 * Jeremy Katz <katzj@redhat.com>
 *
 * Copyright 1999 - 2003 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <kudzu/kudzu.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "loader.h"
#include "log.h"
#include "modules.h"
#include "moduledeps.h"

#include "../isys/imount.h"


/* This forces a pause between initializing usb and trusting the /proc 
   stuff */
static void sleepUntilUsbIsStable(void) {
    struct stat sb;
    time_t last = 0;
    int i, count = 0;

    /* sleep for a maximum of 20 seconds, minimum of 2 seconds */
    logMessage("waiting for usb to become stable...");
    for (i = 0; i < 20; i++) {
	stat("/proc/bus/usb/devices", &sb);
	if (last == sb.st_mtime) {
	    count++;
	    /* if we get the same mtime twice in a row, should be
	       good enough to use now */
	    if (count > 1)
		break;
	} else {
	    /* if we didn't match mtimes, reset the stability counter */
	    count = 0;
	}
	last = sb.st_mtime;
	sleep(1);
    }
    logMessage("%d seconds.", i);
}

int usbInitialize(moduleList modLoaded, moduleDeps modDeps,
			 moduleInfoSet modInfo, int flags) {
    struct device ** devices;
    char * buf;
    int i;
    int loadUsbStorage = 0;

    if (FL_NOUSB(flags)) return 0;

    logMessage("looking for usb controllers");

    devices = probeDevices(CLASS_USB, BUS_PCI, 0);

    if (!devices) {
	logMessage("no usb controller found");
	return 0;
    }

    /* JKFIXME: if we looked for all of them, we could batch this up and it
     * would be faster */
    for (i=0; devices[i]; i++) {
        logMessage("found USB controller %s", devices[i]->driver);

        if (mlLoadModuleSet(devices[i]->driver, modLoaded, modDeps,
                            modInfo, flags)) {
            /* dont return, just keep going. */
            /* may have USB built into kernel */
            /* return 1; */
        }

    }
    free(devices);


    if (FL_TESTING(flags)) return 0;

    if (doPwMount("/proc/bus/usb", "/proc/bus/usb", "usbfs", 0, 0, 
		  NULL, NULL, 0, 0))
	logMessage("failed to mount device usbfs: %s", strerror(errno));

    /* sleep so we make sure usb devices get properly enumerated.
       that way we should block when initializing each usb driver until
       the device is ready for use */
    sleepUntilUsbIsStable();

    if (FL_NOUSBSTORAGE(flags))
        loadUsbStorage = 0;
    else {
        devices = probeDevices(CLASS_UNSPEC, BUS_USB, PROBE_ALL);
        if (devices) {
            for (i = 0; devices[i]; i++) {
                if (!strcmp(devices[i]->driver, "usb-storage")) {
                    loadUsbStorage = 1;
                    break;
                }
            }
            free(devices);
        }
    }

    buf = alloca(40);
    sprintf(buf, "hid:keybdev:%s", (loadUsbStorage ? ":usb-storage" : ""));
    mlLoadModuleSet(buf, modLoaded, modDeps, modInfo, flags);
    sleep(1);

    return 0;
}

void usbInitializeMouse(moduleList modLoaded, moduleDeps modDeps,
                        moduleInfoSet modInfo, int flags) {
    extern struct moduleBallLocation * secondStageModuleLocation;

    if (FL_NOUSB(flags)) return;

    if (access("/proc/bus/usb/devices", R_OK)) return;
    
    logMessage("looking for USB mouse...");
    if (probeDevices(CLASS_MOUSE, BUS_USB, PROBE_ALL)) {
        logMessage("USB mouse found, loading mousedev module");
        if (mlLoadModuleSetLocation("mousedev", modLoaded, modDeps, modInfo, flags, secondStageModuleLocation)) {
            logMessage ("failed to loading mousedev module");
            return;
        }
    }
}

