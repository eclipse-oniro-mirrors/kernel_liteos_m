/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "los_event.h"
#include "los_hpl_logger_pri.h"
#include "los_hpl_compression_pri.h"
#include "los_task_pri.h"
#include "los_hpl_pri.h"
#include "los_hpl_ringbuf_pri.h"

STATIC BOOL OsHplRingbufRemainCheck(HplRingbuf *ringbuf, UINT32 size)
{
    UINT32 remain = OsHplRingbufRemain(ringbuf);
    if (remain < size) {
        /* Compress function will be pended with mutex, so it is forbidden to be invoked in interrupt context. */
        if (OS_EXC_ACTIVE) {
            OsHplExcCompressLog();
        } else if (OS_INT_ACTIVE) {
            ringbuf->overflow = TRUE;
            return FALSE;
        } else {
            (VOID)LOS_EventWrite(&g_runtimeLogger.event, HPL_TRIGGER_EVENT);
        }
        ringbuf->overflow = TRUE;
        /* Regain the remaining size of ringbuf. */
        remain = OsHplRingbufRemain(ringbuf);
    }

    return remain >= size ? TRUE : FALSE;
}

VOID OsHplRingbufInit(HplRingbuf *ringbuf, CHAR *fifo, UINT32 size)
{
    ringbuf->startIdx = 0;
    ringbuf->endIdx = 0;
    ringbuf->peakSize = 0;
    ringbuf->overflow = FALSE;
    ringbuf->fifo = fifo;
    ringbuf->size = size;
    LOS_SpinInit(&ringbuf->ringLock);
}

UINT32 OsHplRingbufUsedSize(HplRingbuf *ringbuf)
{
    return ringbuf->endIdx - ringbuf->startIdx;
}

UINT32 OsHplRingbufRemain(HplRingbuf *ringbuf)
{
    return ringbuf->size - OsHplRingbufUsedSize(ringbuf);
}

UINT32 OsHplRingbufWrite(HplRingbuf *ringbuf, const CHAR *buf, UINT32 size)
{
    UINT32 linearResidue;
    UINT32 end;
    UINT32 start;
    errno_t err;
    UINT32 startIdx;

    if ((ringbuf->size == 0) || (!OsHplRingbufRemainCheck(ringbuf, size)) || (size == 0)) {
        return 0;
    }

    startIdx = ringbuf->startIdx;
    start = ringbuf->startIdx % ringbuf->size;
    end = ringbuf->endIdx % ringbuf->size;
    /* --------start--------end--------- */
    if (end >= start) {
        linearResidue = ringbuf->size - end;
        if (linearResidue >= size) {
            err = memcpy_s(&ringbuf->fifo[end], linearResidue, buf, size);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
        } else {
            err = memcpy_s(&ringbuf->fifo[end], linearResidue, buf, linearResidue);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
            err = memcpy_s(&ringbuf->fifo[0], start, &buf[linearResidue], size - linearResidue);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
        }
    } else { /* --------end--------start--------- */
        linearResidue = start - end;
        err = memcpy_s(&ringbuf->fifo[end], linearResidue, buf, size);
        if (err != EOK) {
            dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
            return 0;
        }
    }
    dsb();
    ringbuf->endIdx += size;
    dsb();
    ringbuf->peakSize = (ringbuf->endIdx - startIdx) > ringbuf->peakSize ? (ringbuf->endIdx - startIdx) :
                        ringbuf->peakSize;
    return size;
}

UINT32 OsHplRingbufWriteWithLock(HplRingbuf *ringbuf, const CHAR *buf, UINT32 size)
{
    UINT32 intSave;
    UINT32 ret;
    LOS_SpinLockSave(&ringbuf->ringLock, &intSave);
    ret = OsHplRingbufWrite(ringbuf, buf, size);
    LOS_SpinUnlockRestore(&ringbuf->ringLock, intSave);
    return ret;
}

STATIC UINT32 OsHplRingbufDoRead(HplRingbuf *ringbuf, UINT32 start, UINT32 end, CHAR *buf, UINT32 maxSize)
{
    UINT32 linearUsedSize;
    errno_t err;

    /* --------start--------end--------- */
    if (end > start) {
        err = memcpy_s(buf, maxSize, &ringbuf->fifo[start], maxSize);
        if (err != EOK) {
            dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
            return 0;
        }
    } else { /* --------end--------start--------- */
        linearUsedSize = ringbuf->size - start;
        if (linearUsedSize >= maxSize) {
            err = memcpy_s(buf, maxSize, &ringbuf->fifo[start], maxSize);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
        } else {
            err = memcpy_s(buf, maxSize, &ringbuf->fifo[start], linearUsedSize);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
            err = memcpy_s(&buf[linearUsedSize], maxSize - linearUsedSize, &ringbuf->fifo[0], maxSize - linearUsedSize);
            if (err != EOK) {
                dprintf("memcpy err, start %u, end %u, line %u\n", start, end, __LINE__);
                return 0;
            }
        }
    }
    return maxSize;
}

UINT32 OsHplRingbufRead(HplRingbuf *ringbuf, CHAR *buf, UINT32 maxSize)
{
    UINT32 end;
    UINT32 start;
    UINT32 rdSize;
    UINT32 usedSize = OsHplRingbufUsedSize(ringbuf);
    /* --------start/end--------- */
    if ((maxSize == 0) || (usedSize == 0)) {
        return 0;
    }

    maxSize = usedSize > maxSize ? maxSize : usedSize;
    end = ringbuf->endIdx % ringbuf->size;
    start = ringbuf->startIdx % ringbuf->size;

    rdSize = OsHplRingbufDoRead(ringbuf, start, end, buf, maxSize);
    dsb();
    ringbuf->startIdx += rdSize;
    dsb();
    return rdSize;
}

UINT32 OsHplRingbufPeekUsedSize(HplRingbuf *ringbuf, UINT32 peekSize)
{
    return ringbuf->endIdx - (ringbuf->startIdx + peekSize);
}

UINT32 OsHplRingbufPeekRemain(HplRingbuf *ringbuf, UINT32 peekSize)
{
    return ringbuf->size - OsHplRingbufPeekUsedSize(ringbuf, peekSize);
}

UINT32 OsHplRingbufPeekRead(HplRingbuf *ringbuf, CHAR *buf, UINT32 maxSize, UINT32 peekSize)
{
    UINT32 end;
    UINT32 start;
    UINT32 usedSize = OsHplRingbufPeekUsedSize(ringbuf, peekSize);
    /* --------start/end--------- */
    if ((maxSize == 0) || usedSize == 0) {
        return 0;
    }
    maxSize = usedSize > maxSize ? maxSize : usedSize;

    end = ringbuf->endIdx % ringbuf->size;
    start = (ringbuf->startIdx + peekSize) % ringbuf->size;
    return OsHplRingbufDoRead(ringbuf, start, end, buf, maxSize);
}

VOID OsHplRingbufReadPeekDone(HplRingbuf *ringbuf, UINT32 peekSize)
{
    dsb();
    ringbuf->startIdx += peekSize;
    dsb();
}
