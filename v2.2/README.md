# zmmalloc

*Zé Matias malloc* — alocador de memória dinâmica com **classes de tamanho
segregadas** e **metadados autenticados (MAC)** para Linux x86-64.

## Características

- Arena única via `mmap` (128 KiB), iniciada por uma estrutura de controle.
- Blocos por classe de tamanho (64 / 256 / 1024 / 4096 bytes) com listas de
  reciclagem separadas.
- *Boundary tags* (header + footer), cada um com um MAC (SipHash-2-4) dos
  metadados. A chave é estabelecida em tempo de execução.
- `zm_free` deriva a classe a partir da geometria real do bloco.
- `zm_dump()` mostra os blocos e as listas por classe.

## API

```c
void   zm_init(void);
void   zm_destroy(void);
void  *zm_malloc(size_t n);
void   zm_free(void *p);
void   zm_dump(void);
void  *zm_heap_base(void);
```

## Build

```sh
make
./demo
```

## Licença

Uso livre para fins educacionais.
