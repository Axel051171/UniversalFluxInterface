/* Host-side stand-in for the FatFs API (ChaN ff.h): types, result codes and the
 * prototypes the firmware uses.  Behaviour comes from the test or fuzz harness that
 * implements f_*; nothing here touches a file system. */
#ifndef FF_STUB_H
#define FF_STUB_H
#include <stdint.h>

typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef uint64_t QWORD;
typedef DWORD FSIZE_t;
typedef DWORD LBA_t;
typedef char TCHAR;

typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH, FR_INVALID_NAME,
    FR_DENIED, FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED,
    FR_NO_FILESYSTEM, FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER
} FRESULT;

typedef struct {
    BYTE fs_type, pdrv, n_fats, wflag, fsi_flag;
    WORD id, n_rootdir, csize;
    DWORD n_fatent, fsize, volbase, fatbase, dirbase, database, winsect, free_clst, last_clst;
    BYTE win[512];
} FATFS;

typedef struct {
    FATFS* fs;
    WORD id;
    BYTE attr, stat;
    DWORD sclust;
    FSIZE_t objsize;
} FFOBJID;

typedef struct {
    FFOBJID obj;
    BYTE flag, err;
    FSIZE_t fptr;
    DWORD clust, sect, dir_sect;
    BYTE* dir_ptr;
    BYTE buf[512];
} FIL;

typedef struct {
    FFOBJID obj;
    DWORD dptr, clust, sect;
    BYTE* dir;
    BYTE fn[12];
} DIR;

typedef struct {
    FSIZE_t fsize;
    WORD fdate, ftime;
    BYTE fattrib;
    TCHAR altname[13];
    TCHAR fname[256];
} FILINFO;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS   0x10
#define FA_OPEN_APPEND   0x30

#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10
#define AM_ARC 0x20

#define f_size(fp) ((fp)->obj.objsize)
#define f_tell(fp) ((fp)->fptr)
#define f_eof(fp)  ((int)((fp)->fptr == (fp)->obj.objsize))

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, FSIZE_t ofs);
FRESULT f_truncate(FIL* fp);
FRESULT f_sync(FIL* fp);
FRESULT f_opendir(DIR* dp, const TCHAR* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_mkdir(const TCHAR* path);
FRESULT f_unlink(const TCHAR* path);
FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new);
FRESULT f_stat(const TCHAR* path, FILINFO* fno);
FRESULT f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs);
FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt);
FRESULT f_mkfs(const TCHAR* path, const void* opt, void* work, UINT len);

#endif
