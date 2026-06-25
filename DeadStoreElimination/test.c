#include <stdio.h>

void f(int x) {
    printf("%d\n", x);
}

/* Funkcija g() - primer za ugnežđene if-ove */
int g(int x) {
    int y = 0;                    // LIVE (inicijalizacija)

    if (x > 0) {
        y = 5;                    // DEAD STORE → treba da se eliminiše
                                  // (prepisuje se kasnije sa y = 7)

        if (x > 10) {
            x++;                  // LIVE (uticaj na kontrolu toka, ali ne na y)
        }

        y = 7;                    // LIVE STORE → ostaje
                                  // (koristi se u return y)
    }

    return y;                     // koristi poslednju vrednost y (7)
}

int main() {
    int a, b, c, sum, x, y;

    /* === Varijabla a === */
    a = 1;                        // DEAD STORE → eliminiše se
    a = 2;                        // LIVE STORE → ostaje (koristi se u if (a > 0))

    /* === Varijabla b === */
    b = 5;                        // DEAD STORE → eliminiše se
    f(b);                         // poziv koristi prethodnu vrednost b=5? NE - vidi redosled
    b = 10;                       // LIVE STORE → ostaje
    f(b);                         // koristi b=10

    /* === Varijabla c === */
    c = 1;                        // DEAD STORE → eliminiše se
    c = 2;                        // DEAD STORE → eliminiše se
    c = 3;                        // DEAD STORE → eliminiše se
    c = 4;                        // DEAD STORE → eliminiše se

    if (a > 0) {
        x = 100;                  // DEAD STORE → eliminiše se
        x = 200;                  // LIVE STORE → ostaje (prosleđuje se u f(x))
        c = 5;                    // DEAD STORE → eliminiše se (nema dalje upotrebe c)
        f(x);
    } 
    else {
        x = -1;                   // LIVE STORE → ostaje
        f(x);
    }

    /* === Varijabla sum === */
    sum = 0;                      // LIVE STORE → ostaje (početna vrednost za sabiranje)

    for (int i = 0; i < 5; i++) {
        sum = sum + i;            // LIVE STORE → ostaje (akumulira vrednost)
    }
    f(sum);                       // koristi konačnu vrednost sum

    /* === Varijabla y u petlji === */
    for (int i = 0; i < 3; i++) {
        y = i;                    // DEAD STORE → eliminiše se
        y = i * 2;                // LIVE STORE → ostaje (prosleđuje se u f(y))
        f(y);
    }

    /* === Pozivi funkcije g() - test za multi-BB dead store */
    int res1 = g(5);              // unutar g() će se eliminisati y=5
    f(res1);

    return 0;
}