/* 
 * getparts.c - functions associated with getting partitions for a disk
 *
 * Michael Fulbright <msf@redhat.com>
 * Jeremy Katz <katzj@redhat.com>
 *
 * Copyright 1997 - 2004 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include "log.h"

/* see if this is a partition name or not */
static int isPartitionName(char *pname) {

    /* if it doesnt start with a alpha its not one */
    if (!isalpha(*pname))
	return 0;

    /* if it has a '/' in it then treat it specially */
    if (strchr(pname, '/') && !strstr(pname, "iseries")) {
	/* assume its either a /dev/ida/ or /dev/cciss device */
	/* these have form of c?d?p? if its a partition */
	return strchr(pname, 'p') != NULL;
    } else {
	/* if it ends with a digit we're ok */
	return isdigit(pname[strlen(pname)-1]);
    }
}

/* return NULL terminated array of pointers to names of partitons in
 * /proc/partitions
 */
char **getPartitionsList(char * disk) {
    FILE *f;
    int numfound = 0;
    char **rc=NULL;

    f = fopen("/proc/partitions", "r");
    if (!f) {
	logMessage("getPartitionsList: could not open /proc/partitions");
	return NULL;
    }

    /* read through /proc/partitions and parse out partitions */
    while (1) {
	char *tmpptr, *pptr;
	char tmpstr[4096];

	tmpptr = fgets(tmpstr, sizeof(tmpstr), f);

	if (tmpptr) {
	    char *a, *b;
	    int toknum = 0;

	    a = tmpstr;
	    while (1) {
		b = strsep(&a, " \n");

		/* if no fields left abort */
		if (!b)
		    break;

		/* if field was empty means we hit another delimiter */
		if (!*b)
		    continue;

		/* make sure this is a valid partition line, should start */
		/* with a numeral */
		if (toknum == 0) {
		    if (!isdigit(*b))
			break;
		} else if (toknum == 2) {
		    /* if size is exactly 1 then ignore it as an extended */
		    if (!strcmp(b, "1"))
			break;
		} else if (toknum == 3) {
		    /* this should be the partition name */
		    /* now we need to see if this is the block device or */
		    /* actually a partition name                         */
		    if (!isPartitionName(b))
			break;

                    /* make sure that either we don't care about the disk
                     * or it's this one */
                    if ((disk != NULL) && (strncmp(disk, b, strlen(disk))))
                        break;

		    /* we found a partition! */
		    pptr = (char *) malloc(strlen(b) + 7);
		    sprintf(pptr, "/dev/%s", b);

		    if (!rc) {
			rc = (char **) malloc(2*sizeof(char *));
		        rc[0] = pptr;
			rc[1] = NULL;
		    } else {
			int idx;
			
			rc = (char **) realloc(rc, (numfound+2)*sizeof(char *));
			idx = 0;
			while (idx < numfound) {
			    if (strcmp(pptr, rc[idx]) < 0)
				break;

			    idx++;
			}

			/* move existing out of way if necessary */
			if (idx != numfound)
			    memmove(rc+idx+1, rc+idx, (numfound-idx)*sizeof(char *));

			rc[idx] = pptr;
			rc[numfound+1] = NULL;
		    }
		    numfound++;
		    break;
		}
		toknum++;
	    }
	} else {
	    break;
	}
    }

    fclose(f);

    return rc;
}

/* returns length of partitionlist */
int lenPartitionsList(char **list) {
    char **part;
    int  rc;

    if (!list) return 0;
    for (rc = 0, part = list; *part; rc++, part++);

    return rc;
}

/* frees partition list */
void freePartitionsList(char **list) {
    char **part;

    if (!list)
	return;

    for (part = list; *part; part++)
	if (*part)
	    free(*part);

    free(list);
}
