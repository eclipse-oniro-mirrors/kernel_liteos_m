/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided by the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 本文件通过对外接口 LOS_FsRegister 注册测试专用文件系统(操作表为用例本地实现),
 * 驱动 kernel/liteos_m/fs/vfs 各文件的挂载管理与错误分支:
 *   - vfs_maps.c  : LOS_FsRegister/OsFsRegister(重复注册/空操作表)
 *   - vfs_mount.c : mount(source 非空)/umount(引用计数/失败)/umount2(MNT_FORCE)/VfsRemount
 *   - vfs_fs.c    : fops 操作表为 NULL 的 ENOTSUP 分支 / ENFILE / pread-pwrite 内部 lseek 失败
 * 说明:littlefs 上游测试套(third_party/littlefs/tests/test_bd.toml)使用可注入错误的
 * 块设备(lfs_emubd)驱动错误分支;本内核场景下等价的错误注入点是 VFS 操作表,
 * 故以自定义文件系统实现同类"可控失败"注入。
 */

#include "posix_fs_test.h"
#include "los_fs.h"
#include "vfs_operations.h"
#include <sys/statfs.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

/* newlib 移植层 dirent.h 未定义目录项类型宏, 按 musl 值兜底 */
#ifndef DT_REG
#define DT_REG 8
#endif

#define VFS_TEST_BUF_LEN    1024
#define VFS_TEST_OPEN_MAX   260     /* NR_OPEN_DEFAULT(253) + 余量 */

/* 测试专用文件系统的可控失败开关与状态 */
static volatile int g_vfsTsUmountFail = 0;    /* umount 操作返回失败 */
static volatile int g_vfsTsUmount2Fail = 0;   /* umount2 操作返回失败 */
static volatile int g_vfsTsClosedirFail = 0;  /* closedir 操作返回失败 */
static volatile int g_vfsTsReaddirEof = 0;    /* readdir 第二次返回 EOF */
static volatile off_t g_vfsTsLseekMagic = -1; /* lseek(SEEK_SET, magic) 返回失败 */
static volatile int g_vfsTsLseekCurFail = 0;  /* 置 1 时 lseek(SEEK_CUR, 0) 返回失败 */
static volatile int g_vfsTsMountRo = 0;       /* mount 后将 mp 置为只读 */
static char g_vfsTsBuf[VFS_TEST_BUF_LEN];     /* 共享文件内容缓冲 */
static int g_vfsTsMountCnt = 0;

/* ---- 测试文件系统 "testfs" 挂载操作 ---- */
static int VfsTsMount(struct MountPoint *mp, unsigned long mountflags, const void *data)
{
    (void)mountflags;
    (void)data;
    g_vfsTsMountCnt++;
    if (g_vfsTsMountRo) {
        mp->mWriteEnable = 0; /* 模拟只读挂载, 覆盖 mWriteEnable=FALSE 分支 */
    }
    return 0;
}

static int VfsTsUmount(struct MountPoint *mp)
{
    (void)mp;
    return g_vfsTsUmountFail ? -1 : 0;
}

static int VfsTsUmount2(struct MountPoint *mp, int flag)
{
    (void)mp;
    (void)flag;
    return g_vfsTsUmount2Fail ? -1 : 0;
}

static int VfsTsStatfs(const char *path, struct statfs *buf)
{
    (void)path;
    buf->f_type = 0x54454653; /* 'TEST' 标识 */
    buf->f_bsize = VFS_TEST_BUF_LEN;
    return 0;
}

/* ---- "testfs" 文件操作 ---- */
static int VfsTsOpen(struct File *file, const char *path, int flags)
{
    (void)path;
    (void)flags;
    file->fData = (void *)1; /* 非空标记 */
    return 0;
}

static int VfsTsClose(struct File *file)
{
    (void)file;
    return 0;
}

