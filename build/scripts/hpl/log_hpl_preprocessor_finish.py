#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
