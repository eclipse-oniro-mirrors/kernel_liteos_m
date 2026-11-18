#!/usr/bin/env python3
"""footprint 模块套件入口 (ICUNIT It_los_task.c 套件注册的 python 镜像).

结构对齐 it_los_atomic_001.c + It_los_task.c:
  - It_footprint_NNN.py 内 ItFootprintNNN() 仅做 TEST_ADD_CASE 自注册
  - 本入口依序调用注册函数, 再由 ICunitRun 统一执行并输出板端同款日志
前置:  artifacts/qemu_baseline (板级阶段归档) + fx/ 夹具 (gen_inputs.py 再生成),
       缺失时套件整体 SKIP, 不计 FAIL。
用法:  python3 It_footprint.py [用例名...]   (默认全部)
"""
import sys

from os_test import ICunitRun, log, have_replay_inputs
from It_footprint_001 import ItFootprint001
from It_footprint_002 import ItFootprint002
from It_footprint_003 import ItFootprint003
from It_footprint_004 import ItFootprint004
from It_footprint_005 import ItFootprint005
from It_footprint_006 import ItFootprint006
from It_footprint_007 import ItFootprint007
from It_footprint_008 import ItFootprint008
from It_footprint_009 import ItFootprint009
from It_footprint_010 import ItFootprint010
from It_footprint_011 import ItFootprint011
from It_footprint_012 import ItFootprint012
from It_footprint_013 import ItFootprint013
from It_footprint_014 import ItFootprint014
from It_footprint_015 import ItFootprint015
from It_footprint_016 import ItFootprint016
from It_footprint_017 import ItFootprint017
from It_footprint_021 import ItFootprint021
from It_footprint_022 import ItFootprint022
from It_footprint_023 import ItFootprint023

ItFootprint001()   # TEST_ADD_CASE 自注册 (镜像 It_los_task.c 中 ItLosXxx() 注册调用)
ItFootprint002()
ItFootprint003()
ItFootprint004()
ItFootprint005()
ItFootprint006()
ItFootprint007()
ItFootprint008()
ItFootprint009()
ItFootprint010()
ItFootprint011()
ItFootprint012()
ItFootprint013()
ItFootprint014()
ItFootprint015()
ItFootprint016()
ItFootprint017()
ItFootprint021()
ItFootprint022()
ItFootprint023()

def ItSuiteFootprint(only=None):
    if not have_replay_inputs():
        log("[ItSuiteFootprint] SKIP (缺板级阶段产物 qemu_baseline 或 fx 夹具: "
            "先执行板级阶段构建归档并运行 gen_inputs.py 再回放)")
        return 0
    return ICunitRun(only)

if __name__ == "__main__":
    sys.exit(1 if ItSuiteFootprint(sys.argv[1:] or None) else 0)
