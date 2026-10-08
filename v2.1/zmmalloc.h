/* zmmalloc — Zé Matias malloc.
 *
 * Alocador de memória dinâmica com classes de tamanho segregadas, para Linux
 * x86-64. A arena é obtida via mmap e começa com uma pequena estrutura de
 * controle, seguida pela região de blocos.
 *
 * Cada bloco pertence a uma classe de tamanho; blocos livres são reciclados
 * em listas separadas por classe. Metadados por bloco ficam em boundary tags
 * (header + footer) e carregam uma tag de integridade calculada com uma chave
 * inicializada em tempo de execução.
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

/* Tamanhos (bytes de dados) de cada classe. */
extern const size_t zm_class_size[ZM_NUM_CLASSES];

/* Header no início do bloco. Ordem dos campos própria desta série. */
typedef struct zm_header {
    uint64_t          check;      /* tag de integridade (8 bytes) */
    uint32_t          size_class; /* índice da classe deste bloco */
    uint32_t          is_free;    /* 1 = livre, 0 = em uso */
    size_t            size;       /* bytes de dados (= zm_class_size[classe]) */
    struct zm_header *next_free;  /* lista da classe (válido quando livre) */
} zm_header_t;

typedef struct zm_footer {
    uint64_t check;
    uint32_t size_class;
    uint32_t is_free;
    size_t   size;
} zm_footer_t;

/* Estrutura de controle, no início da arena. */
typedef struct zm_control {
    uint64_t     key;                    /* chave da tag, definida no init */
    uint32_t     magic;                  /* identificação do formato */
    uint32_t     nclasses;
    char        *top;                    /* próximo endereço livre */
    char        *end;                    /* fim da arena */
    zm_header_t *bins[ZM_NUM_CLASSES];   /* listas de reciclagem por classe */
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
