/*
 * yaffs2 adapter for the liteos_m OH-VFS.
 * (YAFFS2_BINARY_LIB_PORTING_PLAN.md P3; based on the morpheus board-proven
 * adapter, re-targeted from the source-route adapt layer to the CLOSED fbb
 * prebuilt libyaffs2.a.)
 *
 * Registered as fs type "yaffs". Mount data is a fbb-contract mtd_partition
 * (third_party/yaffs2/include/mtd_partition.h) assembled by
 * fmc/yaffs/nand_yaffs_bridge.c; the mount flow mirrors the closed fbb
 * vfs_yaffs_bind() exactly: yaffs_start(mountpoint, partition) then
 * yaffs_mount(mountpoint). The closed archive takes the mtd device from
 * its own exported global `yaffs_mtd` (assigned by the bridge after the
 * NAND probe) and computes the device geometry from it.
 *
 * Path semantics: liteos_m's non-recursive VfsMpFind hands the FULL path
 * to the fs ops, and yaffs matches its own devices by longest prefix against
 * dev->param.name — which the closed yaffs_start() sets to the VFS mount
 * point. So the full path is passed through unstripped.
 *
 * Handles: file->fData carries the yaffs fd as (fd + 1): yaffs handle 0 is
 * VALID (the first open on the system) and fData == NULL is the "not open"
 * sentinel — storing a raw 0 handle made every later op fail with EBADF
 * (morpheus board round 4). Errors: yaffsfs_GetLastError().
 *
 * Registration happens from the bridge (YaffsVfsRegister) after OsVfsInit,
 * so vfs_init.c stays untouched.
 */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <dirent.h>

#include "vfs_mount.h"
#include "vfs_files.h"
#include "los_fs.h"
#include "los_debug.h"
#include "securec.h"

#include "yaffsfs.h"          /* direct API (closed archive export) */
#include "yaffs_guts.h"       /* struct yaffs_dev full type (statfs geometry) */
#include "yaffs_nandcfg.h"    /* yaffs_start/yaffs_end (fbb contract) */

/*
 * Layout sentinels against the CLOSED archive. yaffs_nand_install_drv()
 * allocates sizeof(struct yaffs_dev) = 3740 bytes via yaffsfs_malloc; if our
 * compile of the same fbb header disagrees (enum size / alignment drift),
 * the adapter would corrupt the device the archive built. FAIL THE BUILD.
 * The dirent sentinel pins the d_name offset the archive's readdir writes
 * (board round 2: our musl off_t is 8B vs the fbb build's 4B — see the ABI
 * note in third_party/yaffs2/include/yaffs2/direct/yaffsfs.h).
 */
_Static_assert(sizeof(struct yaffs_dev) == 3740,
               "struct yaffs_dev layout drifted from the fbb prebuilt archive");
_Static_assert(offsetof(struct yaffs_dirent, d_name) == 11,
               "struct yaffs_dirent.d_name drifted from the fbb prebuilt archive");

#define YAFFS_DEFAULT_MODE 0666

static int YaffsErrno(void)
{
    int err = yaffsfs_GetLastError();

    return (err > 0) ? err : EIO;
}

static int YaffsErrFail(void)
{
    errno = YaffsErrno();
    return (int)LOS_NOK;
}

/*
 * fd slot encoding: file->fData carries the yaffs handle as (fd + 1) so the
 * valid handle 0 never collides with the NULL "not open" sentinel.
 */
static int YaffsFdFromData(const struct File *file)
{
    return (int)(INTPTR)file->fData - 1;
}

static int YaffsCheckFileOpen(struct File *file)
{
    if ((file == NULL) || (file->fData == NULL)) {
        errno = EBADF;
        return (int)LOS_NOK;
    }
    return (int)LOS_OK;
}

/* ---- mount / umount -------------------------------------------------------- */

