/* Test global variable read/write */
int counter = 0;
int base = 100;

int increment(void) {
    counter = counter + 1;
    return counter;
}

int main(void) {
    int a = increment();
    int b = increment();
    int c = increment();

    if (a != 1) return 1;
    if (b != 2) return 2;
    if (c != 3) return 3;
    if (counter != 3) return 4;
    if (base != 100) return 5;

    base = base + counter;
    if (base != 103) return 6;

    return 0;
}
