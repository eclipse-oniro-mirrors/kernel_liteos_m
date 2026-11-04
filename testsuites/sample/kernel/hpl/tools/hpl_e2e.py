#!/usr/bin/env python3
"""Host-side HPL end-to-end verification (stage 3).

Pipeline: QEMU serial log -> extract "HPLDUMP:" HEX lines -> rebuild the
compressed stream -> mkDecompressor/decompressor -> compare the restored text
against the anchors emitted by It_los_hpl_031.c (in order).

Usage: hpl_e2e.py <qemu_serial_log> [--metadata <file>] [--print-raw]
  --metadata: script-preprocessor mode; the format dictionary comes from the
  4-lines-per-entry metadata file generated at build time
  (out/<board>/<product>/hpl/preprocess/Metadata_0) via "decompressor -m".
  In that mode the args format ids already match the global dictionary, so no
  checkpoint prefix nor fmtId remap is applied.
Exit code 0 = PASS, 1 = FAIL.
"""
import os
import struct
import subprocess
import sys
import tempfile

CK_MAGIC, CK_VER = 0xDEADBEEF, 0x1
FMT_MAGIC, FRAG_MAGIC, ARGS_MAGIC = 0x4F3E89AD, 0x4F3E89AE, 0x4F3E89AF
ABANDON_MAGIC, CUSTOM_MAGIC = 0xDEADDEAD, 0xCE31BD9A


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def walk_entries(stream):
    """Yield ('fmt', off) for format entries and ('args', off) for args
    entries (offset of the entry start). Mirrors the decompressor parser:
    unknown magics advance one byte."""
    i = 0
    n = len(stream)
    while i + 4 <= n:
        magic = u32(stream, i)
        if magic == CK_MAGIC:
            i += 8
        elif magic == FMT_MAGIC:
            w1 = u32(stream, i + 4)
            w2 = u32(stream, i + 8)
            analysis_bytes = (w1 >> 4) & 0xFF
            filename_len = w2 & 0xFFF
            func_len = (w2 >> 12) & 0xFF
            fmt_len = (w2 >> 20) & 0xFFF
            i += 12 + analysis_bytes + filename_len + func_len + fmt_len
            yield ("fmt", i)  # not used; fmt count kept by caller
        elif magic == FRAG_MAGIC:
            w = u32(stream, i + 4)
            frag_bytes = (w >> 4) & 0xFFFFFFF
            total = u32(stream, i + 8)
            j = i + 12
            end = min(i + frag_bytes, n)
            for _ in range(total):
                if j + 12 > end:
                    break
                yield ("args", j)
                tlen = (u32(stream, j + 4) >> 16) & 0xFFFF
                if tlen < 12:
                    break
                j += tlen
            i += max(frag_bytes, 12)
        elif magic == ARGS_MAGIC:
            yield ("args", i)
            tlen = (u32(stream, i + 4) >> 16) & 0xFFFF
            i += max(tlen, 12)
        elif magic == ABANDON_MAGIC:
            cnt = u32(stream, i + 4)
            i += 8 + cnt * 12
        elif magic == CUSTOM_MAGIC:
            dlen = u32(stream, i + 12)
            i += 16 + dlen
        else:
            i += 1


def remap_fmtids(stream):
    """The decompressor numbers format entries by arrival order (local ids
    0..N-1) because a format entry carries no id of its own. An incremental
    stream collected mid-run misses earlier format entries, so the args
    entries still reference the original global ids. This patches every args
    formatId back to the local numbering when the ids form a contiguous run
    of exactly N entries; ids outside the run are left untouched (the tool
    skips them with a debug note)."""
    fmt_cnt = 0
    args_offs = []
    for kind, off in walk_entries(stream):
        if kind == "fmt":
            fmt_cnt += 1
        else:
            args_offs.append(off)
    if fmt_cnt == 0 or not args_offs:
        return stream, 0
    ids = sorted(u32(stream, off + 8) for off in args_offs)
    # Longest contiguous run in ids.
    best_x, best_len, run_x, run_len = None, 0, ids[0], 1
    for a, b in zip(ids, ids[1:]):
        if b == a + 1:
            run_len += 1
        else:
            if run_len > best_len:
                best_x, best_len = run_x, run_len
            run_x, run_len = b, 1
    if run_len > best_len:
        best_x, best_len = run_x, run_len
    if best_len != fmt_cnt:
        raise RuntimeError(
            "cannot remap: %d args form no contiguous run of %d format ids (ids=%s)"
            % (len(ids), fmt_cnt, ids[:32]))
    x = best_x
    patched = 0
    for off in args_offs:
        fid = u32(stream, off + 8)
        if x <= fid < x + fmt_cnt:
            struct.pack_into("<I", stream, off + 8, fid - x)
            patched += 1
    return stream, patched

