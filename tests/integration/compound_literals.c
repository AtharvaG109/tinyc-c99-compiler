/* Test compound literals as automatic objects and struct assignment. */
struct Pair {
    int x;
    int y;
};

struct Box {
    struct Pair p;
    int z;
};

int main(void) {
    struct Pair a = (struct Pair){3, 4};
    struct Pair b;
    struct Pair *p = &(struct Pair){10, 20};
    int scalar = (int){7};

    if (a.x != 3) return 1;
    if (a.y != 4) return 2;
    if (p->x != 10) return 3;
    if (p->y != 20) return 4;
    if (scalar != 7) return 5;

    b = (struct Pair){.y = 9, .x = 8};
    if (b.x != 8) return 6;
    if (b.y != 9) return 7;

    a = b;
    if (a.x != 8) return 8;
    if (a.y != 9) return 9;

    if (((struct Pair){11, 12}).y != 12) return 10;

    struct Box box = (struct Box){{1, 2}, 3};
    if (box.p.x != 1) return 11;
    if (box.p.y != 2) return 12;
    if (box.z != 3) return 13;

    return 0;
}
