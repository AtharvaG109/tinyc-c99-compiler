int printf(const char *fmt, ...);

double add(double a, double b) {
    return a + b;
}

double twice(double x) {
    return x * 2.0;
}

int main(void) {
    double a = 3.14;
    double b = 2.0;
    double c = add(a, b);
    double d = twice(c) / 2.0;
    int as_int = (int)d;

    if (c <= 5.0) return 1;
    if (c >= 6.0) return 2;
    if (d <= 5.0) return 3;
    if (as_int != 5) return 4;

    printf("Success: %f\n", c);
    return 0;
}
