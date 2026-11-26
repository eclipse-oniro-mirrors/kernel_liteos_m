#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used
#    to endorse or promote products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
'''
Description: Parse high performance log to calculate the arguments number and type
'''

import sys
import os
import logging
import stat

LOG_FORMAT = '[%(funcName)s:%(lineno)d, %(levelname)s]: %(message)s'
logging.basicConfig(level=logging.NOTSET, format=LOG_FORMAT)

metadata_info_after = [
    "};\n",
    "typedef struct {\n",
    "    UINT32 magic;\n",
    "    UINT32 version;\n",
    "    UINT32 formatNum;\n",
    "    UINT32 reserve;\n",
    "} HplMetadataHeader;\n",
    "HplMetadataHeader g_hplFormatHeader ATTR_SECTION1 ATTR_USED = {\n",
    "    .magic = 0xDEADBEEF,\n",
    "    .version = 0x1,\n",
    "    .formatNum = sizeof(g_metadata) / sizeof(HplMetadataInfo),\n",
    "};\n",
]

# argv[1]: metadata file path
if __name__ == '__main__':
    metadata_filepath = sys.argv[1]
    logging.info("finish metadata file path: %s", metadata_filepath)

    # open with write only
    file_flags = os.O_WRONLY | os.O_APPEND | os.O_FSYNC
    # Write by owner | Read by owner | Read by group | Read by others
    metadata_file_modes = stat.S_IWUSR | stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH
    with os.fdopen(os.open(metadata_filepath, file_flags, metadata_file_modes), 'a') as metadata_fd:
        metadata_fd.writelines(metadata_info_after)
