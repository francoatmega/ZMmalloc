/* zmmalloc — Zé Matias malloc. Demo de uso. */
#include "zmmalloc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    zm_init();
    printf("--- inicio ---\n");
    zm_dump();

    printf("\n--- aloca a=100, b=200, c=50 ---\n");
    char *a = zm_malloc(100);
    char *b = zm_malloc(200);
    char *c = zm_malloc(50);
    strcpy(a, "buffer A");
    strcpy(b, "buffer B");
    strcpy(c, "C");
    zm_dump();

    printf("\n--- libera b ---\n");
    zm_free(b);
    zm_dump();

    printf("\n--- aloca d=80 ---\n");
    char *d = zm_malloc(80);
    (void)d;
    zm_dump();

    printf("\n--- libera tudo (coalescing) ---\n");
    zm_free(a);
    zm_free(c);
    zm_free(d);
    zm_dump();

    zm_destroy();
    return 0;
}
