int printf(const char *fmt, ...);

int main(void) {
    int x = 42;
    int *p = &x;
    printf("%d\n", *p);     /* 42 */
    *p = 100;
    printf("%d\n", x);      /* 100 */

    int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    printf("%d %d %d\n", arr[0], arr[1], arr[2]);

    return 0;
}
