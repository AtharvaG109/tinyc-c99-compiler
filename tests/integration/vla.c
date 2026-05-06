int sum_n(int n) {
    int values[n];
    int i = 0;
    int total = 0;
    while (i < n) {
        values[i] = i + 1;
        total = total + values[i];
        i = i + 1;
    }
    return total;
}

int main(void) {
    if (sum_n(4) != 10) return 1;
    if (sum_n(7) != 28) return 2;
    return 0;
}
