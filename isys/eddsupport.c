/*
 * eddsupport.c - handling of mapping disk drives in Linux to disk drives
 * according to the BIOS using the edd kernel module
 *
 * Copyright 2004 Dell, Inc., Red Hat, Inc.
 *
 * Rezwanul_Kabir@Dell.com
 * Jeremy Katz <katzj@redhat.com>
 *
 * This software may be freely redistributed under the terms of the GNU
 * general public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <linux/types.h>

#include <kudzu/kudzu.h>


#include "eddsupport.h"
#include "isys.h"



#define EDD_DIR "/sys/firmware/edd"
#define SIG_FILE "mbr_signature"
#define MBRSIG_OFFSET 0x1b8

#define HASH_TABLE_SIZE 17

struct diskMapEntry{
    uint32_t key;
    char *diskname;
    struct diskMapEntry *next;
};

struct diskMapTable {
    struct diskMapEntry **table;
    int tableSize;
};

static struct diskMapTable *mbrSigToName = NULL;
static int diskHashInit = 0;



static struct diskMapTable*  initializeHashTable(int);
static int insertHashItem(struct diskMapTable *, struct diskMapEntry *);
static struct diskMapEntry* lookupHashItem(struct diskMapTable *, uint32_t);
static int addToHashTable(struct diskMapTable *, uint32_t , char *);
static struct device ** createDiskList();
static int mapBiosDisks(struct diskMapTable * , const char *);
static int readDiskSig(char *,  uint32_t *);
static struct diskMapTable* uniqueSignatureExists(struct device **);
static int readMbrSig(char *, uint32_t *);

/* This is the top level function that creates a disk list present in the
 * system, checks to see if unique signatures exist on the disks at offset 
 * 0x1b8.  If a unique signature exists then it will map BIOS disks to their 
 * corresponding hd/sd device names.  Otherwise, we'll avoid mapping drives.
 */

int probeBiosDisks() {
    struct device ** devices = NULL;
    struct diskMapTable *diskSigToName;

    devices = createDiskList();
    if(!devices){
        fprintf(stderr, "No disks!\n");
        return -1;
    }

    if(!(diskSigToName = uniqueSignatureExists(devices))) {
        fprintf(stderr, "WARNING: Unique disk signatures don't exist\n");
        return -1;
    } else {
        if(!mapBiosDisks(diskSigToName, EDD_DIR)){
            fprintf(stderr, "WARNING: couldn't map BIOS disks\n");
            return -1;
        }
    }
    return 0;
}


static struct device ** createDiskList(){
    return probeDevices (CLASS_HD, BUS_UNSPEC, PROBE_ALL);
}

static struct diskMapTable * uniqueSignatureExists(struct device **devices) {
    uint32_t current_sig, headsig;
    struct device **devhead, **devlist;
    int i;
    struct diskMapTable *hashTable;

    hashTable = initializeHashTable(HASH_TABLE_SIZE);
    if(!hashTable){
        fprintf(stderr, "Error initializing diskSigToName table\n");
        return NULL;
    }

    for (devhead = devices, i = 0; (*devhead) != NULL; devhead++, i++) {
        if (readDiskSig((*devhead)->device, &headsig) < 0) {
            return NULL;
        }

        for (devlist = devhead + 1; (*devlist) != NULL; devlist++) {
            if (readDiskSig((*devlist)->device, &current_sig) < 0)
                return NULL;

            if (headsig == current_sig)
                return NULL;
        } 

        if(!addToHashTable(hashTable, headsig, (*devhead)->device))
            return NULL;
    }

    return hashTable;
}


static int readDiskSig(char *device, uint32_t *disksig) {
    int fd, rc;

    if (devMakeInode(device, "/tmp/biosdev")){
        return -1;
    }

    fd = open("/tmp/biosdev", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening devce %s: %s\n ", device, 
                strerror(errno));
        return -1;
    }

    rc = lseek(fd, MBRSIG_OFFSET, SEEK_SET);
    if (rc < 0){
        close(fd);

        fprintf(stderr, "Error seeking to MBRSIG_OFFSET in %s: %s\n", 
                device, strerror(errno));
        return -1;
    }

    rc = read(fd, disksig, sizeof(uint32_t));
    if (rc < sizeof(uint32_t)) {
        close(fd);

        fprintf(stderr, "Failed to read signature from %s\n", device); 
        return -1;
    }

    unlink("/tmp/biosdev");
    return 0;
}

