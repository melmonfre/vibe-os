int abs(int value) {
    return value < 0 ? -value : value;
}

double fmod(double x, double y) {
    long long q;

    if (y == 0.0) {
        return 0.0;
    }
    q = (long long)(x / y);
    return x - ((double)q * y);
}
