int printf(const char *format, ...);
void exit(int status);

void check(int cond, int line) {
    if (!cond) {
        printf("Assertion failed on line %d\n", line);
        exit(1);
    }
}

/* Test 1: small struct (fits in 2 registers) */
typedef struct { int x; int y; } Point;
Point make_point(int x, int y) { return (Point){x, y}; }
int sum_point(Point p) { return p.x + p.y; }

/* Test 2: struct exactly 8 bytes (fits in 1 register) */
typedef struct { int a; short b; short c; } Small;
Small make_small(void) { return (Small){1, 2, 3}; }

/* Test 3: large struct (must go through memory) */
typedef struct { long a[9]; } Big;  /* 72 bytes > 64 */
Big make_big(long val) { Big b; for(int i=0;i<9;i++) b.a[i]=val+i; return b; }

/* Test 4: mixed float+int struct */
typedef struct { int n; double d; } Mixed;
Mixed make_mixed(void) { return (Mixed){42, 3.14}; }

/* Test 5: nested struct */
typedef struct { Point p; int z; } Point3;
Point3 make3(void) { return (Point3){{1,2},3}; }

int main(void) {
    printf("Test 1 starting...\n");
    Point p = make_point(3, 4);
    check(sum_point(p) == 7, 31);
    printf("Test 1 passed\n");

    printf("Test 2 starting...\n");
    Small s = make_small();
    check(s.a == 1 && s.b == 2 && s.c == 3, 36);
    printf("Test 2 passed\n");

    printf("Test 3 starting...\n");
    Big b = make_big(10);
    check(b.a[0] == 10 && b.a[8] == 18, 41);
    printf("Test 3 passed\n");

    printf("Test 4 starting...\n");
    Mixed m = make_mixed();
    check(m.n == 42, 46);
    printf("Test 4 passed\n");

    printf("Test 5 starting...\n");
    Point3 q = make3();
    check(q.p.x == 1 && q.p.y == 2 && q.z == 3, 51);
    printf("Test 5 passed\n");

    printf("ALL ABI TESTS PASSED\n");
    return 0;
}
