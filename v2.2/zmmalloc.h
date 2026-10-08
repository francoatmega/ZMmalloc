/* zmmalloc — Zé Matias malloc.
 *
 * Alocador de memória dinâmica com classes de tamanho segregadas para Linux
 * x86-64. A integridade dos metadados é protegida por um código de
 * autenticação (MAC) por bloco, com chave estabelecida em tempo de execução.
 *
 * Layout de um bloco:
 *
 *     [ zm_header_t | dados | zm_footer_t ]
 */
#ifndef ZMMALLOC_H
#define ZMMALLOC_H

#include <stddef.h>
#include <stdint.h>

#define ZM_ALIGN       16
#define ZM_NUM_CLASSES 4

extern const size_t zm_class_size[ZM_NUM_CLASSES];

typedef struct zm_header {
    uint64_t          mac;        /* MAC dos metadados (8 bytes) */
    size_t            size;       /* bytes de dados */
    uint32_t          size_class; /* classe (informativo; não confiável p/ decisões) */
    uint32_t          is_free;
    struct zm_header *next_free;
} zm_header_t;

typedef struct zm_footer {
    uint64_t mac;
    size_t   size;
    uint32_t size_class;
    uint32_t is_free;
} zm_footer_t;

typedef struct zm_control {
    uint32_t     magic;
    uint32_t     nclasses;
    char        *top;
    char        *end;
    zm_header_t *bins[ZM_NUM_CLASSES];
} zm_control_t;

#define ZM_HDR_SIZE  (((sizeof(zm_header_t)  + ZM_ALIGN - 1) / ZM_ALIGN) * ZM_ALIGN)
#define ZM_FTR_SIZE  (((sizeof(zm_footer_t)  + ZM_ALIGN - 1) / ZM_ALIGN) * ZM_ALIGN)
#define ZM_CTRL_SIZE (((sizeof(zm_control_t) + ZM_ALIGN - 1) / ZM_ALIGN) * ZM_ALIGN)

void   zm_init(void);
void   zm_destroy(void);
void  *zm_malloc(size_t n);
void   zm_free(void *p);
void   zm_dump(void);

#endif /* ZMMALLOC_H */