static ssize_t VfsTsRead(struct File *file, char *buf, size_t len)
{
    size_t n = (file->fOffset + len > sizeof(g_vfsTsBuf)) ? (sizeof(g_vfsTsBuf) - file->fOffset) : len;
    (void)memcpy_s(buf, len, g_vfsTsBuf + file->fOffset, n);
    file->fOffset += n;
    return (ssize_t)n;
}

static ssize_t VfsTsWrite(struct File *file, const char *buf, size_t len)
{
    if (file->fOffset + len > sizeof(g_vfsTsBuf)) {
        return -1;
    }
    (void)memcpy_s(g_vfsTsBuf + file->fOffset, sizeof(g_vfsTsBuf) - file->fOffset, buf, len);
    file->fOffset += len;
    return (ssize_t)len;
}

static off_t VfsTsLseek(struct File *file, off_t off, int whence)
{
    off_t pos;
    /* 可控失败: 命中 magic 偏移时返回 -1, 驱动 pread/pwrite 内部 lseek 失败分支 */
    if ((whence == SEEK_SET) && (off == g_vfsTsLseekMagic)) {
        return (off_t)-1;
    }
    /* 可控失败: 保存位置调用 lseek(0, SEEK_CUR) 失败, 驱动 pread/pwrite 的 savepos 分支 */
    if ((whence == SEEK_CUR) && (off == 0) && g_vfsTsLseekCurFail) {
        return (off_t)-1;
    }
    pos = (whence == SEEK_CUR) ? (file->fOffset + off) :
          ((whence == SEEK_END) ? ((off_t)sizeof(g_vfsTsBuf) + off) : off);
    if ((pos < 0) || (pos > (off_t)sizeof(g_vfsTsBuf))) {
        return (off_t)-1;
    }
    file->fOffset = pos;
    return pos;
}

static int VfsTsStat(struct MountPoint *mp, const char *path, struct stat *st)
{
    (void)mp;
    (void)path;
    st->st_mode = S_IFREG | 0666; /* 0666: 带权限位, 区别于 littlefs */
    st->st_size = 123;            /* 123: 固定大小 */
    return 0;
}

static int VfsTsTruncate(struct File *file, off_t length)
{
    (void)file;
    (void)length;
    return 0;
}

static int VfsTsUnlink(struct MountPoint *mp, const char *path)
{
    (void)mp;
    (void)path;
    return 0;
}

static int VfsTsRename(struct MountPoint *mp, const char *oldp, const char *newp)
{
    (void)mp;
    (void)oldp;
    (void)newp;
    return 0;
}

static int VfsTsIoctl(struct File *file, int cmd, unsigned long arg)
{
    (void)file;
    (void)cmd;
    return (int)arg;
}

static int VfsTsSync(struct File *file)
{
    (void)file;
    return 0;
}

static int VfsTsMkdir(struct MountPoint *mp, const char *path)
{
    (void)mp;
    (void)path;
    return 0;
}

static int VfsTsRmdir(struct MountPoint *mp, const char *path)
{
    (void)mp;
    (void)path;
    return 0;
}

static int VfsTsOpendir(struct Dir *dir, const char *path)
{
    (void)path;
    dir->dData = (void *)1;
    g_vfsTsReaddirEof = 0;
    return 0;
}

static int VfsTsReaddir(struct Dir *dir, struct dirent *dent)
{
    (void)dir;
    if (g_vfsTsReaddirEof) {
        return 1; /* 非零: 本目录项结束 */
    }
    (void)strcpy_s(dent->d_name, sizeof(dent->d_name), "ts_entry");
    dent->d_type = DT_REG;
    g_vfsTsReaddirEof = 1;
    return 0;
}

static int VfsTsClosedir(struct Dir *dir)
{
    (void)dir;
    return g_vfsTsClosedirFail ? -1 : 0;
}

