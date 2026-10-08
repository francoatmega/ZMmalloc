/* zmmalloc — Zé Matias malloc. Demo de uso. */
#include "zmmalloc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    zm_init();
    printf("--- inicio ---\n");
    zm_dump();

    printf("\n--- aloca a=48, b=200, c=48 ---\n");
    char *a = zm_malloc(48);
    char *b = zm_malloc(200);
    char *c = zm_malloc(48);
    strcpy(a, "buffer A");
    strcpy(b, "buffer B");
    strcpy(c, "C");
    zm_dump();

    printf("\n--- libera b e realoca 200 (reciclagem por classe) ---\n");
    zm_free(b);
    char *b2 = zm_malloc(200);
    printf("b=%p  b2=%p (mesma classe reciclada)\n", (void *)b, (void *)b2);
    zm_dump();

    printf("\n--- libera tudo ---\n");
    zm_free(a);
    zm_free(c);
    zm_free(b2);
    zm_dump();

    zm_destroy();
    return 0;
}
