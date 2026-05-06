/* Test array initializer storage, indexed loads/stores, and element scaling. */
int main(void) {
    int nums[4] = {3, 5, 7, 11};
    char bytes[4] = {1, 2, 3, 4};

    if (nums[0] != 3) return 1;
    if (nums[1] != 5) return 2;
    if (nums[2] != 7) return 3;
    if (nums[3] != 11) return 4;

    nums[2] = nums[0] + nums[3];
    if (nums[2] != 14) return 5;

    if (bytes[0] != 1) return 6;
    if (bytes[1] != 2) return 7;
    if (bytes[2] != 3) return 8;
    if (bytes[3] != 4) return 9;
    return 0;
}
