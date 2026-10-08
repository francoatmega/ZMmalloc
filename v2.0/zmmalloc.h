/* zmmalloc — Zé Matias malloc.
 *
 * Alocador de memória dinâmica compacto para Linux x86-64.
 * Arena única obtida via mmap, boundary tags (header + footer) por bloco,
 * free list com first-fit, splitting no malloc e coalescing no free.
 *
 * Layout de um bloco:
 *
 *     [ zm_header_t | dados (size bytes) | zm_footer_t ]
 *
 * API com prefixo zm_.
 */
#ifndef ZMMALLOC_H
#define ZMMALLOC_H

#include <stddef.h>
#include <stdint.h>

#define ZM_ALIGN 16

typedef struct zm_header {
    size_t            size;      /* bytes de dados neste bloco */
    uint32_t          is_free;   /* 1 = livre, 0 = em uso */
    uint32_t          tag;       /* tag de integridade (4 bytes) */
    struct zm_header *next_free; /* free list (válido quando livre) */
} zm_header_t;

typedef struct zm_footer {
    size_t   size;
    uint32_t is_free;
    uint32_t tag;
} zm_footer_t;

#define ZM_HDR_SIZE (((sizeof(zm_header_t) + ZM_ALIGN - 1) / ZM_ALIGN) * ZM_ALIGN)
#define ZM_FTR_SIZE (((sizeof(zm_footer_t) + ZM_ALIGN - 1) / ZM_ALIGN) * ZM_ALIGN)

void   zm_init(void);
void   zm_destroy(void);
void  *zm_malloc(size_t n);
void   zm_free(void *p);
void   zm_dump(void);

#endif /* ZMMALLOC_H */
