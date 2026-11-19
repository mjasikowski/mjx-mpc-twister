#ifndef AARCH64_HOOK_H
#define AARCH64_HOOK_H
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    uint8_t *site;
    void *trampoline;
    uint32_t original[4];
    size_t page_size;
} a64_hook;

static inline void a64_jump(uint8_t *output,uintptr_t address) {
    const uint32_t words[]={0x58000051,0xd61f0220}; /* ldr x17,+8; br x17 */
    memcpy(output,words,sizeof words);
    memcpy(output+8,&address,sizeof address);
}

/* Relocate the four verified prologue instructions. The captured constructor
 * and destructors contain ADRP; their other instructions are position independent. */
static inline size_t a64_relocate(uint8_t *output,const uint32_t words[4],uintptr_t pc) {
    size_t used=0;
    for(unsigned i=0;i<4;i++) {
        uint32_t word=words[i];
        if((word&0x9f000000)==0x90000000) {
            int64_t immediate=((word>>5)&0x7ffff)<<2 | ((word>>29)&3);
            if(immediate&(1<<20))immediate-=1<<21;
            uintptr_t target=((pc+i*4)&~(uintptr_t)4095)+immediate*4096;
            unsigned reg=word&31;
            for(unsigned part=0;part<4;part++) {
                uint32_t move=(part ? 0xf2800000 : 0xd2800000)|(part<<21)|
                    (((target>>(part*16))&0xffff)<<5)|reg;
                memcpy(output+used,&move,4);used+=4;
            }
        } else {
            if((word&0x9f000000)==0x10000000 || (word&0x7c000000)==0x14000000 ||
               (word&0xff000000)==0x54000000 || (word&0x7e000000)==0x34000000 ||
               (word&0x7e000000)==0x36000000 || (word&0x3b000000)==0x18000000) {
                errno=ENOTSUP;return 0;
            }
            memcpy(output+used,&word,4);used+=4;
        }
    }
    return used;
}

static inline int a64_protect(const a64_hook *hook,int protection) {
    uintptr_t begin=(uintptr_t)hook->site&~(hook->page_size-1);
    uintptr_t end=((uintptr_t)hook->site+16+hook->page_size-1)&~(hook->page_size-1);
    return mprotect((void*)begin,end-begin,protection);
}

static inline int a64_install(a64_hook *hook,void *site,const uint32_t expected[4],void *replacement) {
    if(memcmp(site,expected,16)){errno=ENOTSUP;return -1;}
    hook->site=site;hook->page_size=(size_t)sysconf(_SC_PAGESIZE);
    memcpy(hook->original,site,16);
    hook->trampoline=mmap(NULL,hook->page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(hook->trampoline==MAP_FAILED){hook->trampoline=NULL;return -1;}
    size_t used=a64_relocate(hook->trampoline,expected,(uintptr_t)site);
    if(!used)goto fail;
    a64_jump((uint8_t*)hook->trampoline+used,(uintptr_t)site+16);
    __builtin___clear_cache(hook->trampoline,(char*)hook->trampoline+used+16);
    if(mprotect(hook->trampoline,hook->page_size,PROT_READ|PROT_EXEC) ||
       a64_protect(hook,PROT_READ|PROT_WRITE|PROT_EXEC))goto fail;
    uint8_t jump[16];a64_jump(jump,(uintptr_t)replacement);
    memcpy(site,jump,16);__builtin___clear_cache(site,(char*)site+16);
    if(a64_protect(hook,PROT_READ|PROT_EXEC)) {
        int error=errno;
        memcpy(site,expected,16);__builtin___clear_cache(site,(char*)site+16);
        (void)a64_protect(hook,PROT_READ|PROT_EXEC);
        errno=error;goto fail;
    }
    return 0;
fail:
    {int error=errno;munmap(hook->trampoline,hook->page_size);hook->trampoline=NULL;errno=error;return -1;}
}

/* Installation/removal is used before MPC's main starts, never while a native
 * thread can be executing the modified entry points. */
static inline int a64_remove(a64_hook *hook) {
    if(!hook->trampoline)return 0;
    if(a64_protect(hook,PROT_READ|PROT_WRITE|PROT_EXEC))return -1;
    memcpy(hook->site,hook->original,16);
    __builtin___clear_cache((char*)hook->site,(char*)hook->site+16);
    if(a64_protect(hook,PROT_READ|PROT_EXEC))return -1;
    munmap(hook->trampoline,hook->page_size);hook->trampoline=NULL;
    return 0;
}
#endif
