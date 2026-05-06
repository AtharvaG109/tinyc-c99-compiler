/* Test simple local array and struct designator initializers. */
struct Pair {
    int x;
    int y;
    int z;
};

int main(void) {
    int nums[4] = {[2] = 7, [0] = 3, [3] = 11};
    struct Pair p = {.z = 9, .x = 4, .y = 6};

    if (nums[0] != 3) return 1;
    if (nums[1] != 0) return 2;
    if (nums[2] != 7) return 3;
    if (nums[3] != 11) return 4;

    if (p.x != 4) return 5;
    if (p.y != 6) return 6;
    if (p.z != 9) return 7;
    return 0;
}
