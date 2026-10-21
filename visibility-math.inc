static bool CoversMonitor(long left, long top, long right, long bottom,
                          long monitorLeft, long monitorTop,
                          long monitorRight, long monitorBottom) {
    return left <= monitorLeft && top <= monitorTop &&
           right >= monitorRight && bottom >= monitorBottom;
}

static bool UseAdaptivePin(bool enabled, int slot, const bool (&pins)[4]) {
    return enabled && slot >= 0 && slot < 4 && pins[slot];
}
