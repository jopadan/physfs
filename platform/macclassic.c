/*
 * MacOS Classic support routines for PhysicsFS.
 *
 * Please see the file LICENSE in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <alloca.h>

/*
 * Most of the API calls in here are, according to ADC, available since
 *  MacOS 8.1. I don't think I used any MacOS 9 or CarbonLib-specific
 *  functions. There might be one or two 8.5 calls, and perhaps when the
 *  ADC docs say "Available in MacOS 8.1" they really mean "this works
 *  with System 6, but we don't want to hear about it at this point."
 *
 * IsAliasFile() showed up in MacOS 8.5. You can duplicate this code with
 *  PBGetCatInfoSync(), which is an older API, if you hope the bits in the
 *  catalog info never change (which they won't for, say, MacOS 8.1).
 *  See Apple Technote FL-30:
 *    http://developer.apple.com/technotes/fl/fl_30.html
 *
 * If you want to use weak pointers and Gestalt, and choose the correct
 *  code to use during __PHYSFS_platformInit(), I'll accept a patch, but
 *  chances are, it wasn't worth the time it took to write this, let alone
 *  implement that.
 */


/*
 * Please note that I haven't tried this code with CarbonLib or under
 *  MacOS X at all. The code in unix.c is known to work with Darwin,
 *  and you may or may not be better off using that, especially since
 *  mutexes are no-ops in this file. Patches welcome.
 */
#ifdef __PHYSFS_CARBONIZED__  /* this is currently not defined anywhere. */
#include <Carbon.h>
#else
#include <OSUtils.h>
#include <Processes.h>
#include <Files.h>
#include <TextUtils.h>
#include <Resources.h>
#include <MacMemory.h>
#include <Events.h>
#include <DriverGestalt.h>
#include <Aliases.h>
#endif

#define __PHYSICSFS_INTERNAL__
#include "physfs_internal.h"


const char *__PHYSFS_platformDirSeparator = ":";

static struct ProcessInfoRec procInfo;
static FSSpec procfsspec;

int __PHYSFS_platformInit(void)
{
    OSErr err;
    ProcessSerialNumber psn;
    BAIL_IF_MACRO(GetCurrentProcess(&psn) != noErr, ERR_OS_ERROR, 0);
    memset(&procInfo, '\0', sizeof (ProcessInfoRec));
    memset(&procfsspec, '\0', sizeof (FSSpec));
    procInfo.processInfoLength = sizeof (ProcessInfoRec);
    procInfo.processAppSpec = &procfsspec;
    err = GetProcessInformation(&psn, &procInfo);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    return(1);  /* we're golden. */
} /* __PHYSFS_platformInit */


int __PHYSFS_platformDeinit(void)
{
    return(1);  /* always succeed. */
} /* __PHYSFS_platformDeinit */


/* 
 * CD detection code is borrowed from Apple Technical Q&A DV18.
 *  http://developer.apple.com/qa/dv/dv18.html
 */
char **__PHYSFS_platformDetectAvailableCDs(void)
{
    DriverGestaltParam pb;
    DrvQEl *dqp;
    OSErr status;
    char **retval = (char **) malloc(sizeof (char *));
    int cd_count = 1;

    BAIL_IF_MACRO(retval == NULL, ERR_OUT_OF_MEMORY, NULL);

    *retval = NULL;

    pb.csCode = kDriverGestaltCode;
    pb.driverGestaltSelector = kdgDeviceType;
    dqp = (DrvQEl *) GetDrvQHdr()->qHead;

    while (dqp != NULL)
    {
        pb.ioCRefNum = dqp->dQRefNum;
        pb.ioVRefNum = dqp->dQDrive;
        status = PBStatusSync((ParmBlkPtr) &pb);
        if ((status == noErr) && (pb.driverGestaltResponse == kdgCDType))
        {
            Str63 volName;
            HParamBlockRec hpbr;
            memset(&hpbr, '\0', sizeof (HParamBlockRec));
            hpbr.volumeParam.ioNamePtr = volName;
            hpbr.volumeParam.ioVRefNum = dqp->dQDrive;
            hpbr.volumeParam.ioVolIndex = 0;
            if (PBHGetVInfoSync(&hpbr) == noErr)
            {
                char **tmp = realloc(retval, sizeof (char *) * cd_count + 1);
                if (tmp)
                {
                    char *str = (char *) malloc(volName[0] + 1);
                    retval = tmp;
                    if (str != NULL)
                    {
                        memcpy(str, &volName[1], volName[0]);
                        str[volName[0]] = '\0';
                        retval[cd_count-1] = str;
                        cd_count++;
                    } /* if */
                } /* if */
            } /* if */
        } /* if */

        dqp = (DrvQEl *) dqp->qLink;
    } /* while */

    retval[cd_count - 1] = NULL;
    return(retval);
} /* __PHYSFS_platformDetectAvailableCDs */


