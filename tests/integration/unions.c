/* Test union layout and shared member storage. */
union Value {
    int i;
    int j;
};

int main(void) {
    union Value v;
    union Value w = {17};

    v.i = 42;
    if (v.j != 42) return 1;

    v.j = 99;
    if (v.i != 99) return 2;

    if (w.i != 17) return 3;
    if (w.j != 17) return 4;
    return 0;
}
