/* Test sizeof constants and ensure sizeof expression side effects do not run. */
struct S {
    char c;
    int i;
};

union U {
    char c;
    int i;
};

int main(void) {
    int x = 0;
    int arr[3];
    struct S s;
    struct S *sp = &s;

    if (sizeof(char) != 1) return 1;
    if (sizeof(int) != 4) return 2;
    if (sizeof(long) != 8) return 3;
    if (sizeof(&x) != 8) return 4;
    if (sizeof(arr) != 12) return 5;
    if (sizeof(struct S) != 8) return 6;
    if (sizeof(union U) != 4) return 7;
    if (sizeof(s.c) != 1) return 8;
    if (sizeof(sp->i) != 4) return 9;
    if (sizeof(x++) != 4) return 10;
    if (x != 0) return 11;
    return 0;
}
