// Shared notification generation; no polling or periodic keep-alive timer.
namespace fluid {
std::atomic<unsigned long long> displayGeneration{0};
}
