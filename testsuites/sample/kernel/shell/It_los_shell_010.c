/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_shell.h"
#include "shcmd.h"

#if defined(LOSCFG_SHELL)

/* 用例简要描述: VfsNormalizePath 路径规范化直调全分支(纯字符串运算,不依赖文件系统):
 * 参数防御(NULL/空串/超长)、目录为 NULL、TEMP_PATH_MAX 超限、"//"开头绝对路径、
 * "./"前缀跳过、多斜杠折叠、./与../回溯、根钳位、尾斜杠去除、点号字面名、组合超长 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    CHAR *out = NULL;
    static CHAR bigDir[601];   /* 600 字符目录,触发 TEMP_PATH_MAX 超限 */
    static CHAR bigName[251];  /* 250 字符文件名,组合后超 PATH_MAX */
    static CHAR pathName[300]; /* 299 字符文件名,触发 namelen >= PATH_MAX */

    /* F-Inv: pathname 为 NULL,参数防御 */
    ret = VfsNormalizePath("/dir", "file", NULL);
    ICUNIT_GOTO_EQUAL(ret, -EINVAL, ret, EXIT);

    /* F-Inv: filename 为 NULL,参数防御 */
    ret = VfsNormalizePath("/dir", NULL, &out);
    ICUNIT_GOTO_EQUAL(ret, -EINVAL, ret, EXIT);

    /* F-Inv: filename 为空串 */
    ret = VfsNormalizePath("/dir", "", &out);
    ICUNIT_GOTO_EQUAL(ret, -EINVAL, ret, EXIT);

    /* F-Inv: filename 长度 >= PATH_MAX(256) */
    (VOID)memset_s(pathName, sizeof(pathName), 'a', sizeof(pathName) - 1);
    pathName[sizeof(pathName) - 1] = '\0';
    ret = VfsNormalizePath("/dir", pathName, &out);
    ICUNIT_GOTO_EQUAL(ret, -ENAMETOOLONG, ret, EXIT);

    /* F-Inv: directory 为 NULL 且 filename 为相对路径(NO_WORKING_DIR) */
    ret = VfsNormalizePath(NULL, "file", &out);
    ICUNIT_GOTO_EQUAL(ret, -EINVAL, ret, EXIT);

    /* F-Inv: directory 超长,strlen(dir)+namelen+2 > TEMP_PATH_MAX */
    (VOID)memset_s(bigDir, sizeof(bigDir), 'd', sizeof(bigDir) - 1);
    bigDir[sizeof(bigDir) - 1] = '\0';
    ret = VfsNormalizePath(bigDir, "f", &out);
    ICUNIT_GOTO_EQUAL(ret, -ENAMETOOLONG, ret, EXIT);

    /* F-Inv: 绝对路径以 "//" 开头(filename[1] == '/' 防御) */
    ret = VfsNormalizePath(NULL, "//x", &out);
    ICUNIT_GOTO_EQUAL(ret, -EINVAL, ret, EXIT);

    /* F-Inv: 目录+文件名组合后 >= PATH_MAX(250+250 拼接为 501) */
    (VOID)memset_s(bigName, sizeof(bigName), 'n', sizeof(bigName) - 1);
    bigName[sizeof(bigName) - 1] = '\0';
    ret = VfsNormalizePath(bigName, bigName, &out);
    ICUNIT_GOTO_EQUAL(ret, -ENAMETOOLONG, ret, EXIT);

    /* F-Pos: 相对路径基本拼接 */
    ret = VfsNormalizePath("/work", "file", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/work/file", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: "./" 前缀跳过 */
    ret = VfsNormalizePath("/work", "./file", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/work/file", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 绝对路径 + 多余斜杠折叠(StrPath) */
    ret = VfsNormalizePath(NULL, "/a//b///c", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/a/b/c", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 中间 "./" 消除 */
    ret = VfsNormalizePath(NULL, "/a/./b", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/a/b", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: "../" 回溯一级 */
    ret = VfsNormalizePath(NULL, "/a/b/../c", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/a/c", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 连续 "../" 回溯到根并钳位 */
    ret = VfsNormalizePath(NULL, "/a/../..", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 根下 ".." 钳位 */
    ret = VfsNormalizePath(NULL, "/..", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: "/a/.." 回溯到根 */
    ret = VfsNormalizePath(NULL, "/a/..", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 根下 "." 去除(StrRemovePathEndSlash 的 '.' 分支) */
    ret = VfsNormalizePath(NULL, "/.", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 尾部 "/." 去除 */
    ret = VfsNormalizePath(NULL, "/a/.", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/a", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 尾斜杠去除 */
    ret = VfsNormalizePath(NULL, "/a/b/", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/a/b", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 相对路径 "../" 回溯 */
    ret = VfsNormalizePath("/work", "a/../b", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/work/b", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: ".." 后非 '/' 非结束,按字面名处理 */
    ret = VfsNormalizePath("/work", "..lit", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/work/..lit", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: "." 后非 '/' 非结束,按字面名处理(隐藏文件) */
    ret = VfsNormalizePath("/work", ".hid", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/work/.hid", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 根路径原样返回 */
    ret = VfsNormalizePath(NULL, "/", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/", 0, EXIT);
    free(out);
    out = NULL;

    /* F-Pos: 绝对路径直接采用(忽略 directory) */
    ret = VfsNormalizePath("/work", "/abc/def", &out);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(out, "/abc/def", 0, EXIT);
    free(out);
    out = NULL;

EXIT:
    /* 异常路径同样释放规范化输出缓冲 */
    if (out != NULL) {
        free(out);
    }
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell010
 * @tc.desc     验证 VfsNormalizePath 路径规范化对外接口: NULL/空串/超长文件名等
 *              参数防御返回 -EINVAL/-ENAMETOOLONG; directory 为 NULL 的相对路径
 *              拒绝; "//" 开头绝对路径拒绝; "./" 前缀跳过; 多余斜杠折叠;
 *              "./" "../" 规范化与根钳位; 尾斜杠与尾 "/." 去除; 点号字面名保留;
 *              目录与文件名组合超 PATH_MAX 拒绝。纯字符串运算,双板行为一致
 */
VOID ItLosShell010(VOID)
{
    TEST_ADD_CASE("ItLosShell010", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
