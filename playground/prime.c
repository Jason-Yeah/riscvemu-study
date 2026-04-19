#include <stdio.h>
#include <stdbool.h>
#include <time.h>

bool is_prime(int n) {
    if (n < 2) return false;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) return false;
    }
    return true;
}

int main() {
    int count = 0;
    int limit = 10000000;

    printf("Calculating primes up to %d...\n", limit);

    for (int i = 1; i <= limit; i++) {
        if (is_prime(i)) {
            count++;
        }
    }

    printf("Found %d primes.\n", count);
    return 0;
}