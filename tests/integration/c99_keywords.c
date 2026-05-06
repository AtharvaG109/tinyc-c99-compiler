_Pragma("STDC FENV_ACCESS OFF")

int main(void) {
    _Complex double z = 4.0;
    double _Imaginary i = 2.0;

    _Pragma("STDC CX_LIMITED_RANGE ON")
    if (sizeof(_Complex double) != 8) return 1;
    if (sizeof(double _Imaginary) != 8) return 2;
    if ((int)z != 4) return 3;
    if ((int)i != 2) return 4;
    return 0;
}
