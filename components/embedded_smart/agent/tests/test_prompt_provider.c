#include "litecrab/prompt.h"

#include <stdio.h>
#include <string.h>

static int custom_build(void* context,
                        const LiteCrabPromptRequest* request,
                        char* output,
                        size_t outputSize) {
    return snprintf(output,
                    outputSize,
                    "%s:%s:%d",
                    (const char*) context,
                    request->productId,
                    request->includeSkills) >= (int) outputSize
               ? -1
               : 0;
}

int main(void) {
    char output[128];
    LiteCrabPromptProvider provider = {.context = "custom", .build = custom_build};
    LiteCrabPromptRequest request = {.productId = "unit", .includeSkills = 1};
    if (LiteCrabPromptSetProvider(&provider) ||
        LiteCrabPromptBuildBase(&request, output, sizeof output) ||
        strcmp(output, "custom:unit:1"))
        return 1;
    if (!LiteCrabPromptBuildBase(&request, output, 4))
        return 1;
    LiteCrabPromptResetProvider();
    if (LiteCrabPromptBuildBase(&request, output, sizeof output) == 0)
        return 1; /* Generated diagnostic prompt is intentionally larger than this buffer. */
    return 0;
}
