#!/usr/bin/env python3
# coding: utf-8
# Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
'''
Utils method
'''

import sys
import subprocess
import shlex
import logging
import traceback

LOG_FORMAT = '[%(funcName)s:%(lineno)d, %(levelname)s]: %(message)s'
logging.basicConfig(level=logging.NOTSET, format=LOG_FORMAT)


class Utils:
    '''
    Class for general function
    '''
    @staticmethod
    def run_shell_return(command, directory):
        '''
        Run the shell command
        '''
        is_list = isinstance(command, list)
        if is_list:
            command = shlex.split(' '.join(command)) # avoid manual spilt not right
        try:
            pipe_handle = subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE, \
                    stderr=subprocess.PIPE, shell=(is_list is False), universal_newlines=True)

            output, errout = pipe_handle.communicate(timeout=600)
            if errout:
                logging.error("%s", errout)
                traceback.print_stack()
        except SystemError:
            logging.error("%s", ' '.join(command))
            traceback.print_stack()
        return output, pipe_handle.returncode

    @staticmethod
    def err_exit(info):
        '''
        Error exit
        '''
        logging.error("%s", info)
        traceback.print_stack()
        sys.exit(1)