static int YaffsMount(struct MountPoint *mp, unsigned long mountflags,
                      const void *data)
{
    const mtd_partition *cfg = (const mtd_partition *)data;

    if ((mp == NULL) || (mp->mPath == NULL) || (cfg == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if ((mountflags & MS_REMOUNT) != 0UL) {
        errno = ENOSYS;
        return (int)LOS_NOK;
    }

    /*
     * Closed yaffs_start(name, partition): installs the driver hooks and
     * computes the geometry from the global yaffs_mtd (set by the bridge).
     * RETURN CONTRACT (fbb disassembly, yaffs_nand_install_drv tail +
     * vfs_yaffs_bind): success returns param.start_block — NONZERO, because
     * the fbb layout reserves block 0 so start_block >= 1 always; failure
     * (pool malloc) returns 0. Board round 1 failed exactly here: the
     * morpheus source-route adapter treated 0 as success and rejected the
     * valid start_block return with ENODEV.
     */
    if (yaffs_start(mp->mPath, cfg) == 0) {
        errno = ENODEV;
        return (int)LOS_NOK;
    }
    if (yaffs_mount(mp->mPath) != 0) {
        int err = YaffsErrno();

        PRINTK("yaffs: mount %s fail errno=%d\n", mp->mPath, err);
        yaffs_end(mp->mPath);
        errno = err;
        return (int)LOS_NOK;
    }
    PRINTK("yaffs: mount %s ok (blocks %u)\n", mp->mPath,
           cfg->end_block - cfg->start_block + 1U);
    return (int)LOS_OK;
}

static int YaffsUmount(struct MountPoint *mp)
{
    if (mp == NULL) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }

    if (yaffs_unmount(mp->mPath) != 0) {
        return YaffsErrFail();
    }
    yaffs_end(mp->mPath);
    mp->mData = NULL;
    return (int)LOS_OK;
}

static int YaffsUmount2(struct MountPoint *mp, int flag)
{
    (void)flag; /* MNT_FORCE not supported */
    return YaffsUmount(mp);
}

/* ---- file ops --------------------------------------------------------------- */

static int YaffsOpen(struct File *file, const char *pathName, int openFlag)
{
    int fd;

    if ((file == NULL) || (pathName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }

    fd = yaffs_open(pathName, openFlag, YAFFS_DEFAULT_MODE);
    if (fd < 0) {
        return YaffsErrFail();
    }
    /* fd + 1: keep yaffs handle 0 distinct from the NULL sentinel */
    file->fData = (void *)(INTPTR)(INT32)(fd + 1);
    return (int)LOS_OK;
}

static int YaffsClose(struct File *file)
{
    int fd;

    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (int)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    if (yaffs_close(fd) != 0) {
        return YaffsErrFail();
    }
    file->fData = NULL;
    return (int)LOS_OK;
}

static ssize_t YaffsRead(struct File *file, char *buf, size_t len)
{
    int fd;
    int ret;

    if (buf == NULL) {
        errno = EFAULT;
        return (ssize_t)LOS_NOK;
    }
    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (ssize_t)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    ret = yaffs_read(fd, buf, (unsigned int)len);
    if (ret < 0) {
        return (ssize_t)YaffsErrFail();
    }
    return (ssize_t)ret;
}

static ssize_t YaffsWrite(struct File *file, const char *buf, size_t len)
{
    int fd;
    int ret;

    if (buf == NULL) {
        errno = EFAULT;
        return (ssize_t)LOS_NOK;
    }
    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (ssize_t)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    ret = yaffs_write(fd, buf, (unsigned int)len);
    if (ret < 0) {
        return (ssize_t)YaffsErrFail();
    }
    return (ssize_t)ret;
}

static off_t YaffsLseek(struct File *file, off_t offset, int whence)
{
    int fd;
    Y_LOFF_T ret;

    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (off_t)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    ret = yaffs_lseek(fd, (Y_LOFF_T)offset, whence);
    if (ret < 0) {
        return (off_t)YaffsErrFail();
    }
    return (off_t)ret;
}

static int YaffsSync(struct File *file)
{
    int fd;

    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (int)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    if (yaffs_flush(fd) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

static int YaffsTruncate(struct File *file, off_t len)
{
    int fd;

    if (YaffsCheckFileOpen(file) != (int)LOS_OK) {
        return (int)LOS_NOK;
    }
    fd = YaffsFdFromData(file);
    if (yaffs_ftruncate(fd, (Y_LOFF_T)len) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

static void YaffsStatToStat(struct stat *buf, const struct yaffs_stat *ys)
{
    /* yaffsfs.c fills st_mode with permission bits OR the S_IFDIR/S_IFREG
     * type bits, so it maps 1:1 onto struct stat. */
    (void)memset_s(buf, sizeof(*buf), 0, sizeof(*buf));
    buf->st_size = (off_t)ys->st_size;
    buf->st_mode = (mode_t)ys->st_mode;
    buf->st_nlink = (short)ys->st_nlink;
}

static int YaffsStat(struct MountPoint *mp, const char *path, struct stat *buf)
{
    struct yaffs_stat ys;

    if ((mp == NULL) || (path == NULL) || (buf == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    (void)memset_s(&ys, sizeof(ys), 0, sizeof(ys));
    if (yaffs_stat(path, &ys) != 0) {
        return YaffsErrFail();
    }
    YaffsStatToStat(buf, &ys);
    return (int)LOS_OK;
}

static int YaffsUnlink(struct MountPoint *mp, const char *fileName)
{
    if ((mp == NULL) || (fileName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (yaffs_unlink(fileName) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

static int YaffsRename(struct MountPoint *mp, const char *oldName,
                       const char *newName)
{
    if ((mp == NULL) || (oldName == NULL) || (newName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (yaffs_rename(oldName, newName) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

static int YaffsMkdir(struct MountPoint *mp, const char *dirName)
{
    if ((mp == NULL) || (dirName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (yaffs_mkdir(dirName, YAFFS_DEFAULT_MODE) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

static int YaffsRmdir(struct MountPoint *mp, const char *dirName)
{
    if ((mp == NULL) || (dirName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (yaffs_rmdir(dirName) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

/* ---- dir ops ----------------------------------------------------------------- */

static int YaffsOpendir(struct Dir *dir, const char *dirName)
{
    yaffs_DIR *d;

    if ((dir == NULL) || (dirName == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    d = yaffs_opendir(dirName);
    if (d == NULL) {
        return YaffsErrFail();
    }
    dir->dData = (void *)d;
    dir->dOffset = 0;
    return (int)LOS_OK;
}

static int YaffsReaddir(struct Dir *dir, struct dirent *dent)
{
    struct yaffs_dirent *de;
    size_t nameLen;

    if ((dir == NULL) || (dent == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (dir->dData == NULL) {
        errno = EBADF;
        return (int)LOS_NOK;
    }
    de = yaffs_readdir((yaffs_DIR *)dir->dData);
    if (de == NULL) {
        /* end of directory (or error, indistinguishable in yaffs direct) */
        return (int)LOS_NOK;
    }
    nameLen = strlen(de->d_name);
    if (nameLen >= sizeof(dent->d_name)) {
        nameLen = sizeof(dent->d_name) - 1U;
    }
    (void)memcpy_s(dent->d_name, sizeof(dent->d_name), de->d_name, nameLen);
    dent->d_name[nameLen] = '\0';
    dent->d_type = (de->d_type == (YUCHAR)DT_DIR) ? DT_DIR : DT_REG;
    dent->d_reclen = (unsigned short)nameLen;
    return (int)LOS_OK;
}

static int YaffsClosedir(struct Dir *dir)
{
    if (dir == NULL) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    if (dir->dData == NULL) {
        errno = EBADF;
        return (int)LOS_NOK;
    }
    if (yaffs_closedir((yaffs_DIR *)dir->dData) != 0) {
        return YaffsErrFail();
    }
    dir->dData = NULL;
    return (int)LOS_OK;
}

/* ---- mount ops: statfs / format ----------------------------------------------- */

static int YaffsStatfs(const char *path, struct statfs *buf)
{
    struct yaffs_dev *dev;
    Y_LOFF_T total;
    Y_LOFF_T freeSpace;
    u32 chunk;

    if ((path == NULL) || (buf == NULL)) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    dev = (struct yaffs_dev *)yaffs_getdev(path);
    if (dev == NULL) {
        errno = ENOENT;
        return (int)LOS_NOK;
    }
    total = yaffs_totalspace(path);
    freeSpace = yaffs_freespace(path);
    if ((total < 0) || (freeSpace < 0)) {
        return YaffsErrFail();
    }
    chunk = dev->param.total_bytes_per_chunk;
    if (chunk == 0U) {
        chunk = 1U;
    }
    (void)memset_s(buf, sizeof(*buf), 0, sizeof(*buf));
    /* yaffs counts capacity in chunks (2048B), not erase blocks — smoke-test
     * assertions must compare capacity, not block counts (morpheus round 4) */
    buf->f_bsize = (long)chunk;
    buf->f_blocks = (long)(total / (Y_LOFF_T)chunk);
    buf->f_bfree = (long)(freeSpace / (Y_LOFF_T)chunk);
    buf->f_bavail = buf->f_bfree;
    buf->f_namelen = 255;
    return (int)LOS_OK;
}

static int YaffsFormat(const char *partName, void *privData)
{
    (void)privData;
    if (partName == NULL) {
        errno = EFAULT;
        return (int)LOS_NOK;
    }
    /* volume must be registered (yaffs_start done) and not mounted */
    if (yaffs_format(partName, 0, 0, 0) != 0) {
        return YaffsErrFail();
    }
    return (int)LOS_OK;
}

/* ---- registration --------------------------------------------------------------- */

static struct MountOps g_yaffsMnt = {
    .mount = YaffsMount,
    .umount = YaffsUmount,
    .umount2 = YaffsUmount2,
    .statfs = YaffsStatfs,
};

static struct FileOps g_yaffsFops = {
    .open = YaffsOpen,
    .close = YaffsClose,
    .read = YaffsRead,
    .write = YaffsWrite,
    .lseek = YaffsLseek,
    .stat = YaffsStat,
    .truncate = YaffsTruncate,
    .unlink = YaffsUnlink,
    .rename = YaffsRename,
    .ioctl = NULL, /* not supported */
    .sync = YaffsSync,
    .opendir = YaffsOpendir,
    .readdir = YaffsReaddir,
    .closedir = YaffsClosedir,
    .mkdir = YaffsMkdir,
    .rmdir = YaffsRmdir,
};

static struct FsManagement g_yaffsMgt = {
    .fdisk = NULL,
    .format = YaffsFormat,
};

void YaffsVfsRegister(void)
{
    (void)OsFsRegister("yaffs", &g_yaffsMnt, &g_yaffsFops, &g_yaffsMgt);
}