static char *convFSSpecToPath(FSSpec *spec, int includeFile)
{
    char *ptr;
    char *retval = NULL;
    UInt32 retLength = 0;
    CInfoPBRec infoPB;
    Str255 str255;

    str255[0] = spec->name[0];
    memcpy(&str255[1], &spec->name[1], str255[0]);

    memset(&infoPB, '\0', sizeof (CInfoPBRec));
    infoPB.dirInfo.ioNamePtr = str255;          /* put name in here.         */
    infoPB.dirInfo.ioVRefNum = spec->vRefNum;   /* ID of bin's volume.       */ 
    infoPB.dirInfo.ioDrParID = spec->parID;     /* ID of bin's dir.          */
    infoPB.dirInfo.ioFDirIndex = (includeFile) ? 0 : -1;

    /* walk the tree back to the root dir (volume), building path string... */
    do
    {
        /* check parent dir of what we last looked at... */
        infoPB.dirInfo.ioDrDirID = infoPB.dirInfo.ioDrParID;
        if (PBGetCatInfoSync(&infoPB) != noErr)
        {
            if (retval != NULL)
                free(retval);
            BAIL_MACRO(ERR_OS_ERROR, NULL);
        } /* if */

        infoPB.dirInfo.ioFDirIndex = -1;  /* look at parent dir next time. */

        /* allocate more space for the retval... */
        retLength += str255[0] + 1; /* + 1 for a ':' or null char... */
        ptr = (char *) malloc(retLength);
        if (ptr == NULL)
        {
            if (retval != NULL)
                free(retval);
            BAIL_MACRO(ERR_OUT_OF_MEMORY, NULL);
        } /* if */

        /* prepend new dir to retval and cleanup... */
        memcpy(ptr, &str255[1], str255[0]);
        ptr[str255[0]] = '\0';  /* null terminate it. */
        if (retval != NULL)
        {
            strcat(ptr, ":");
            strcat(ptr, retval);
            free(retval);
        } /* if */
        retval = ptr;
    } while (infoPB.dirInfo.ioDrDirID != fsRtDirID);

    return(retval);
} /* convFSSpecToPath */


char *__PHYSFS_platformCalcBaseDir(const char *argv0)
{
    FSSpec spec;
    
    /* Get the name of the binary's parent directory. */
    FSMakeFSSpec(procfsspec.vRefNum, procfsspec.parID, procfsspec.name, &spec);
    return(convFSSpecToPath(&spec, 0));
} /* __PHYSFS_platformCalcBaseDir */


char *__PHYSFS_platformGetUserName(void)
{
    char *retval = NULL;
    StringHandle strHandle;
    short origResourceFile = CurResFile();

    /* use the System resource file. */
    UseResFile(0);
    /* apparently, -16096 specifies the username. */
    strHandle = GetString(-16096);
    UseResFile(origResourceFile);
    BAIL_IF_MACRO(strHandle == NULL, ERR_OS_ERROR, NULL);

    HLock((Handle) strHandle);
    retval = (char *) malloc((*strHandle)[0] + 1);
    if (retval == NULL)
    {
        HUnlock((Handle) strHandle);
        BAIL_MACRO(ERR_OUT_OF_MEMORY, NULL);
    } /* if */
    memcpy(retval, &(*strHandle)[1], (*strHandle)[0]);
    retval[(*strHandle)[0]] = '\0';  /* null-terminate it. */
    HUnlock((Handle) strHandle);

    return(retval);
} /* __PHYSFS_platformGetUserName */


char *__PHYSFS_platformGetUserDir(void)
{
#if 0
    return(NULL);  /* bah...use default behaviour, I guess. */
#else
    /* (Hmm. Default behaviour is broken in the base library.  :)  )  */
    return(__PHYSFS_platformCalcBaseDir(NULL));
#endif
} /* __PHYSFS_platformGetUserDir */


