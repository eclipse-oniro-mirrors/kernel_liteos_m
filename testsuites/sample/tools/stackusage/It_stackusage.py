#!/usr/bin/env python3
"""stackusage 模块套件入口 (ICUNIT It_los_task.c 套件注册的 python 镜像).

结构对齐 it_los_atomic_001.c + It_los_task.c:
  - It_stackusage_NNN.py 内 ItStackusageNNN() 仅做 TEST_ADD_CASE 自注册
  - 本入口依序调用注册函数, 再由 ICunitRun 统一执行并输出板端同款日志
前置(真实镜像类用例):  artifacts/qemu_baseline + artifacts/ws63_baseline
       (板级阶段归档); 合成样本随仓 fx/。缺失时用例记 [Skipped], 不计 FAIL。
用法:  python3 It_stackusage.py [用例名...]   (默认全部)
"""
import sys

from os_test import ICunitRun
from It_stackusage_001 import ItStackusage001
from It_stackusage_002 import ItStackusage002
from It_stackusage_003 import ItStackusage003
from It_stackusage_004 import ItStackusage004
from It_stackusage_005 import ItStackusage005
from It_stackusage_006 import ItStackusage006
from It_stackusage_007 import ItStackusage007
from It_stackusage_008 import ItStackusage008
from It_stackusage_009 import ItStackusage009
from It_stackusage_010 import ItStackusage010
from It_stackusage_011 import ItStackusage011
from It_stackusage_012 import ItStackusage012
from It_stackusage_013 import ItStackusage013
from It_stackusage_014 import ItStackusage014
from It_stackusage_015 import ItStackusage015
from It_stackusage_016 import ItStackusage016
from It_stackusage_017 import ItStackusage017
from It_stackusage_018 import ItStackusage018
from It_stackusage_019 import ItStackusage019
from It_stackusage_020 import ItStackusage020
from It_stackusage_021 import ItStackusage021
from It_stackusage_022 import ItStackusage022
from It_stackusage_023 import ItStackusage023
from It_stackusage_031 import ItStackusage031

ItStackusage001()   # TEST_ADD_CASE 自注册 (镜像 It_los_task.c 中 ItLosXxx() 注册调用)
ItStackusage002()
ItStackusage003()
ItStackusage004()
ItStackusage005()
ItStackusage006()
ItStackusage007()
ItStackusage008()
ItStackusage009()
ItStackusage010()
ItStackusage011()
ItStackusage012()
ItStackusage013()
ItStackusage014()
ItStackusage015()
ItStackusage016()
ItStackusage017()
ItStackusage018()
ItStackusage019()
ItStackusage020()
ItStackusage021()
ItStackusage022()
ItStackusage023()
ItStackusage031()

def ItSuiteStackusage(only=None):
    return ICunitRun(only)

if __name__ == "__main__":
    sys.exit(1 if ItSuiteStackusage(sys.argv[1:] or None) else 0)
