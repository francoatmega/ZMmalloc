/* zmmalloc — Zé Matias malloc. Implementação. */
#include "zmmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define ZM_ARENA_SIZE (128 * 1024)
#define ZM_TAG_KEY    0x5A4D5F30u  /* constante de mistura da tag */

static char        *zm_arena     = NULL;
static char        *zm_arena_end = NULL;
static zm_header_t *zm_free_list = NULL;
static int          zm_ready     = 0;

static size_t zm_align_up(size_t x) {
    return (x + (ZM_ALIGN - 1)) & ~((size_t)(ZM_ALIGN - 1));
}

static uint32_t zm_tag(size_t size, uint32_t is_free) {
    return (uint32_t)((uint32_t)size ^ is_free ^ ZM_TAG_KEY);
}

static void *zm_user_ptr(zm_header_t *h) { return (char *)h + ZM_HDR_SIZE; }
static zm_header_t *zm_hdr_of(void *p)   { return (zm_header_t *)((char *)p - ZM_HDR_SIZE); }

static zm_footer_t *zm_footer_of(zm_header_t *h) {
    return (zm_footer_t *)((char *)h + ZM_HDR_SIZE + h->size);
}

static zm_header_t *zm_next_chunk(zm_header_t *h) {
    char *p = (char *)h + ZM_HDR_SIZE + h->size + ZM_FTR_SIZE;
    if (p >= zm_arena_end) return NULL;
    return (zm_header_t *)p;
}

static zm_footer_t *zm_prev_footer(zm_header_t *h) {
    if ((char *)h <= zm_arena) return NULL;
    return (zm_footer_t *)((char *)h - ZM_FTR_SIZE);
}

static zm_header_t *zm_prev_chunk(zm_header_t *h, zm_footer_t *pf) {
    return (zm_header_t *)((char *)h - ZM_FTR_SIZE - pf->size - ZM_HDR_SIZE);
}

static void zm_set_tags(zm_header_t *h) {
    uint32_t t = zm_tag(h->size, h->is_free);
    h->tag = t;
    zm_footer_t *f = zm_footer_of(h);
    f->size = h->size;
    f->is_free = h->is_free;
    f->tag = t;
}

static void zm_verify_header(zm_header_t *h, const char *ctx) {
    if (h->tag != zm_tag(h->size, h->is_free)) {
        fprintf(stderr, "zmmalloc: integridade invalida em %s (header %p)\n",
                ctx, (void *)h);
        abort();
    }
}

static void zm_verify_footer(zm_footer_t *f, const char *ctx) {
    if (f->tag != zm_tag(f->size, f->is_free)) {
        fprintf(stderr, "zmmalloc: integridade invalida em %s (footer %p)\n",
                ctx, (void *)f);
        abort();
    }
}

static void zm_flist_push(zm_header_t *h) {
    h->next_free = zm_free_list;
    zm_free_list = h;
}

static void zm_flist_remove(zm_header_t *target) {
    zm_header_t *prev = NULL, *cur = zm_free_list;
    while (cur) {
        if (cur == target) {
            if (prev) prev->next_free = cur->next_free;
            else      zm_free_list    = cur->next_free;
            cur->next_free = NULL;
            return;
        }
        prev = cur;
        cur  = cur->next_free;
    }
}