PHYSFS_uint64 __PHYSFS_platformGetThreadID(void)
{
    return(1);  /* single threaded. */
} /* __PHYSFS_platformGetThreadID */


int __PHYSFS_platformStricmp(const char *x, const char *y)
{
    extern int _stricmp(const char *, const char *);
    return(_stricmp(x, y));  /* (*shrug*) */
} /* __PHYSFS_platformStricmp */


static OSErr fnameToFSSpecNoAlias(const char *fname, FSSpec *spec)
{
    OSErr err;
    Str255 str255;
    int needColon = (strchr(fname, ':')  == NULL);
    int len = strlen(fname) + ((needColon) ? 1 : 0);
    if (len > 255)
        return(bdNamErr);

    /* !!! FIXME: What happens with relative pathnames? */

    str255[0] = len;
    memcpy(&str255[1], fname, len);

    /* probably just a volume name, which seems to need a ':' at the end. */
    if (needColon)
        str255[len] = ':';

    err = FSMakeFSSpec(0, 0, str255, spec);
    return(err);
} /* fnameToFSSpecNoAlias */


/* !!! FIXME: This code is pretty heinous. */
static OSErr fnameToFSSpec(const char *fname, FSSpec *spec)
{
    Boolean alias = 0;
    Boolean folder = 0;
    OSErr err = fnameToFSSpecNoAlias(fname, spec);

    if (err == dirNFErr)  /* might be an alias in the middle of the path. */
    {
        /* 
         * Has to be at least two ':' chars, or we wouldn't get a
         *  dir-not-found condition. (no ':' means it was just a volume,
         *  just one ':' means we would have gotten a fnfErr, if anything.
         */
        char *ptr;
        char *start;
        char *path = alloca(strlen(fname) + 1);
        strcpy(path, fname);
        ptr = strchr(path, ':');
        BAIL_IF_MACRO(!ptr, ERR_FILE_NOT_FOUND, err); /* just in case */
        ptr = strchr(ptr + 1, ':');
        BAIL_IF_MACRO(!ptr, ERR_FILE_NOT_FOUND, err); /* just in case */
        *ptr = '\0';
        err = fnameToFSSpecNoAlias(path, spec); /* get first dir. */
        BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, err);
        start = ptr;
        ptr = strchr(start + 1, ':');

        do
        {
            CInfoPBRec infoPB;
            memset(&infoPB, '\0', sizeof (CInfoPBRec));
            infoPB.dirInfo.ioNamePtr = spec->name;
            infoPB.dirInfo.ioVRefNum = spec->vRefNum;
            infoPB.dirInfo.ioDrDirID = spec->parID;
            infoPB.dirInfo.ioFDirIndex = 0;
            err = PBGetCatInfoSync(&infoPB);
            if (err != noErr)  /* not an alias, really a bogus path. */
                return(fnameToFSSpecNoAlias(fname, spec)); /* reset */

            if ((infoPB.dirInfo.ioFlAttrib & kioFlAttribDirMask) != 0)
                spec->parID = infoPB.dirInfo.ioDrDirID;

            if (ptr != NULL)
                *ptr = '\0';
            *start = strlen(start + 1);  /* make it a pstring. */
            err = FSMakeFSSpec(spec->vRefNum, spec->parID,
                               (const unsigned char *) start, spec);
            if (err != noErr)  /* not an alias, really a bogus path. */
                return(fnameToFSSpecNoAlias(fname, spec)); /* reset */

            err = ResolveAliasFileWithMountFlags(spec, 1, &folder, &alias, 0);
            if (err != noErr)  /* not an alias, really a bogus path. */
                return(fnameToFSSpecNoAlias(fname, spec)); /* reset */
            start = ptr;
            if (ptr != NULL)
                ptr = strchr(start + 1, ':');                
        } while (start != NULL);
    } /* if */

    else /* there's something there; make sure final file is not an alias. */
    {
        BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, err);
        err = ResolveAliasFileWithMountFlags(spec, 1, &folder, &alias, 0);
        BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, err);
    } /* else */

    return(noErr);  /* w00t. */
} /* fnameToFSSpec */


int __PHYSFS_platformExists(const char *fname)
{
    FSSpec spec;
    return(fnameToFSSpec(fname, &spec) == noErr);
} /* __PHYSFS_platformExists */


