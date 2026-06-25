#include <stdio.h>

void f(int x) {
    printf("%d\n", x);
}

int main() {
    int a, b, c, sum, x, y;

    a = 1;
    a = 2;

    b = 5;
    f(b);
    b = 10;
    f(b);

    c = 1;
    c = 2;
    c = 3;
    c = 4;

    if (a > 0) {
        x = 100;
        x = 200;
        c = 5;
        f(x);
    } 
    else {
        x = -1;
        f(x);
    }

    sum = 0;
    for (int i = 0; i < 5; i++) {
        sum = sum + i;
    }
    f(sum);

    for (int i = 0; i < 3; i++) {
        y = i;
        y = i * 2;
        f(y);
    }

    return 0;
}