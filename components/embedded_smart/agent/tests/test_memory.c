#include "litecrab/memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char tmp[]="/tmp/litecrab-memory-XXXXXX";
    assert(mkdtemp(tmp));
    MemoryConfig c; MemoryConfigDefaults(&c);
    char out[4097];
    assert(!MemoryInit(&c,tmp));
    assert(!MemoryIsEnabled());
    assert(MemoryRemember("alice","fact","alpha","agent")==2);
    c.enabled=1;
    assert(!MemoryInit(&c,tmp));
    assert(MemoryIsEnabled());
    assert(MemoryBuildContext("alice",out,sizeof out)==1);
    assert(MemoryRemember("alice","preference","Use Chinese","agent")==0);
    assert(MemoryRemember("alice","preference","Use Chinese","agent")==1);
    assert(MemoryRemember("bob","fact","Bob fact","agent")==0);
    assert(MemoryBuildContext("alice",out,sizeof out)==0);
    assert(strstr(out,"Use Chinese") && !strstr(out,"Bob fact"));
    assert(MemoryBuildContext("bob",out,sizeof out)==0);
    assert(strstr(out,"Bob fact") && !strstr(out,"Use Chinese"));
    assert(MemoryRemember("alice","note","Recent item","agent")==0);
    assert(MemoryRemember("alice","note","Recent item","agent")==1);
    assert(MemoryBuildContext("alice",out,sizeof out)==0 && strstr(out,"Recent item"));
    assert(MemoryRemember("alice","bad","bad","agent")==-1);
    assert(MemoryRemember("alice","fact","password=secret","agent")==-1);
    assert(MemoryRemember("","fact","bad","agent")==-1);
    assert(MemoryRemember("alice","fact","line\nbreak","agent")==-1);
    c.recentMaxBytes=4096;
    assert(!MemoryInit(&c,tmp));
    for (int i=0;i<40;i++) {
        char item[450];
        memset(item,'x',sizeof item);
        snprintf(item,16,"event-%03d ",i);
        item[10]='x';
        item[sizeof item-1]=0;
        assert(MemoryRemember("alice","note",item,"agent")==0);
    }
    assert(MemoryBuildContext("alice",out,sizeof out)==0);
    assert(strstr(out,"event-039") && !strstr(out,"event-000"));
    assert(strlen(out)<=4096);
    MemoryShutdown();
    assert(!MemoryInit(&c,tmp));
    assert(MemoryBuildContext("alice",out,sizeof out)==0 && strstr(out,"Use Chinese"));
    MemoryShutdown();
    snprintf(c.storeDir,sizeof c.storeDir,"../escape");
    assert(MemoryInit(&c,tmp)==-1);
    puts("memory tests passed");
    return 0;
}