/* ---- "testfs" 操作表 ---- */
static struct MountOps g_vfsTsMops = {
    .mount = VfsTsMount,
    .umount = VfsTsUmount,
    .umount2 = VfsTsUmount2,
    .statfs = VfsTsStatfs,
};

static struct FileOps g_vfsTsFops = {
    .open = VfsTsOpen,
    .close = VfsTsClose,
    .read = VfsTsRead,
    .write = VfsTsWrite,
    .lseek = VfsTsLseek,
    .stat = VfsTsStat,
    .truncate = VfsTsTruncate,
    .unlink = VfsTsUnlink,
    .rename = VfsTsRename,
    .ioctl = VfsTsIoctl,
    .sync = VfsTsSync,
    .opendir = VfsTsOpendir,
    .readdir = VfsTsReaddir,
    .closedir = VfsTsClosedir,
    .mkdir = VfsTsMkdir,
    .rmdir = VfsTsRmdir,
};

/* ---- "minfs": 仅 open/close, 其余操作为 NULL(驱动 ENOTSUP 分支) ---- */
static struct MountOps g_vfsMinMops = {
    .mount = VfsTsMount,
    .umount = VfsTsUmount,
    .umount2 = NULL,
    .statfs = NULL,
};

static struct FileOps g_vfsMinFops = {
    .open = VfsTsOpen,
    .close = VfsTsClose,
};

/* ---- "nullclose": 仅 open, close 为 NULL(驱动 VfsClose 的 ENOTSUP 分支) ---- */
static struct FileOps g_vfsNullCloseFops = {
    .open = VfsTsOpen,
};

/* ---- "diropen": 仅 opendir, readdir/closedir 为 NULL; mops 仅 mount(umount 为 NULL) ---- */
static struct MountOps g_vfsDirOpenMops = {
    .mount = VfsTsMount,
    .umount = NULL,
    .umount2 = NULL,
    .statfs = NULL,
};

static struct FileOps g_vfsDirOpenFops = {
    .opendir = VfsTsOpendir,
};

