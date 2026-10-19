#ifndef LITECRAB_PROMPT_H
#define LITECRAB_PROMPT_H

#include <stddef.h>

typedef struct {
    const char* productId;
    int includeSkills;
} LiteCrabPromptRequest;

typedef struct {
    void* context;
    int (*build)(void* context,
                 const LiteCrabPromptRequest* request,
                 char* output,
                 size_t outputSize);
} LiteCrabPromptProvider;

int LiteCrabPromptSetProvider(const LiteCrabPromptProvider* provider);
void LiteCrabPromptResetProvider(void);
int LiteCrabPromptBuildBase(const LiteCrabPromptRequest* request, char* output, size_t outputSize);

#endif
