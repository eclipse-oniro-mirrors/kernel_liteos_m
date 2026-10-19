#include "litecrab/prompt.h"

#include <pthread.h>
#include <string.h>

extern const unsigned char litecrab_generated_product_prompt[];
extern const size_t litecrab_generated_product_prompt_size;

static pthread_mutex_t providerMutex = PTHREAD_MUTEX_INITIALIZER;
static LiteCrabPromptProvider activeProvider;

int LiteCrabPromptSetProvider(const LiteCrabPromptProvider* provider) {
    if (!provider || !provider->build)
        return -1;
    pthread_mutex_lock(&providerMutex);
    activeProvider = *provider;
    pthread_mutex_unlock(&providerMutex);
    return 0;
}

void LiteCrabPromptResetProvider(void) {
    pthread_mutex_lock(&providerMutex);
    memset(&activeProvider, 0, sizeof activeProvider);
    pthread_mutex_unlock(&providerMutex);
}

int LiteCrabPromptBuildBase(const LiteCrabPromptRequest* request, char* output, size_t outputSize) {
    if (!output || !outputSize)
        return -1;
    pthread_mutex_lock(&providerMutex);
    LiteCrabPromptProvider provider = activeProvider;
    pthread_mutex_unlock(&providerMutex);
    if (provider.build)
        return provider.build(provider.context, request, output, outputSize);
    if (litecrab_generated_product_prompt_size + 1 > outputSize)
        return -1;
    memcpy(output, litecrab_generated_product_prompt, litecrab_generated_product_prompt_size + 1);
    return 0;
}
