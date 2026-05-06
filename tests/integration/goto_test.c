/* Test goto and named labels */
int main(void) {
    int x = 0;
    goto skip;
    x = 999;   /* must not execute */
skip:
    x = x + 1;
    if (x != 1) return 1;

    /* Forward and backward goto */
    int i = 0;
loop:
    i = i + 1;
    if (i < 3) goto loop;
    if (i != 3) return 2;

    return 0;
}
