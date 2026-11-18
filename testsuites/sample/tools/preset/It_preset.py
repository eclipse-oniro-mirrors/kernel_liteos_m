#!/usr/bin/env python3
"""preset 模块套件入口 (ICUNIT It_los_task.c 套件注册的 python 镜像).

结构对齐 it_los_atomic_001.c + It_los_task.c:
  - It_preset_NNN.py 内 ItPresetNNN() 仅做 TEST_ADD_CASE 自注册
  - 本入口依序调用注册函数, 再由 ICunitRun 统一执行并输出板端同款日志
结果 -> run_20261112/preset/<case>/result.md; ItPreset002 挡位产物 -> itp002/.
用法:  python3 It_preset.py [用例名...]   (默认全部)
"""
import sys

from os_test import ICunitRun
from It_preset_001 import ItPreset001
from It_preset_002 import ItPreset002
from It_preset_003 import ItPreset003
from It_preset_004 import ItPreset004
from It_preset_005 import ItPreset005
from It_preset_006 import ItPreset006
from It_preset_007 import ItPreset007
from It_preset_008 import ItPreset008
from It_preset_009 import ItPreset009
from It_preset_013 import ItPreset013
from It_preset_014 import ItPreset014

ItPreset001()   # TEST_ADD_CASE 自注册 (镜像 It_los_task.c 中 ItLosXxx() 注册调用)
ItPreset002()
ItPreset003()
ItPreset004()
ItPreset005()
ItPreset006()
ItPreset007()
ItPreset008()
ItPreset009()
ItPreset013()
ItPreset014()

def ItSuitePreset(only=None):
    return ICunitRun(only)

if __name__ == "__main__":
    sys.exit(1 if ItSuitePreset(sys.argv[1:] or None) else 0)
