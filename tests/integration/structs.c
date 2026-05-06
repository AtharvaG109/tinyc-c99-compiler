/* Test local structs, member writes/reads, and pointer member access. */
struct Inner {
    int z;
};

struct Point {
    int x;
    char tag;
    int y;
    struct Inner inner;
};

int main(void) {
    struct Point p = {10, 7, 20, {30}};
    struct Point *pp = &p;

    if (p.x != 10) return 1;
    if (p.tag != 7) return 2;
    if (p.y != 20) return 3;
    if (p.inner.z != 30) return 4;

    p.x = p.x + 5;
    pp->y = pp->x + p.inner.z;
    pp->inner.z = pp->y - 3;
    pp->tag = 9;

    if (p.x != 15) return 5;
    if (p.y != 45) return 6;
    if (p.inner.z != 42) return 7;
    if (pp->tag != 9) return 8;
    return 0;
}
