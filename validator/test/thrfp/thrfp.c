#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef int (*ptr)(int*);    /* ptr is pointer to function that takes pointer to int and returns int */
typedef ptr (*pm)();         /* pm is a pointer to a function that returns type ptr */

pm globpm;                   /* global variable holds pointer to a function that returns a function pointer */

int worldfun(int* y) {
  printf("world: ");
  return *y + 10;
}
 
ptr hellofun(void) {
  printf("Hello ");
  return &worldfun;
}

void thread_helper(void) {
  static int x = 10;
  int (*(*p[1])())(int*) = { globpm };
  /* p is an array of function pointers of size 1 */
  /* p[0] is globpm, which points to hellofun */
  /* invoke hellofun -- prints "Hello " and returns pointer to worldfun */
  /* invoke worldfun with pointer to x -- prints "world: " followed by (x + 10) */
  printf("%d\n", (*p[0]())(&x)); 
}

void *thread1_fun( void *ptr ) {
  thread_helper();
  return NULL;
}

int main(int argc, char * argv[]) {
  int       iret1;
  pthread_t thread1;
  globpm = hellofun;
  iret1 = pthread_create( &thread1, NULL, thread1_fun, (void*) NULL);
  pthread_join( thread1, NULL);
  exit(0);
}

/* main directly invokes pthread_create and pthread_join */
/* main had data dependency to thread1_fun, globpm, and hellofun */

/* thread1_fun is invoked via a pointer by an unknown external function */
/* this hides control dependency from main to other functions in this file */

/* thread1_fun has control dependency to thread_helper */
/* thread_helper invokes hellofun and worldfun via pointers */
/* thread_helper has data dependency to globpm */

/* globpm has data dependency to hellofun */
/* hellofun has data dependency to worldfun */

/* 3 external functions */
/* 5 internal functions */
/* 1 global variable */
/* 1 function-scoped static variable */