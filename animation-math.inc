static double AnimationTime(double elapsed, double duration, int mode, bool show) {
    if (elapsed <= 0) return 0;
    if (elapsed >= duration || mode == 0) return elapsed;
    double t = elapsed / duration;
    double p = show ? 1.0 - t : t;
    double shaped;
    switch (mode) {
        case 1: shaped = p * p; break;
        case 2: shaped = p * p * p; break;
        case 3: shaped = p * p * p * p; break;
        case 4: return duration * t * t * (3.0 - 2.0 * t);
        default: return elapsed;
    }
    return duration * (show ? 1.0 - shaped : shaped);
}
