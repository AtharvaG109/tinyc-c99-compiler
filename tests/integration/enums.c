/* Test enum constants through sema and IR generation. */
enum Color {
    RED = 3,
    GREEN,
    BLUE = 9
};

int classify(enum Color c) {
    switch (c) {
    case RED: return 10;
    case GREEN: return 20;
    case BLUE: return 30;
    default: return -1;
    }
}

int main(void) {
    if (RED != 3) return 1;
    if (GREEN != 4) return 2;
    if (BLUE != 9) return 3;
    if (classify(GREEN) != 20) return 4;
    return 0;
}
