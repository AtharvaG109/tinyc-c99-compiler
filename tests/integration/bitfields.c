struct Flags {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 6;
    int x;
};

int main(void) {
    struct Flags f = {0};
    f.a = 5;
    f.b = 17;
    f.c = 33;
    f.x = 7;

    if (f.a != 5) return 1;
    if (f.b != 17) return 2;
    if (f.c != 33) return 3;
    if (f.x != 7) return 4;
    if (sizeof(struct Flags) != 8) return 5;
    return 0;
}