int __PHYSFS_platformIsSymLink(const char *fname)
{
    OSErr err;
    FSSpec spec;
    Boolean a = 0;
    Boolean f = 0;
    CInfoPBRec infoPB;
    char *ptr;
    char *dir = alloca(strlen(fname) + 1);
    BAIL_IF_MACRO(dir == NULL, ERR_OUT_OF_MEMORY, 0);
    strcpy(dir, fname);
    ptr = strrchr(dir, ':');
    if (ptr == NULL)  /* just a volume name? Can't be a symlink. */
        return(0);

    /* resolve aliases up to the actual file... */
    *ptr = '\0';
    BAIL_IF_MACRO(fnameToFSSpec(dir, &spec) != noErr, ERR_OS_ERROR, 0);

    *ptr = strlen(ptr + 1);  /* ptr is now a pascal string. Yikes! */
    memset(&infoPB, '\0', sizeof (CInfoPBRec));
    infoPB.dirInfo.ioNamePtr = spec.name;
    infoPB.dirInfo.ioVRefNum = spec.vRefNum;
    infoPB.dirInfo.ioDrDirID = spec.parID;
    infoPB.dirInfo.ioFDirIndex = 0;
    BAIL_IF_MACRO(PBGetCatInfoSync(&infoPB) != noErr, ERR_OS_ERROR, 0);

    err = FSMakeFSSpec(spec.vRefNum, infoPB.dirInfo.ioDrDirID,
                       (const unsigned char *) ptr, &spec);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    BAIL_IF_MACRO(IsAliasFile(&spec, &a, &f) != noErr, ERR_OS_ERROR, 0);
    return(a);
} /* __PHYSFS_platformIsSymlink */


int __PHYSFS_platformIsDirectory(const char *fname)
{
    FSSpec spec;
    CInfoPBRec infoPB;
    OSErr err;

    BAIL_IF_MACRO(fnameToFSSpec(fname, &spec) != noErr, ERR_OS_ERROR, 0);
    memset(&infoPB, '\0', sizeof (CInfoPBRec));
    infoPB.dirInfo.ioNamePtr = spec.name;     /* put name in here.       */
    infoPB.dirInfo.ioVRefNum = spec.vRefNum;  /* ID of file's volume.    */ 
    infoPB.dirInfo.ioDrDirID = spec.parID;    /* ID of bin's dir.        */
    infoPB.dirInfo.ioFDirIndex = 0;           /* file (not parent) info. */
    err = PBGetCatInfoSync(&infoPB);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    return((infoPB.dirInfo.ioFlAttrib & kioFlAttribDirMask) != 0);
} /* __PHYSFS_platformIsDirectory */


char *__PHYSFS_platformCvtToDependent(const char *prepend,
                                      const char *dirName,
                                      const char *append)
{
    int len = ((prepend) ? strlen(prepend) : 0) +
              ((append) ? strlen(append) : 0) +
              strlen(dirName) + 1;
    const char *src;
    char *dst;
    char *retval = malloc(len);
    BAIL_IF_MACRO(retval == NULL, ERR_OUT_OF_MEMORY, NULL);

    if (prepend != NULL)
    {
        strcpy(retval, prepend);
        dst = retval + strlen(retval);
    } /* if */
    else
    {
        *retval = '\0';
        dst = retval;
    } /* else */

    for (src = dirName; *src; src++, dst++)
        *dst = ((*src == '/') ? ':' : *src);

    *dst = '\0';
    return(retval);
} /* __PHYSFS_platformCvtToDependent */


void __PHYSFS_platformTimeslice(void)
{
    SystemTask();
} /* __PHYSFS_platformTimeslice */


