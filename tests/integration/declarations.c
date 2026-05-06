/* Test typedefs, function pointers, storage classes, qualifiers, and variadic calls. */
int printf(const char *fmt, ...);

typedef int MyInt;
typedef int (*Unary)(int);

static int global_counter = 5;
extern int printf(const char *fmt, ...);

int add_one(int x) {
    return x + 1;
}

static int add_two(int x) {
    return x + 2;
}

int call_it(int (*fn)(int), int value) {
    return fn(value);
}

int main(void) {
    const MyInt base = 10;
    volatile int local = base;
    int (*fn)(int) = add_one;
    Unary alias_fn = add_two;

    if (fn(4) != 5) return 1;
    if (alias_fn(4) != 6) return 5;
    fn = add_two;
    if (fn(4) != 6) return 2;
    if (call_it(add_one, 7) != 8) return 3;

    global_counter = global_counter + local;
    if (global_counter != 15) return 4;

    printf("");
    return 0;
}