static int mapBiosDisks(struct diskMapTable* hashTable,const char *path) {
    DIR *dirHandle;
    struct dirent *entry;
    char * sigFileName;
    uint32_t mbrSig, biosNum;
    struct diskMapEntry *hashItem;

    dirHandle = opendir(path);
    if(!dirHandle){
        fprintf(stderr, "Failed to open directory %s: %s\n", path, 
                strerror(errno));
        return 0;
    }

    mbrSigToName = initializeHashTable(HASH_TABLE_SIZE);
    if(!mbrSigToName){
        fprintf(stderr, "Error initializing mbrSigToName table\n");
        return 0;
    }

    while ((entry = readdir(dirHandle)) != NULL) {
        if(!strncmp(entry->d_name,".",1) || !strncmp(entry->d_name,"..",2)) {
            continue;
        }
        sscanf((entry->d_name+9), "%x", &biosNum);
        
        sigFileName = malloc(strlen(path) + strlen(entry->d_name) + 20);
        sprintf(sigFileName, "%s/%s/%s", path, entry->d_name, SIG_FILE);
        if (readMbrSig(sigFileName, &mbrSig) == 0) {
            hashItem = lookupHashItem(hashTable, mbrSig);
            if (!hashItem)
                return 0;
            if(!addToHashTable(mbrSigToName, (uint32_t)biosNum, 
                               hashItem->diskname))
                return 0;
        }
    }
    closedir(dirHandle);
    return 1;
} 


static int readMbrSig(char *filename, uint32_t *int_sig){
    FILE* fh;

    fh = fopen(filename,"r");
    if(fh == NULL) {
        fprintf(stderr, "Error opening mbr_signature file %s: %s\n", filename,
                strerror(errno));
        return -1;
    }
    fseek(fh, 0, SEEK_SET);
    if (fscanf(fh, "%x", int_sig) != 1) {
        fprintf(stderr, "Error reading %s\n", filename);
        fclose(fh);
        return -1;
    }

    fclose(fh);
    return 0;
}                                                   


static struct diskMapTable* initializeHashTable(int size) {
    struct diskMapTable *hashTable;

    hashTable = malloc(sizeof(struct diskMapTable));
    hashTable->tableSize = size;
    hashTable->table = malloc(sizeof(struct diskMapEntry *) * size);
    memset(hashTable->table,0,(sizeof(struct diskMapEntry *) * size));
    return hashTable;
}


static int insertHashItem(struct diskMapTable *hashTable,
                          struct diskMapEntry *hashItem) {
    int index;

    index = (hashItem->key) % (hashTable->tableSize);

    if(hashTable->table[index] == NULL){
        hashTable->table[index] = hashItem;
        return index;
    } else {
        hashItem->next = hashTable->table[index];
        hashTable->table[index] = hashItem;
        return index;
    }
}


static struct diskMapEntry * lookupHashItem(struct diskMapTable *hashTable,
                                            uint32_t itemKey) {
    int index;
    struct diskMapEntry *hashItem;

    index = itemKey % (hashTable->tableSize);
    for (hashItem = hashTable->table[index]; 
         (hashItem != NULL) && (hashItem->key != itemKey); 
         hashItem = hashItem->next) { 
        ;
    }
    return hashItem;
}


static int addToHashTable(struct diskMapTable *hashTable, 
                          uint32_t itemKey, char *diskName) {
    int index;
    struct diskMapEntry *diskSigToNameEntry;

    diskSigToNameEntry = malloc(sizeof(struct diskMapEntry));
    diskSigToNameEntry->next = NULL;
    diskSigToNameEntry->key = itemKey;
    diskSigToNameEntry->diskname = diskName;

    if ((index = insertHashItem(hashTable, diskSigToNameEntry)) < 0){
        fprintf(stderr, "Unable to insert item\n");
        return 0;
    } else {
        return 1;
    }
}


char * getBiosDisk(char *biosStr) {
    uint32_t biosNum;

    if (diskHashInit == 0) {
        probeBiosDisks();
        diskHashInit = 1;
    }

    if (mbrSigToName == NULL)
        return NULL;

    sscanf(biosStr,"%x",&biosNum);
    return lookupHashItem(mbrSigToName, biosNum)->diskname;
}