LinkedStringList *__PHYSFS_platformEnumerateFiles(const char *dirname,
                                                  int omitSymLinks)
{
    LinkedStringList *retval = NULL;
    LinkedStringList *l = NULL;
    LinkedStringList *prev = NULL;
    UInt16 i;
    UInt16 max;
    FSSpec spec;
    CInfoPBRec infoPB;
    Str255 str255;
    long dirID;

    BAIL_IF_MACRO(fnameToFSSpec(dirname, &spec) != noErr, ERR_OS_ERROR, 0);

    /* get the dir ID of what we want to enumerate... */
    memset(&infoPB, '\0', sizeof (CInfoPBRec));
    infoPB.dirInfo.ioNamePtr = spec.name;     /* name of dir to enum.    */
    infoPB.dirInfo.ioVRefNum = spec.vRefNum;  /* ID of file's volume.    */ 
    infoPB.dirInfo.ioDrDirID = spec.parID;    /* ID of dir.              */
    infoPB.dirInfo.ioFDirIndex = 0;           /* file (not parent) info. */
    BAIL_IF_MACRO(PBGetCatInfoSync(&infoPB) != noErr, ERR_OS_ERROR, NULL);

    if ((infoPB.dirInfo.ioFlAttrib & kioFlAttribDirMask) == 0)
        BAIL_MACRO(ERR_NOT_A_DIR, NULL);

    dirID = infoPB.dirInfo.ioDrDirID;
    max = infoPB.dirInfo.ioDrNmFls;

    for (i = 1; i <= max; i++)
    {
        FSSpec aliasspec;
        Boolean alias = 0;
        Boolean folder = 0;

        memset(&infoPB, '\0', sizeof (CInfoPBRec));
        str255[0] = 0;
        infoPB.dirInfo.ioNamePtr = str255;        /* store name in here.  */
        infoPB.dirInfo.ioVRefNum = spec.vRefNum;  /* ID of dir's volume. */ 
        infoPB.dirInfo.ioDrDirID = dirID;         /* ID of dir.           */
        infoPB.dirInfo.ioFDirIndex = i;         /* next file's info.    */
        if (PBGetCatInfoSync(&infoPB) != noErr)
            continue;  /* skip this file. Oh well. */

        if (FSMakeFSSpec(spec.vRefNum, dirID, str255, &aliasspec) != noErr)
            continue;  /* skip it. */

        if (IsAliasFile(&aliasspec, &alias, &folder) != noErr)
            continue;  /* skip it. */

        if ((alias) && (omitSymLinks))
            continue;

        /* still here? Add it to the list. */

        l = (LinkedStringList *) malloc(sizeof (LinkedStringList));
        if (l == NULL)
            break;

        l->str = (char *) malloc(str255[0] + 1);
        if (l->str == NULL)
        {
            free(l);
            break;
        } /* if */

        memcpy(l->str, &str255[1], str255[0]);
        l->str[str255[0]] = '\0';

        if (retval == NULL)
            retval = l;
        else
            prev->next = l;

        prev = l;
        l->next = NULL;
    } /* for */

    return(retval);
} /* __PHYSFS_platformEnumerateFiles */


char *__PHYSFS_platformCurrentDir(void)
{
    /*
     * I don't think MacOS has a concept of "current directory", beyond
     *  what is grafted on by a given standard C library implementation,
     *  so just return the base dir.
     * We don't use this for anything crucial at the moment anyhow.
     */
    return(__PHYSFS_platformCalcBaseDir(NULL));
} /* __PHYSFS_platformCurrentDir */


char *__PHYSFS_platformRealPath(const char *path)
{
    /*
     * fnameToFSSpec() will resolve any symlinks to get to the real
     *  file's FSSpec, which, when converted, will contain the real
     *  direct path to a given file. convFSSpecToPath() mallocs a
     *  return value buffer.
     */

    FSSpec spec;
    BAIL_IF_MACRO(fnameToFSSpec(path, &spec) != noErr, ERR_OS_ERROR, NULL);
    return(convFSSpecToPath(&spec, 1));
} /* __PHYSFS_platformRealPath */


int __PHYSFS_platformMkDir(const char *path)
{
    SInt32 val = 0;
    FSSpec spec;
    OSErr err = fnameToFSSpec(path, &spec);

    BAIL_IF_MACRO(err == noErr, ERR_FILE_EXISTS, 0);
    BAIL_IF_MACRO(err != fnfErr, ERR_OS_ERROR, 0);

    err = DirCreate(spec.vRefNum, spec.parID, spec.name, &val);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    return(1);
} /* __PHYSFS_platformMkDir */


static SInt16 *macDoOpen(const char *fname, SInt8 perm, int createIfMissing)
{
    int created = 0;
    SInt16 *retval = NULL;
    FSSpec spec;
    OSErr err = fnameToFSSpec(fname, &spec);
    BAIL_IF_MACRO((err != noErr) && (err != fnfErr), ERR_OS_ERROR, NULL);
    if (err == fnfErr)
    {
        BAIL_IF_MACRO(!createIfMissing, ERR_FILE_NOT_FOUND, NULL);
        err = HCreate(spec.vRefNum, spec.parID, spec.name,
                      procInfo.processSignature, 'BINA');
        BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, NULL);
        created = 1;
    } /* if */

    retval = (SInt16 *) malloc(sizeof (SInt16));
    if (retval == NULL)
    {
        if (created)
            HDelete(spec.vRefNum, spec.parID, spec.name);
        BAIL_MACRO(ERR_OUT_OF_MEMORY, NULL);
    } /* if */

    if (HOpenDF(spec.vRefNum, spec.parID, spec.name, perm, retval) != noErr)
    {
        free(retval);
        if (created)
            HDelete(spec.vRefNum, spec.parID, spec.name);
        BAIL_MACRO(ERR_OS_ERROR, NULL);
    } /* if */

    return(retval);
} /* macDoOpen */


