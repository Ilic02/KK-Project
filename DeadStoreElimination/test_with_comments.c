#include <stdio.h>

void f(int x) {
    printf("%d\n", x);
}

int main() {
    int a, b, c, sum, x, y;

    /* === INTRA-BB Dead Stores (unutar istog BasicBlock-a) === */

    a = 1;          // DEAD STORE - odmah se overwrituje
    a = 2;          // LIVE - koristi se u if uslovu (a > 0)

    b = 5;
    f(b);           // koristi vrednost 5
    b = 10;         // LIVE - koristi se u sledećem pozivu
    f(b);

    c = 1;          // DEAD STORE
    c = 2;          // DEAD STORE
    c = 3;          // DEAD STORE
    c = 4;          // DEAD STORE 

    /* === Inter-BB Dead Store (glavni slučaj za maks poene) === */
    if (a > 0) {
        x = 100;    // DEAD STORE 
        x = 200;    // LIVE
        c = 5;      // KILL STORE - ubija prethodni c = 4
        f(x);
    } 
    else {
        x = -1;     // LIVE
        f(x);
        // Nema store-a u c → ali nema ni load-a od c nakon if-a
    }

    /* c = 4 je mrtav jer:
       - Na then putanji ga ubija c = 5
       - Na else putanji se ne koristi nigde posle
       - Nema load-a od c između c=4 i kraja if-a */

    sum = 0;        // LIVE - početna vrednost za akumulaciju
    for (int i = 0; i < 5; i++) {
        sum = sum + i;   // LIVE - akumulira vrednost
    }
    f(sum);

    for (int i = 0; i < 3; i++) {
        y = i;           // DEAD STORE 
        y = i * 2;       // LIVE
        f(y);
    }

    return 0;
}