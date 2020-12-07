#include <stdio.h>
struct st {
    int f1[10];
    int *f2 ;
};

void test1(struct st* ss) {
   printf("%d\n", ss->f1[0]); 
}

// array<type, var_len> a; 
void test2(int* s, int len) {
    printf("%d\n", s[1]);
    /* for (int i = 0; i < len; ++i) */ 
    /*     printf("%d\n", s[i]); */
}

//driver
/* void test3(int *a) { */
/*     int a[2]; */
/*     ... */
/*     test2(a, 2); */
/* } */

/* void test4() { */
/*     int a[3]; */
/*     ... */
/*     test2(a, 3); */
/* } */
