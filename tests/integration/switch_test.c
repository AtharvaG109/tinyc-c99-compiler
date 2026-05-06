/* Test switch statements */
int puts(const char *s);
int printf(const char *fmt, ...);

int classify(int n) {
    switch (n) {
    case 1: return 10;
    case 2: return 20;
    case 3: return 30;
    default: return -1;
    }
}

int main(void) {
    int a = classify(1);
    int b = classify(2);
    int c = classify(3);
    int d = classify(99);

    /* Verify results via return code arithmetic: 10+20+30-1=59, we check each */
    if (a != 10) return 1;
    if (b != 20) return 2;
    if (c != 30) return 3;
    if (d != -1) return 4;
    return 0;
}
