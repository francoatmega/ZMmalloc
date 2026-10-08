/* zmmalloc — Zé Matias malloc. Implementação. */
#include "zmmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <time.h>

#define ZM_ARENA_SIZE (128 * 1024)
#define ZM_MAGIC      0x7A6D3231u  /* "zm21" */

const size_t zm_class_size[ZM_NUM_CLASSES] = { 64, 256, 1024, 4096 };

static char         *zm_arena = NULL;
static zm_control_t *zm_ctrl  = NULL;
static int           zm_ready = 0;

static int zm_class_for(size_t n) {
    for (int i = 0; i < ZM_NUM_CLASSES; i++)
        if (n <= zm_class_size[i]) return i;
    return -1;
}

static uint64_t zm_check(uint64_t key, uint32_t size_class, uint32_t is_free, uint64_t size) {
    uint64_t x = key;
    x ^= (uint64_t)size_class * 0x9E3779B97F4A7C15ULL;
    x ^= (uint64_t)is_free    * 0xC2B2AE3D27D4EB4FULL;
    x ^= (uint64_t)size       * 0x165667B19E3779F9ULL;
    return x;
}

static void *zm_user_ptr(zm_header_t *h) { return (char *)h + ZM_HDR_SIZE; }
static zm_header_t *zm_hdr_of(void *p)   { return (zm_header_t *)((char *)p - ZM_HDR_SIZE); }

static zm_footer_t *zm_footer_of(zm_header_t *h) {
    return (zm_footer_t *)((char *)h + ZM_HDR_SIZE + h->size);
}

static void zm_set_tags(zm_header_t *h) {
    uint64_t c = zm_check(zm_ctrl->key, h->size_class, h->is_free, h->size);
    h->check = c;
    zm_footer_t *f = zm_footer_of(h);
    f->check      = c;
    f->size_class = h->size_class;
    f->is_free    = h->is_free;
    f->size       = h->size;
}

static void zm_verify_header(zm_header_t *h, const char *ctx) {
    if (h->check != zm_check(zm_ctrl->key, h->size_class, h->is_free, h->size)) {
        fprintf(stderr, "zmmalloc: integridade invalida em %s (header %p)\n",
                ctx, (void *)h);
        abort();
    }
}

static void zm_verify_footer(zm_footer_t *f, const char *ctx) {
    if (f->check != zm_check(zm_ctrl->key, f->size_class, f->is_free, f->size)) {
        fprintf(stderr, "zmmalloc: integridade invalida em %s (footer %p)\n",
                ctx, (void *)f);
        abort();
    }
}

static void zm_bin_push(int cls, zm_header_t *h) {
    h->next_free = zm_ctrl->bins[cls];
    zm_ctrl->bins[cls] = h;
}

static zm_header_t *zm_bin_pop(int cls) {
    zm_header_t *h = zm_ctrl->bins[cls];
    if (h) zm_ctrl->bins[cls] = h->next_free;
    return h;
}

static uint64_t zm_gen_key(void) {
    uint64_t k = 0;
    if (getrandom(&k, sizeof(k), 0) != (ssize_t)sizeof(k)) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        k = (uint64_t)ts.tv_nsec * 0x100000001B3ULL + (uint64_t)ts.tv_sec;
    }
    return k;
}

void zm_init(void) {
    if (zm_ready) return;
    void *base = mmap(NULL, ZM_ARENA_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { perror("zmmalloc: mmap"); abort(); }

    zm_arena = (char *)base;
    zm_ctrl  = (zm_control_t *)base;
    zm_ctrl->key      = zm_gen_key();
    zm_ctrl->magic    = ZM_MAGIC;
    zm_ctrl->nclasses = ZM_NUM_CLASSES;
    zm_ctrl->top      = zm_arena + ZM_CTRL_SIZE;
    zm_ctrl->end      = zm_arena + ZM_ARENA_SIZE;
    for (int i = 0; i < ZM_NUM_CLASSES; i++) zm_ctrl->bins[i] = NULL;

    zm_ready = 1;
}

void zm_destroy(void) {
    if (!zm_ready) return;
    munmap(zm_arena, ZM_ARENA_SIZE);
    zm_arena = NULL;
    zm_ctrl  = NULL;
    zm_ready = 0;
}

void *zm_malloc(size_t n) {
    if (!zm_ready) zm_init();
    if (n == 0) return NULL;

    int cls = zm_class_for(n);
    if (cls < 0) return NULL;

    zm_header_t *h = zm_bin_pop(cls);
    if (!h) {
        size_t stride = ZM_HDR_SIZE + zm_class_size[cls] + ZM_FTR_SIZE;
        if (zm_ctrl->top + stride > zm_ctrl->end) return NULL;
        h = (zm_header_t *)zm_ctrl->top;
        zm_ctrl->top += stride;
    }
    h->size_class = (uint32_t)cls;
    h->size       = zm_class_size[cls];
    h->is_free    = 0;
    h->next_free  = NULL;
    zm_set_tags(h);
    return zm_user_ptr(h);
}

void zm_free(void *p) {
    if (!p) return;
    zm_header_t *h = zm_hdr_of(p);

    zm_verify_header(h, "zm_free");
    zm_verify_footer(zm_footer_of(h), "zm_free/footer");

    int cls = (int)h->size_class;   /* classe vem do metadado do bloco */
    h->is_free = 1;
    zm_set_tags(h);
    zm_bin_push(cls, h);
}

void zm_dump(void) {
    printf("=== zmmalloc arena (base=%p, %d bytes) ===\n",
           (void *)zm_arena, ZM_ARENA_SIZE);
    printf("    HDR=%zu FTR=%zu CTRL=%zu magic=0x%08x\n",
           ZM_HDR_SIZE, ZM_FTR_SIZE, ZM_CTRL_SIZE, zm_ctrl->magic);

    char *cursor = zm_arena + ZM_CTRL_SIZE;
    int idx = 0;
    while (cursor < zm_ctrl->top) {
        zm_header_t *h = (zm_header_t *)cursor;
        if (h->size == 0 || h->size > ZM_ARENA_SIZE) {
            printf("  [%2d] off=%6ld size=%zu (parando)\n",
                   idx, (long)(cursor - zm_arena), h->size);
            break;
        }
        zm_footer_t *f = zm_footer_of(h);
        int ho = (h->check == zm_check(zm_ctrl->key, h->size_class, h->is_free, h->size));
        int fo = (f->check == zm_check(zm_ctrl->key, f->size_class, f->is_free, f->size));
        printf("  [%2d] off=%6ld size=%6zu class=%u %-6s hdr=%s ftr=%s\n",
               idx++, (long)(cursor - zm_arena), h->size, h->size_class,
               h->is_free ? "LIVRE" : "em-uso",
               ho ? "ok" : "RUIM", fo ? "ok" : "RUIM");
        cursor += ZM_HDR_SIZE + h->size + ZM_FTR_SIZE;
    }
    for (int i = 0; i < ZM_NUM_CLASSES; i++) {
        printf("  bin[%d] (size=%zu): ", i, zm_class_size[i]);
        for (zm_header_t *fl = zm_ctrl->bins[i]; fl; fl = fl->next_free)
            printf("off=%ld -> ", (long)((char *)fl - zm_arena));
        printf("NULL\n");
    }
}
