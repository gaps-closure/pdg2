#include <stdio.h>

typedef struct clothes {
    char color[10];
    int length;
} Clothes;

typedef struct person_t {
    int age;
    char name[10];
    Clothes s;
} Person;

void f(Clothes *c1) {
    printf("clothes color %s.", c1->color);
}

int main() {
    Clothes c = {"red", 5};
    Person p = {10, "Jack", c};
    Person *pt = &p;
    f(&pt->s);
    return 0;
}