# Anchors: the exact texts the decompressor must restore, in the order the
# It_los_hpl_031.c case emits them. Keep in sync with that file.
ANCHORS = [
    "e2e01 d=42\n",
    "e2e02 s=endtoend\n",
    "e2e03 x=0xabcd\n",
    "e2e04 u=123456789\n",
    "e2e05 c=Z\n",
    "e2e06 ld=-100000\n",
    "e2e07 lld=9007199254740993\n",
    "e2e08 f=3.140000\n",
    "e2e09 pct=%d done\n",
    "e2e10 p=0x1234\n",
]

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "..", "..", "..",
                    "kernel", "extended", "hpl", "mkDecompressor", "decompressor")


def extract_stream(log_path):
    data = bytearray()
    declared = None
    with open(log_path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("HPLDUMP_BEGIN:"):
                declared = int(line.split(":", 1)[1].strip())
            elif line.startswith("HPLDUMP:"):
                hexpart = line[len("HPLDUMP:"):].strip()
                if len(hexpart) % 2 != 0:
                    raise RuntimeError("odd HEX length in dump line: %r" % line)
                data.extend(bytes.fromhex(hexpart))
            elif line.startswith("HPLDUMP_END"):
                break
    if declared is None:
        raise RuntimeError("no HPLDUMP_BEGIN found in %s" % log_path)
    if declared != len(data):
        raise RuntimeError("dump size mismatch: declared %d, got %d" % (declared, len(data)))
    return bytes(data)


def tool_is_script_mode():
    """The tool bakes its CLI shape in at build time (config.h): a
    SCRIPT_PREPROCESSOR build requires '-m metadata...' while a plain build
    takes self-describing streams. Probe the usage line."""
    r = subprocess.run([TOOL], capture_output=True, text=True, timeout=10)
    return "-m" in (r.stdout + r.stderr)


def main():
    if len(sys.argv) < 2:
        print("usage: hpl_e2e.py <qemu_serial_log> [--metadata <file>] [--print-raw]")
        return 1
    raw = "--print-raw" in sys.argv
    metadata = None
    if "--metadata" in sys.argv:
        metadata = sys.argv[sys.argv.index("--metadata") + 1]

    script_tool = tool_is_script_mode()
    if script_tool and metadata is None:
        print("E2E: FAIL - the decompressor was built with "
              "LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR: pass "
              "--metadata <Metadata_0> or rebuild the tool from a non-SCRIPT config")
        return 1
    if (not script_tool) and metadata is not None:
        print("E2E: FAIL - the decompressor is a NON-SCRIPT build (its CLI has "
              "no -m mode) while --metadata was given: the stream carries "
              "pre-allocated format ids that only the build-time dictionary "
              "can resolve. Rebuild the tool with "
              "'make -C kernel/liteos_m/kernel/extended/hpl/mkDecompressor' "
              "from the SCRIPT-enabled config, or drop --metadata")
        return 1

    stream = extract_stream(sys.argv[1])
    print("stream bytes: %d" % len(stream))

    if not script_tool:
        # The collector is installed long after system start, so the increment
        # misses the one-shot startup checkpoint. The decompressor needs it to
        # initialise its format dictionary: prepend it unless already present.
        stream = bytearray(stream)
        if stream[:4] != struct.pack("<I", CK_MAGIC):
            stream = bytearray(struct.pack("<II", CK_MAGIC, CK_VER)) + stream

        # Patch the args format ids to the stream-local numbering (see remap_fmtids).
        stream, patched = remap_fmtids(stream)
        print("fmtId remap: %d args entries patched" % patched)
        cmd = [TOOL, "log.bin"]
    else:
        # Script-preprocessor mode: the metadata dictionary matches the global
        # format ids carried by the stream and its load initialises the
        # decompressor, so neither the checkpoint prefix nor the remap applies.
        if not os.path.isabs(metadata):
            # Relative metadata paths are rooted at the liteos_new workspace.
            metadata = os.path.abspath(os.path.join(
                HERE, "..", "..", "..", "..", "..", "..", "..", metadata))
        cmd = [TOOL, "-m", metadata, "-l", "log.bin"]

    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
        f.write(stream)
        bin_path = f.name
    try:
        cmd = [c.replace("log.bin", bin_path) if c == "log.bin" else c for c in cmd]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    finally:
        os.unlink(bin_path)
    text = r.stdout
    if raw:
        sys.stdout.write(text)
    print("restored bytes: %d (tool rc=%d)" % (len(text), r.returncode))
    if r.stderr:
        print("tool stderr: %r" % r.stderr)

    pos = 0
    failed = []
    for anchor in ANCHORS:
        idx = text.find(anchor, pos)
        if idx < 0:
            failed.append(anchor)
            continue
        pos = idx + len(anchor)
    if failed:
        print("E2E: FAIL")
        for a in failed:
            print("  missing anchor: %r" % a)
        # Diagnostic: dump the lines that share the anchor prefix.
        for a in failed:
            key = a.split()[0]
            for ln in text.splitlines():
                if key in ln:
                    print("  got line: %r" % ln)
        return 1
    print("E2E: PASS (%d anchors restored in order)" % len(ANCHORS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
