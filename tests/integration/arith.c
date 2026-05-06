int printf(const char *fmt, ...);

int main(void) {
    int a = 6;
    int b = 4;
    printf("%d\n", a + b);   /* 10 */
    printf("%d\n", a - b);   /* 2  */
    printf("%d\n", a * b);   /* 24 */
    printf("%d\n", a / b);   /* 1  */
    printf("%d\n", a % b);   /* 2  */
    return 0;
}