void zm_init(void) {
    if (zm_ready) return;
    void *base = mmap(NULL, ZM_ARENA_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { perror("zmmalloc: mmap"); abort(); }

    zm_arena     = (char *)base;
    zm_arena_end = zm_arena + ZM_ARENA_SIZE;

    zm_header_t *h = (zm_header_t *)zm_arena;
    h->size      = ZM_ARENA_SIZE - ZM_HDR_SIZE - ZM_FTR_SIZE;
    h->is_free   = 1;
    h->next_free = NULL;
    zm_set_tags(h);

    zm_free_list = h;
    zm_ready     = 1;
}

void zm_destroy(void) {
    if (!zm_ready) return;
    munmap(zm_arena, ZM_ARENA_SIZE);
    zm_arena = zm_arena_end = NULL;
    zm_free_list = NULL;
    zm_ready = 0;
}

void *zm_malloc(size_t n) {
    if (!zm_ready) zm_init();
    if (n == 0) return NULL;
    n = zm_align_up(n);

    zm_header_t *prev = NULL, *cur = zm_free_list;
    while (cur) {
        if (cur->is_free && cur->size >= n) {
            if (cur->size >= n + ZM_FTR_SIZE + ZM_HDR_SIZE + ZM_ALIGN) {
                zm_header_t *rest =
                    (zm_header_t *)((char *)cur + ZM_HDR_SIZE + n + ZM_FTR_SIZE);
                rest->size      = cur->size - n - ZM_FTR_SIZE - ZM_HDR_SIZE;
                rest->is_free   = 1;
                rest->next_free = cur->next_free;
                zm_set_tags(rest);

                cur->size = n;
                if (prev) prev->next_free = rest;
                else      zm_free_list    = rest;
            } else {
                if (prev) prev->next_free = cur->next_free;
                else      zm_free_list    = cur->next_free;
            }
            cur->is_free   = 0;
            cur->next_free = NULL;
            zm_set_tags(cur);
            return zm_user_ptr(cur);
        }
        prev = cur;
        cur  = cur->next_free;
    }
    return NULL;
}

void zm_free(void *p) {
    if (!p) return;
    zm_header_t *h = zm_hdr_of(p);
    zm_verify_header(h, "zm_free");
    h->is_free = 1;

    zm_header_t *next = zm_next_chunk(h);
    if (next) {
        zm_verify_header(next, "zm_free/next");
        if (next->is_free) {
            zm_flist_remove(next);
            h->size += ZM_HDR_SIZE + next->size + ZM_FTR_SIZE;
        }
    }

    zm_footer_t *pf = zm_prev_footer(h);
    if (pf) {
        zm_verify_footer(pf, "zm_free/prev");
        if (pf->is_free) {
            zm_header_t *prev = zm_prev_chunk(h, pf);
            zm_flist_remove(prev);
            prev->size += ZM_HDR_SIZE + h->size + ZM_FTR_SIZE;
            h = prev;
        }
    }

    zm_set_tags(h);
    zm_flist_push(h);
}

void zm_dump(void) {
    printf("=== zmmalloc arena (base=%p, %d bytes) ===\n",
           (void *)zm_arena, ZM_ARENA_SIZE);
    printf("    HDR=%zu FTR=%zu\n", ZM_HDR_SIZE, ZM_FTR_SIZE);
    char *cursor = zm_arena;
    int idx = 0;
    while (cursor < zm_arena_end) {
        zm_header_t *h = (zm_header_t *)cursor;
        if (h->size == 0 || h->size > ZM_ARENA_SIZE) {
            printf("  [%2d] off=%6ld  size=%zu (parando)\n",
                   idx, (long)(cursor - zm_arena), h->size);
            break;
        }
        zm_footer_t *f = zm_footer_of(h);
        int ho = (h->tag == zm_tag(h->size, h->is_free));
        int fo = (f->tag == zm_tag(f->size, f->is_free));
        printf("  [%2d] off=%6ld size=%6zu %-6s hdr=%s ftr=%s\n",
               idx++, (long)(cursor - zm_arena), h->size,
               h->is_free ? "LIVRE" : "em-uso",
               ho ? "ok" : "RUIM", fo ? "ok" : "RUIM");
        cursor += ZM_HDR_SIZE + h->size + ZM_FTR_SIZE;
    }
    printf("  free_list: ");
    for (zm_header_t *fl = zm_free_list; fl; fl = fl->next_free)
        printf("off=%ld -> ", (long)((char *)fl - zm_arena));
    printf("NULL\n");
}
