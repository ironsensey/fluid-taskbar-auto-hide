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
        case 5: return duration * std::clamp(
            t * t * t * (t * (6.0 * t - 15.0) + 10.0), 0.0, 1.0);
        default: return elapsed;
    }
    return duration * (show ? 1.0 - shaped : shaped);
}

// Use absolute frame deadlines so render/scheduler delays do not accumulate.
// A late frame yields instead of adding another millisecond of forced latency.
static unsigned FrameSleepMilliseconds(double now, double deadline) {
    return static_cast<unsigned>(std::clamp(std::ceil((deadline - now) * 1000.0),
                                             0.0, 34.0));
}

static double AdvanceFrameDeadline(double deadline, double now, double period) {
    double next = deadline + period;
    // Do not issue a burst of catch-up frames after a stalled Explorer thread.
    return next > now ? next : now + period;
}