/* ---- "renull": 有 unlink 无 rename(驱动 VFS rename 的 ENOTSUP 分支) ---- */
static struct FileOps g_vfsReNullFops = {
    .open = VfsTsOpen,
    .close = VfsTsClose,
    .unlink = VfsTsUnlink,
};

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_REGISTER
 * @tc.name     VfsRegister
 * @tc.desc     LOS_FsRegister 成功/重复注册/空操作表
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsRegister, Function | MediumTest | Level1)
{
    int ret;

    ret = LOS_FsRegister("testfs", &g_vfsTsMops, &g_vfsTsFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 重复注册相同类型: 已存在, EINVAL */
    ret = LOS_FsRegister("testfs", &g_vfsTsMops, &g_vfsTsFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 重复注册已存在的 littlefs 类型 */
    ret = LOS_FsRegister("littlefs", &g_vfsTsMops, &g_vfsTsFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 空操作表: EINVAL */
    ret = LOS_FsRegister("badfs", NULL, NULL, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_MOUNT_UMOUNT
 * @tc.name     VfsMountUmount
 * @tc.desc     mount(source 非空)/重复挂载/umount 引用计数/umount 失败/MpDeleteFromList
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsMountUmount, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    struct stat stAfter = { 0 };

    /* source 非空: 覆盖 VfsMountPointInit 的 mDev 分支 */
    ret = mount("testdev0", "/vfsA", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 重复挂载同一路径: EINVAL */
    ret = mount(NULL, "/vfsA", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    ret = mount(NULL, "/vfsB", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 打开文件后 umount: mRefs 非零失败 */
    fd = open("/vfsA/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = umount("/vfsA");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* umount 操作返回失败: 注入 umount=-1 */
    g_vfsTsUmountFail = 1;
    ret = umount("/vfsA");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsUmountFail = 0;

    /* 正常 umount(非链表头: MpDeleteFromList 遍历分支) */
    ret = umount("/vfsA");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = stat("/vfsA/f0", &stAfter);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* umount 不存在的挂载点 */
    ret = umount("/vfs_no_such_mp");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 链表头 umount */
    ret = umount("/vfsB");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_UMOUNT2
 * @tc.name     VfsUmount2
 * @tc.desc     umount2 引用计数/操作失败/MNT_FORCE(遍历关闭 fd)分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsUmount2, Function | MediumTest | Level1)
{
    int ret;
    int fd;

    ret = mount(NULL, "/vfsD", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 有打开文件且未带 MNT_FORCE: mRefs 非零失败 */
    fd = open("/vfsD/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = umount2("/vfsD", 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = umount2("/vfsD", MNT_FORCE);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* umount2 操作返回失败: 注入 */
    g_vfsTsUmount2Fail = 1;
    ret = umount2("/vfsD", 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsUmount2Fail = 0;

    /* MNT_FORCE 无打开文件: CloseFdsInMp 遍历后成功 */
    ret = umount2("/vfsD", MNT_FORCE);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 普通模式成功 */
    ret = mount(NULL, "/vfsD", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount2("/vfsD", 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 未挂载路径与空路径 */
    ret = umount2("/vfs_no_such_mp", MNT_FORCE);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = umount2(NULL, MNT_FORCE);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_REMOUNT_STATFS
 * @tc.name     VfsRemountStatfs
 * @tc.desc     MS_REMOUNT 重挂载与 statfs(自定义实现/littlefs 未实现/不存在路径)
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsRemountStatfs, Function | MediumTest | Level1)
{
    int ret;
    struct statfs sfs = { 0 };

    ret = mount(NULL, "/vfsE", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* MS_REMOUNT: 转发到文件系统 mount 操作 */
    ret = mount(NULL, "/vfsE", "testfs", MS_REMOUNT, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* statfs: 自定义实现, 校验回填字段 */
    ret = statfs("/vfsE", &sfs);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL((int)sfs.f_type, 0x54454653, (int)sfs.f_type);

    /* statfs 参数错误 */
    ret = statfs("/vfsE", NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = statfs(NULL, &sfs);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* statfs 未挂载路径 */
    ret = statfs("/vfs_no_such_mp", &sfs);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* littlefs 未实现 statfs: ENOTSUP */
    ret = statfs(TEST_ROOT, &sfs);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    ret = umount("/vfsE");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_FILE_OPS
 * @tc.name     VfsFileOps
 * @tc.desc     自定义文件系统全量文件操作读写/seek/ioctl/ftruncate/fsync/stat/readdir
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsFileOps, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    off_t off;
    char buf[VFS_TEST_BUF_LEN];
    struct stat st = { 0 };
    DIR *dir = NULL;
    struct dirent *dent = NULL;

    (void)memset_s(g_vfsTsBuf, sizeof(g_vfsTsBuf), 0, sizeof(g_vfsTsBuf));
    ret = mount(NULL, "/vfsC", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    fd = open("/vfsC/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);

    /* write/read/lseek 回环 */
    ret = write(fd, "vfsdata", strlen("vfsdata"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("vfsdata"), ret);
    off = lseek(fd, 0, SEEK_SET);
    ICUNIT_ASSERT_EQUAL((int)off, 0, (int)off);
    ret = read(fd, buf, strlen("vfsdata"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("vfsdata"), ret);
    ICUNIT_ASSERT_EQUAL(memcmp(buf, "vfsdata", strlen("vfsdata")), 0, ret);

    /* 自定义 ioctl: 返回 arg */
    ret = ioctl(fd, 0x1, 0x1234);
    ICUNIT_ASSERT_EQUAL(ret, 0x1234, ret);

    /* 自定义 truncate(非 NULL): ftruncate 成功 */
    ret = ftruncate(fd, 8);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 自定义 sync */
    ret = fsync(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* stat: 带权限位的固定大小 */
    ret = stat("/vfsC/f0", &st);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL((int)st.st_size, 123, (int)st.st_size);
    ret = fstat(fd, &st);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 目录操作 */
    ret = mkdir("/vfsC/d0", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    dir = opendir("/vfsC/d0");
    ICUNIT_ASSERT_NOT_EQUAL(dir, NULL, -1);
    dent = readdir(dir);
    ICUNIT_ASSERT_NOT_EQUAL(dent, NULL, -1);
    ICUNIT_ASSERT_EQUAL(strcmp(dent->d_name, "ts_entry"), 0, dent->d_name[0]);
    /* 第二次 readdir: 自定义 EOF, 返回 NULL */
    dent = readdir(dir);
    ICUNIT_ASSERT_EQUAL(dent, NULL, -1);
    ret = closedir(dir);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* closedir 操作失败分支: 使用独立挂载点(该分支不归还引用计数, 挂载保留至测试结束) */
    g_vfsTsClosedirFail = 1;
    ret = mount(NULL, "/vfsC2", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    dir = opendir("/vfsC2/d0");
    ICUNIT_ASSERT_NOT_EQUAL(dir, NULL, -1);
    ret = closedir(dir);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsClosedirFail = 0;

    /* unlink/rename/rmdir 转发 */
    ret = unlink("/vfsC/f0");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = rename("/vfsC/d0", "/vfsC/d1");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = rmdir("/vfsC/d1");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/vfsC");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_RDONLY_MP
 * @tc.name     VfsRdonlyMp
 * @tc.desc     只读挂载点(mWriteEnable=FALSE)的 open/write/fsync/ftruncate EACCES 分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsRdonlyMp, Function | MediumTest | Level1)
{
    int ret;
    int fd;

    /* 挂载操作将 mp 置只读: 覆盖 mWriteEnable=FALSE 相关分支 */
    g_vfsTsMountRo = 1;
    ret = mount(NULL, "/vfsRO", "testfs", 0, NULL);
    g_vfsTsMountRo = 0;
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 只读挂载点上创建/写打开: EACCES */
    fd = open("/vfsRO/f0", O_CREAT | O_WRONLY, 0666);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);
    ICUNIT_ASSERT_EQUAL(errno, EACCES, errno);
    fd = open("/vfsRO/f0", O_WRONLY);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);

    /* 只读打开成功, 但 ftruncate/fsync 因只读挂载点失败 */
    fd = open("/vfsRO/f0", O_RDONLY);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = ftruncate(fd, 8);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = fsync(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = umount("/vfsRO");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_ENFILE
 * @tc.name     VfsEnfile
 * @tc.desc     打开文件数达到 VFS fd 表上限(NR_OPEN_DEFAULT)返回 ENFILE
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsEnfile, Function | MediumTest | Level1)
{
    int ret;
    int i;
    int opened = 0;
    int hitLimit = 0;
    static int fds[VFS_TEST_OPEN_MAX]; /* 静态数组, 避免占用任务栈 */

    ret = mount(NULL, "/vfsF", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 占满 VFS 文件表(MIN_START_FD=3, 上限 256): 自定义 fs 无文件数限制
     * 探针式打开直到 ENFILE(容忍基线遗留的少量 fd) */
    for (i = 0; i < VFS_TEST_OPEN_MAX; i++) {
        ret = open("/vfsF/lim", O_CREAT | O_RDWR, 0666);
        if (ret == -1) {
            hitLimit = 1;
            break;
        }
        fds[opened++] = ret;
    }
    ICUNIT_GOTO_EQUAL(hitLimit, 1, opened, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENFILE, errno, EXIT);
    /* 至少打开 200 个(容忍基线遗留) */
    ICUNIT_GOTO_NOT_EQUAL(opened < 200, 1, opened, EXIT);

EXIT:
    for (i = 0; i < opened; i++) {
        (void)close(fds[i]);
    }
    ret = umount("/vfsF");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_NULL_OPS
 * @tc.name     VfsNullOps
 * @tc.desc     操作表为 NULL 的文件系统: 各 POSIX 接口 ENOTSUP 分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsNullOps, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    char buf[8] = { 0 }; /* 8 字节读缓冲 */
    struct stat st = { 0 };
    DIR *dir = NULL;
    struct dirent *dent = NULL;

    ret = LOS_FsRegister("minfs", &g_vfsMinMops, &g_vfsMinFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = LOS_FsRegister("nullclose", &g_vfsMinMops, &g_vfsNullCloseFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = mount(NULL, "/vfsG", "minfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    fd = open("/vfsG/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);

    /* read/write/lseek(NULL lseek 返回当前偏移)/fsync/ioctl: ENOTSUP 或降级 */
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = write(fd, "x", 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret); /* fFops->lseek 为 NULL 时返回当前偏移 */
    ret = fsync(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = ioctl(fd, 0x1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = ftruncate(fd, 8);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* stat/unlink/mkdir/rmdir/opendir/rename: ENOTSUP */
    ret = stat("/vfsG/f0", &st);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = unlink("/vfsG/f0");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = mkdir("/vfsG/d0", 0777);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = rmdir("/vfsG/d0");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    dir = opendir("/vfsG/d0");
    ICUNIT_ASSERT_EQUAL(dir, NULL, -1);
    ret = rename("/vfsG/a", "/vfsG/b");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = fstat(fd, &st);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* minfs 的 close 正常返回 0 */
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* nullclose: close 为 NULL */
    ret = mount(NULL, "/vfsH", "nullclose", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/vfsH/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* rmdir 空路径: EINVAL */
    ret = rmdir(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* diropen: opendir 成功但 readdir/closedir 为 NULL */
    ret = LOS_FsRegister("diropen", &g_vfsDirOpenMops, &g_vfsDirOpenFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mount(NULL, "/vfsK", "diropen", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    dir = opendir("/vfsK");
    ICUNIT_ASSERT_NOT_EQUAL(dir, NULL, -1);
    dent = readdir(dir); /* fops->readdir 为 NULL: 返回 NULL */
    ICUNIT_ASSERT_EQUAL(dent, NULL, -1);
    ret = closedir(dir); /* fops->closedir 为 NULL: 返回 -1, DIR 已释放 */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    /* umount 操作为 NULL: 失败 */
    ret = umount("/vfsK");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* renull: unlink 有而 rename 无 */
    ret = LOS_FsRegister("renull", &g_vfsMinMops, &g_vfsReNullFops, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mount(NULL, "/vfsL", "renull", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = rename("/vfsL/a", "/vfsL/b"); /* fops->rename 为 NULL: ENOTSUP */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = unlink("/vfsL/a"); /* unlink 正常 */
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 尽量回收: /vfsG(minfs 有 umount)与 /vfsL 可正常卸载;
     * /vfsH(close 为 NULL 致引用计数无法归零)与 /vfsK(umount 为 NULL)保留至测试结束 */
    ret = umount("/vfsG");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/vfsL");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_RENAME_CROSS
 * @tc.name     VfsRenameCross
 * @tc.desc     跨挂载点 rename 返回 EXDEV 与源/目标不存在分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsRenameCross, Function | MediumTest | Level1)
{
    int ret;

    ret = mount(NULL, "/vfsI", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 源不存在 */
    ret = rename("/no_such_dir/x", TEST_ROOT "/y");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 目标路径不在任何挂载点 */
    ret = rename(TEST_ROOT "/FILE1", "/no_such_dir/y");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 跨文件系统 rename: EXDEV */
    ret = rename(TEST_ROOT "/FILE1", "/vfsI/g");
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EXDEV, errno);

    ret = umount("/vfsI");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_VFS_PREAD_ERR
 * @tc.name     VfsPreadErr
 * @tc.desc     pread/pwrite 内部 lseek 失败分支(保存位置/定位/恢复)
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsPreadErr, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    char buf[8] = { 0 }; /* 8 字节缓冲 */

    ret = mount(NULL, "/vfsJ", "testfs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/vfsJ/f0", O_CREAT | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "abcdefgh", 8); /* 8 字节数据 */
    ICUNIT_ASSERT_EQUAL(ret, 8, ret);

    /* 注入: lseek(SEEK_SET, 0) 失败 -> pread/pwrite 定位失败分支 */
    g_vfsTsLseekMagic = 0;
    ret = pread(fd, buf, 4, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = pwrite(fd, "X", 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsLseekMagic = -1;

    /* 注入: lseek(SEEK_SET, 4) 失败 -> 恢复保存位置失败分支(savepos=4) */
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_ASSERT_EQUAL((int)ret, 0, (int)ret);
    g_vfsTsLseekMagic = 4;
    ret = lseek(fd, 4, SEEK_CUR); /* SEEK_CUR 不受注入影响, 移动到 4 */
    ICUNIT_ASSERT_EQUAL((int)ret, 4, (int)ret);
    ret = pread(fd, buf, 4, 0); /* savepos=4, 定位 0 成功, 恢复 4 失败 */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = pwrite(fd, "X", 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsLseekMagic = -1; /* 清除注入 */

    ret = pread(fd, buf, 4, 0);
    ICUNIT_ASSERT_EQUAL(ret, 4, ret);

    /* 注入: 保存位置 lseek(0, SEEK_CUR) 失败 -> pread/pwrite 的 savepos 分支 */
    g_vfsTsLseekCurFail = 1;
    ret = pread(fd, buf, 4, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = pwrite(fd, "X", 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_vfsTsLseekCurFail = 0;

    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/vfsJ");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

#ifdef LOSCFG_NET_LWIP_SACK
/* *
 * @tc.number   SUB_KERNEL_FS_VFS_SOCKET_FD
 * @tc.name     VfsSocketFd
 * @tc.desc     socket fd 的 write/fcntl/ioctl/close 走 lwip 分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsVfsSocketFd, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    int on = 1;
    char buf[4] = { 0 }; /* 4 字节缓冲 */

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ICUNIT_ASSERT_EQUAL(fd < 256, 0, fd); /* socket fd 应落在文件描述符区之外(>= 256) */

    /* write 转 send(未连接 UDP 报错, 但覆盖分支) */
    ret = write(fd, "s", 1);
    (void)ret;

    /* fcntl 转发 lwip_fcntl */
    ret = fcntl(fd, F_GETFL);
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);
    ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* ioctl 转发 lwip_ioctl */
    ret = ioctl(fd, FIONBIO, &on);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* read 转 recv(未连接 UDP 报错, 但覆盖分支) */
    errno = 0;
    ret = read(fd, buf, sizeof(buf));
    (void)ret;

    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}
#endif

void PosixFsVfsTest(void)
{
#ifdef LOSCFG_NET_LWIP_SACK
    RUN_ONE_TESTCASE(TestFsVfsSocketFd);
#endif
    RUN_ONE_TESTCASE(TestFsVfsRegister);
    RUN_ONE_TESTCASE(TestFsVfsMountUmount);
    RUN_ONE_TESTCASE(TestFsVfsUmount2);
    RUN_ONE_TESTCASE(TestFsVfsRemountStatfs);
    RUN_ONE_TESTCASE(TestFsVfsFileOps);
    RUN_ONE_TESTCASE(TestFsVfsRdonlyMp);
    RUN_ONE_TESTCASE(TestFsVfsEnfile);
    RUN_ONE_TESTCASE(TestFsVfsNullOps);
    RUN_ONE_TESTCASE(TestFsVfsRenameCross);
    RUN_ONE_TESTCASE(TestFsVfsPreadErr);
}
