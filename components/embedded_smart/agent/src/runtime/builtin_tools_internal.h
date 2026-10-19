#ifndef LITECRAB_BUILTIN_TOOLS_INTERNAL_H
#define LITECRAB_BUILTIN_TOOLS_INTERNAL_H

#include "litecrab/runtime.h"

#include <stdint.h>
#include <stddef.h>

int CrabBuiltinRawError(CrabRawResult* result, int code, const char* format, ...);
int CrabBuiltinNormalizePath(const CrabRuntime* runtime,
                             const char* path,
                             char* output,
                             size_t outputSize);
int64_t CrabBuiltinMonotonicMs(void);

int CrabBuiltinExecProgram(CrabRuntime*, const CrabToolCall*, CrabRawResult*);
int CrabBuiltinShell(CrabRuntime*, const CrabToolCall*, CrabRawResult*);
int CrabBuiltinSkillRead(CrabRuntime*, const CrabToolCall*, CrabRawResult*);
int CrabBuiltinSkillComplete(CrabRuntime*, const CrabToolCall*, CrabRawResult*);

#endif
