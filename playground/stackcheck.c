#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline uintptr_t read_sp(void) {
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    return sp;
}

static bool check_pattern(const volatile uint8_t *buf, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != v) return false;
    }
    return true;
}

static void fill_pattern(volatile uint8_t *buf, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++) buf[i] = v;
}

static bool deep_stack(int depth, uintptr_t *min_sp) {
    volatile uint8_t canary[256];
    fill_pattern(canary, sizeof(canary), (uint8_t)(0xA5u ^ (uint8_t)depth));

    uintptr_t sp = read_sp();
    if (sp < *min_sp) *min_sp = sp;

    bool ok = true;
    if (depth > 0) ok = deep_stack(depth - 1, min_sp);

    uint8_t expect = (uint8_t)(0xA5u ^ (uint8_t)depth);
    if (!check_pattern(canary, sizeof(canary), expect)) ok = false;

    return ok;
}

int main(int argc, char **argv) {
    uintptr_t sp0 = read_sp();
    uintptr_t min_sp = sp0;
    int depth = 64;

    if (argc > 1) {
        int v = 0;
        if (sscanf(argv[1], "%d", &v) == 1 && v >= 1 && v <= 2048) depth = v;
    }

    printf("stackcheck: argc=%d argv0=%s\n", argc, argc > 0 ? argv[0] : "(null)");
    printf("stackcheck: sp0=0x%016" PRIxPTR " aligned16=%s depth=%d\n",
           sp0, (sp0 & 0xF) == 0 ? "yes" : "no", depth);

    bool ok = deep_stack(depth, &min_sp);
    printf("stackcheck: min_sp=0x%016" PRIxPTR " used=%" PRIuPTR " ok=%s\n",
           min_sp, (uintptr_t)(sp0 - min_sp), ok ? "yes" : "no");

    volatile uint8_t big[4096];
    fill_pattern(big, sizeof(big), 0x3C);
    printf("stackcheck: bigbuf ok=%s\n",
           check_pattern(big, sizeof(big), 0x3C) ? "yes" : "no");

    puts("stackcheck: done");
    return ok ? 0 : 1;
}