void *__PHYSFS_platformOpenRead(const char *filename)
{
    SInt16 *retval = macDoOpen(filename, fsRdPerm, 0);
    if (retval != NULL)   /* got a file; seek to start. */
    {
        if (SetFPos(*retval, fsFromStart, 0) != noErr)
        {
            FSClose(*retval);
            BAIL_MACRO(ERR_OS_ERROR, NULL);
        } /* if */
    } /* if */

    return((void *) retval);
} /* __PHYSFS_platformOpenRead */


void *__PHYSFS_platformOpenWrite(const char *filename)
{
    SInt16 *retval = macDoOpen(filename, fsRdWrPerm, 1);
    if (retval != NULL)   /* got a file; truncate it. */
    {
        if ((SetEOF(*retval, 0) != noErr) ||
            (SetFPos(*retval, fsFromStart, 0) != noErr))
        {
            FSClose(*retval);
            BAIL_MACRO(ERR_OS_ERROR, NULL);
        } /* if */
    } /* if */

    return((void *) retval);
} /* __PHYSFS_platformOpenWrite */


void *__PHYSFS_platformOpenAppend(const char *filename)
{
    SInt16 *retval = macDoOpen(filename, fsRdWrPerm, 1);
    if (retval != NULL)   /* got a file; seek to end. */
    {
        if (SetFPos(*retval, fsFromLEOF, 0) != noErr)
        {
            FSClose(*retval);
            BAIL_MACRO(ERR_OS_ERROR, NULL);
        } /* if */
    } /* if */

    return(retval);
} /* __PHYSFS_platformOpenAppend */


PHYSFS_sint64 __PHYSFS_platformRead(void *opaque, void *buffer,
                                    PHYSFS_uint32 size, PHYSFS_uint32 count)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt32 br;
    PHYSFS_uint32 i;

    for (i = 0; i < count; i++)
    {
        br = size;
        BAIL_IF_MACRO(FSRead(ref, &br, buffer) != noErr, ERR_OS_ERROR, i);
        BAIL_IF_MACRO(br != size, ERR_OS_ERROR, i);
        buffer = ((PHYSFS_uint8 *) buffer) + size;
    } /* for */

    return(count);
} /* __PHYSFS_platformRead */


PHYSFS_sint64 __PHYSFS_platformWrite(void *opaque, const void *buffer,
                                     PHYSFS_uint32 size, PHYSFS_uint32 count)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt32 bw;
    PHYSFS_uint32 i;

    for (i = 0; i < count; i++)
    {
        bw = size;
        BAIL_IF_MACRO(FSWrite(ref, &bw, buffer) != noErr, ERR_OS_ERROR, i);
        BAIL_IF_MACRO(bw != size, ERR_OS_ERROR, i);
        buffer = ((PHYSFS_uint8 *) buffer) + size;
    } /* for */

    return(count);
} /* __PHYSFS_platformWrite */


int __PHYSFS_platformSeek(void *opaque, PHYSFS_uint64 pos)
{
    SInt16 ref = *((SInt16 *) opaque);
    OSErr err = SetFPos(ref, fsFromStart, (SInt32) pos);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    return(1);
} /* __PHYSFS_platformSeek */


PHYSFS_sint64 __PHYSFS_platformTell(void *opaque)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt32 curPos;
    BAIL_IF_MACRO(GetFPos(ref, &curPos) != noErr, ERR_OS_ERROR, -1);
    return((PHYSFS_sint64) curPos);
} /* __PHYSFS_platformTell */


PHYSFS_sint64 __PHYSFS_platformFileLength(void *opaque)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt32 eofPos;
    BAIL_IF_MACRO(GetEOF(ref, &eofPos) != noErr, ERR_OS_ERROR, -1);
    return((PHYSFS_sint64) eofPos);
} /* __PHYSFS_platformFileLength */


