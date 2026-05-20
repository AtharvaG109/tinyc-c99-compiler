int main(void) {
    unsigned long max = (unsigned long)-1;
    if (!(max > 100ul)) return 1;
    if (!(1ul < max)) return 2;
    if (0ul > max) return 3;
    return 0;
}
