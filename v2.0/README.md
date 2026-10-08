# zmmalloc

*Zé Matias malloc* — um alocador de memória dinâmica compacto para Linux
x86-64, pensado para estudo e para embutir em projetos pequenos onde se quer
um heap próprio, previsível e fácil de inspecionar.

## Características

- Arena única obtida do sistema com `mmap` (128 KiB por padrão).
- *Boundary tags*: cada bloco carrega um header no início e um footer no fim,
  o que permite coalescing dos dois lados no `free`.
- Estratégia *first-fit* com *splitting* no `malloc`.
- Coalescing de blocos livres adjacentes no `free`.
- Tag de integridade nos metadados de cada bloco.
- `zm_dump()` para inspecionar o estado da arena.

## API

```c
void   zm_init(void);            /* prepara a arena (idempotente) */
void   zm_destroy(void);         /* devolve a arena ao sistema */
void  *zm_malloc(size_t n);      /* aloca >= n bytes */
void   zm_free(void *p);         /* libera um ponteiro do zm_malloc */
void   zm_dump(void);            /* imprime o estado da arena */
```

## Build

```sh
make            # gera libzmmalloc.so e o demo
./demo
```

## Licença

Uso livre para fins educacionais.