int __PHYSFS_platformEOF(void *opaque)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt32 eofPos, curPos;
    BAIL_IF_MACRO(GetEOF(ref, &eofPos) != noErr, ERR_OS_ERROR, 1);
    BAIL_IF_MACRO(GetFPos(ref, &curPos) != noErr, ERR_OS_ERROR, 1);
    return(curPos >= eofPos);
} /* __PHYSFS_platformEOF */


int __PHYSFS_platformFlush(void *opaque)
{
    SInt16 ref = *((SInt16 *) opaque);
    ParamBlockRec pb;
    memset(&pb, '\0', sizeof (ParamBlockRec));
    pb.ioParam.ioRefNum = ref;
    BAIL_IF_MACRO(PBFlushFileSync(&pb) != noErr, ERR_OS_ERROR, 0);
    return(1);
} /* __PHYSFS_platformFlush */


int __PHYSFS_platformClose(void *opaque)
{
    SInt16 ref = *((SInt16 *) opaque);
    SInt16 vRefNum;
    HParamBlockRec hpbr;
    Str63 volName;

    BAIL_IF_MACRO(GetVRefNum (ref, &vRefNum) != noErr, ERR_OS_ERROR, 0);

    memset(&hpbr, '\0', sizeof (HParamBlockRec));
    hpbr.volumeParam.ioNamePtr = volName;
    hpbr.volumeParam.ioVRefNum = vRefNum;
    hpbr.volumeParam.ioVolIndex = 0;
    BAIL_IF_MACRO(PBHGetVInfoSync(&hpbr) != noErr, ERR_OS_ERROR, 0);

    BAIL_IF_MACRO(FSClose(ref) != noErr, ERR_OS_ERROR, 0);
    free(opaque);

    FlushVol(volName, vRefNum);
    return(1);
} /* __PHYSFS_platformClose */


int __PHYSFS_platformDelete(const char *path)
{
    FSSpec spec;
    OSErr err;
    BAIL_IF_MACRO(fnameToFSSpec(path, &spec) != noErr, ERR_OS_ERROR, 0);
    err = HDelete(spec.vRefNum, spec.parID, spec.name);
    BAIL_IF_MACRO(err != noErr, ERR_OS_ERROR, 0);
    return(1);
} /* __PHYSFS_platformDelete */


void *__PHYSFS_platformCreateMutex(void)
{
    return((void *) 0x0001);  /* no mutexes on MacOS Classic. */
} /* __PHYSFS_platformCreateMutex */


void __PHYSFS_platformDestroyMutex(void *mutex)
{
    /* no mutexes on MacOS Classic. */
} /* __PHYSFS_platformDestroyMutex */


int __PHYSFS_platformGrabMutex(void *mutex)
{
    return(1);  /* no mutexes on MacOS Classic. */
} /* __PHYSFS_platformGrabMutex */


void __PHYSFS_platformReleaseMutex(void *mutex)
{
    /* no mutexes on MacOS Classic. */
} /* __PHYSFS_platformReleaseMutex */


PHYSFS_sint64 __PHYSFS_platformGetLastModTime(const char *fname)
{
    FSSpec spec;
    CInfoPBRec infoPB;
    UInt32 modDate;

    BAIL_IF_MACRO(fnameToFSSpec(fname, &spec) != noErr, ERR_OS_ERROR, -1);

    memset(&infoPB, '\0', sizeof (CInfoPBRec));
    infoPB.dirInfo.ioNamePtr = spec.name;
    infoPB.dirInfo.ioVRefNum = spec.vRefNum;
    infoPB.dirInfo.ioDrDirID = spec.parID;
    infoPB.dirInfo.ioFDirIndex = 0;
    BAIL_IF_MACRO(PBGetCatInfoSync(&infoPB) != noErr, ERR_OS_ERROR, -1);

    modDate = ((infoPB.dirInfo.ioFlAttrib & kioFlAttribDirMask) != 0) ?
                   infoPB.dirInfo.ioDrMdDat : infoPB.hFileInfo.ioFlMdDat;

    /* epoch is different on MacOS. They use Jan 1, 1904, apparently. */
    /*  subtract seconds between those epochs, counting leap years.   */
    modDate -= 2082844800;

    return((PHYSFS_sint64) modDate);
} /* __PHYSFS_platformGetLastModTime */

/* end of macclassic.c ... */

