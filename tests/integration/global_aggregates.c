/* Test global aggregate initializer emission. */
struct Pair {
    int x;
    int y;
};

struct Box {
    char tag;
    int values[3];
    struct Pair pair;
};

union U {
    int i;
    char c;
};

int nums[4] = {3, 5, 7, 11};
char bytes[4] = {1, 2, 3, 4};
struct Pair p = {.y = 9, .x = 4};
struct Box box = {6, {10, 20, 30}, {40, 50}};
union U u = {123};
static int hidden[3] = {[1] = 8, [2] = 13};

int main(void) {
    if (nums[0] != 3) return 1;
    if (nums[1] != 5) return 2;
    if (nums[2] != 7) return 3;
    if (nums[3] != 11) return 4;

    if (bytes[0] != 1) return 5;
    if (bytes[1] != 2) return 6;
    if (bytes[2] != 3) return 7;
    if (bytes[3] != 4) return 8;

    if (p.x != 4) return 9;
    if (p.y != 9) return 10;
    if (box.tag != 6) return 11;
    if (box.values[0] != 10) return 12;
    if (box.values[1] != 20) return 13;
    if (box.values[2] != 30) return 14;
    if (box.pair.x != 40) return 15;
    if (box.pair.y != 50) return 16;
    if (u.i != 123) return 17;
    if (hidden[0] != 0) return 18;
    if (hidden[1] != 8) return 19;
    if (hidden[2] != 13) return 20;
    return 0;
}
