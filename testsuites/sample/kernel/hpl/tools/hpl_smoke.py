#!/usr/bin/env python3
# Synthetic HPL stream smoke test for the host decompressor tool.
# Stream: checkpoint + one format entry ("val is %d\n") + one args entry (42).
import struct, subprocess, sys, tempfile, os

CK_MAGIC, CK_VER = 0xDEADBEEF, 0x1
FMT_MAGIC, FRAG_MAGIC, ARGS_MAGIC = 0x4F3E89AD, 0x4F3E89AE, 0x4F3E89AF

fmt_str = b"val is %d\n\x00"
filename = b"f.c\x00"
func = b"fn\x00"
analysis = 1  # 1 arg, type 0 (4 bytes)

# HplCompressedFormatEntry bitfields (LE, LSB first):
#  word1: entryType:4 | fmtAnalysisBytes:8 | linenum:20
#  word2: filenameLength:12 | funcStringLength:8 | formatStringLength:12
word1 = (1 & 0xF) | ((8 & 0xFF) << 4) | ((3 & 0xFFFFF) << 12)  # entryType=FORMAT_ENTRY(1), analysis 8B, line 3
word2 = (len(filename) & 0xFFF) | ((len(func) & 0xFF) << 12) | ((len(fmt_str) & 0xFFF) << 20)
fmt_entry = struct.pack("<III", FMT_MAGIC, word1, word2) + struct.pack("<Q", analysis) + filename + func + fmt_str

ts = 0x1122334455667788
args_payload = struct.pack("<I", 42)  # one 4-byte argument
total_len = 12 + 8 + len(args_payload)  # header + timestamp + args
#  word: entryType:4 | additionalTimeStampBytes:4 | protocolVersion:8 | totalEntryLength:16
aword = (3 & 0xF) | ((8 & 0xF) << 4) | ((CK_VER & 0xFF) << 8) | ((total_len & 0xFFFF) << 16)
args_entry = struct.pack("<III", ARGS_MAGIC, aword, 0) + struct.pack("<Q", ts) + args_payload  # formatId=0

stream = struct.pack("<II", CK_MAGIC, CK_VER) + fmt_entry + args_entry

with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
    f.write(stream)
    path = f.name

tool = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "..", "..", "..", "..", "..",
                    "kernel", "extended", "hpl", "mkDecompressor", "decompressor")
r = subprocess.run([tool, path], capture_output=True, text=True, timeout=10)
os.unlink(path)
print("stdout:", repr(r.stdout))
print("stderr:", repr(r.stderr))
ok = r.stdout == "val is 42\n"
print("SMOKE:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
