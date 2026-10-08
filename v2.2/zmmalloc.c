/* zmmalloc — Zé Matias malloc. Implementação. */
#include "zmmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <time.h>

#define ZM_ARENA_SIZE (128 * 1024)
#define ZM_MAGIC      0x7A6D3232u  /* "zm22" */

const size_t zm_class_size[ZM_NUM_CLASSES] = { 64, 256, 1024, 4096 };

static char         *zm_arena = NULL;
static zm_control_t *zm_ctrl  = NULL;
static int           zm_ready = 0;

/* Chave do MAC: fica em armazenamento estático do processo, não na arena.
 * É definida no init a partir de uma fonte de entropia do sistema. */
static uint8_t zm_key[16];

static int zm_class_for(size_t n) {
    for (int i = 0; i < ZM_NUM_CLASSES; i++)
        if (n <= zm_class_size[i]) return i;
    return -1;
}

/* ---- SipHash-2-4 (MAC de 64 bits) ---- */

static uint64_t zm_rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

static uint64_t zm_load64(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, sizeof(v)); return v;
}

static uint64_t zm_siphash(const uint8_t key[16], const uint8_t *in, size_t inlen) {
    uint64_t k0 = zm_load64(key), k1 = zm_load64(key + 8);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

#define ZM_ROUND() do {                       \
    v0 += v1; v1 = zm_rotl(v1,13); v1 ^= v0; v0 = zm_rotl(v0,32); \
    v2 += v3; v3 = zm_rotl(v3,16); v3 ^= v2;                      \
    v0 += v3; v3 = zm_rotl(v3,21); v3 ^= v0;                      \
    v2 += v1; v1 = zm_rotl(v1,17); v1 ^= v2; v2 = zm_rotl(v2,32); \
} while (0)

    size_t i = 0;
    uint64_t b = (uint64_t)inlen << 56;
    for (; i + 8 <= inlen; i += 8) {
        uint64_t m = zm_load64(in + i);
        v3 ^= m; ZM_ROUND(); ZM_ROUND(); v0 ^= m;
    }
    uint64_t last = 0;
    for (size_t j = 0; i + j < inlen; j++)
        last |= (uint64_t)in[i + j] << (8 * j);
    b |= last;
    v3 ^= b; ZM_ROUND(); ZM_ROUND(); v0 ^= b;
    v2 ^= 0xff;
    ZM_ROUND(); ZM_ROUND(); ZM_ROUND(); ZM_ROUND();
#undef ZM_ROUND
    return v0 ^ v1 ^ v2 ^ v3;
}

/* MAC dos campos de metadado (size, size_class, is_free). */
static uint64_t zm_mac(size_t size, uint32_t size_class, uint32_t is_free) {
    uint8_t buf[16];
    memcpy(buf + 0, &size, 8);
    memcpy(buf + 8, &size_class, 4);
    memcpy(buf + 12, &is_free, 4);
    return zm_siphash(zm_key, buf, sizeof(buf));
}

static void *zm_user_ptr(zm_header_t *h) { return (char *)h + ZM_HDR_SIZE; }
static zm_header_t *zm_hdr_of(void *p)   { return (zm_header_t *)((char *)p - ZM_HDR_SIZE); }

static zm_footer_t *zm_footer_of(zm_header_t *h) {
    return (zm_footer_t *)((char *)h + ZM_HDR_SIZE + h->size);
}

static void zm_set_tags(zm_header_t *h) {
    uint64_t m = zm_mac(h->size, h->size_class, h->is_free);
    h->mac = m;
    zm_footer_t *f = zm_footer_of(h);
    f->mac        = m;
    f->size       = h->size;
    f->size_class = h->size_class;
    f->is_free    = h->is_free;
}

static void zm_verify_header(zm_header_t *h, const char *ctx) {
    if (h->mac != zm_mac(h->size, h->size_class, h->is_free)) {
        fprintf(stderr, "zmmalloc: MAC invalido em %s (header %p)\n", ctx, (void *)h);
        abort();
    }
}

static void zm_verify_footer(zm_footer_t *f, const char *ctx) {
    if (f->mac != zm_mac(f->size, f->size_class, f->is_free)) {
        fprintf(stderr, "zmmalloc: MAC invalido em %s (footer %p)\n", ctx, (void *)f);
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

void zm_init(void) {
    if (zm_ready) return;
    void *base = mmap(NULL, ZM_ARENA_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { perror("zmmalloc: mmap"); abort(); }

    if (getrandom(zm_key, sizeof(zm_key), 0) != (ssize_t)sizeof(zm_key)) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t a = (uint64_t)ts.tv_nsec * 0x100000001B3ULL + (uint64_t)ts.tv_sec;
        uint64_t b = a * 0x9E3779B97F4A7C15ULL;
        memcpy(zm_key + 0, &a, 8);
        memcpy(zm_key + 8, &b, 8);
    }

    zm_arena = (char *)base;
    zm_ctrl  = (zm_control_t *)base;
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
    memset(zm_key, 0, sizeof(zm_key));
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
    h->size       = zm_class_size[cls];
    h->size_class = (uint32_t)cls;
    h->is_free    = 0;
    h->next_free  = NULL;
    zm_set_tags(h);
    return zm_user_ptr(h);
}

void zm_free(void *p) {
    if (!p) return;
    zm_header_t *h = zm_hdr_of(p);

    /* Autentica header e footer antes de confiar em qualquer campo. */
    zm_verify_header(h, "zm_free");
    zm_verify_footer(zm_footer_of(h), "zm_free/footer");

    /* A classe é derivada da geometria real (tamanho), não lida do campo
     * inline. Assim o campo size_class não decide nada crítico. */
    int cls = zm_class_for(h->size);
    if (cls < 0 || h->size != zm_class_size[cls]) {
        fprintf(stderr, "zmmalloc: tamanho inconsistente em zm_free (%p)\n", (void *)h);
        abort();
    }

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
        int ho = (h->mac == zm_mac(h->size, h->size_class, h->is_free));
        int fo = (f->mac == zm_mac(f->size, f->size_class, f->is_free));
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
