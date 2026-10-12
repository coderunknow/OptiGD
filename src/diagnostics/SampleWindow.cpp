#include "SampleWindow.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace optigd::diagnostics {

const char* percentileMethodName(PercentileMethod method) {
    switch (method) {
        case PercentileMethod::None: return "none";
        case PercentileMethod::Exact: return "exact";
        case PercentileMethod::HistogramApproximate: return "histogram-approximate";
        default: return "unknown";
    }
}

namespace {

struct MetricDescriptor {
    const char* name;
    MetricAggregation aggregation;
};

// One table, indexed by WindowMetric. Keep in the same order as the enum.
constexpr MetricDescriptor kMetricDescriptors[] = {
    {"update_cpu_us",           MetricAggregation::Mean},
    {"update_thread_cpu_us",    MetricAggregation::Mean},
    {"optigd_overhead_us",      MetricAggregation::Mean},
    {"culling_us",              MetricAggregation::Mean},
    {"particle_us",             MetricAggregation::Mean},
    {"trigger_us",              MetricAggregation::Mean},
    {"matrix_us",               MetricAggregation::Mean},
    {"governor_us",             MetricAggregation::Mean},
    {"culled_objects",          MetricAggregation::Sum},
    {"particles_avoided",       MetricAggregation::Sum},
    {"triggers_skipped",        MetricAggregation::Sum},
    {"matrix_cache_hits",       MetricAggregation::Sum},
    {"matrix_cache_misses",     MetricAggregation::Sum},
    {"arena_allocations",       MetricAggregation::Sum},
    {"heap_allocations_avoided", MetricAggregation::Sum},
    {"arena_peak_bytes",        MetricAggregation::Max},
    {"total_objects",           MetricAggregation::Max},
    {"active_particles",        MetricAggregation::Max},
    {"visible_objects",         MetricAggregation::Max},
};

static_assert(sizeof(kMetricDescriptors) / sizeof(kMetricDescriptors[0]) == kWindowMetricCount,
              "kMetricDescriptors must have exactly one entry per WindowMetric");

// Extracts the per-frame value for `metric` from a FrameStats record.
// Returning 0 for a field that was never populated is deliberate: it keeps the
// accumulator honest (a count of 0 samples) instead of inventing a value.
double extractMetric(WindowMetric metric, const core::FrameStats& s) {
    switch (metric) {
        case WindowMetric::UpdateCpuUs:        return s.updateCpuTimeUs;
        case WindowMetric::UpdateThreadCpuUs:  return s.updateThreadCpuUs;
        case WindowMetric::OptigdOverheadUs:   return s.optigdOverheadUs;
        case WindowMetric::CullingUs:          return s.cullingTimeUs;
        case WindowMetric::ParticleUs:         return s.particleTimeUs;
        case WindowMetric::TriggerUs:          return s.triggerTimeUs;
        case WindowMetric::MatrixUs:           return s.matrixTimeUs;
        case WindowMetric::GovernorUs:         return s.governorTimeUs;
        case WindowMetric::CulledObjects:      return static_cast<double>(s.culledObjects);
        case WindowMetric::ParticlesAvoided:   return static_cast<double>(s.culledParticles);
        case WindowMetric::TriggersSkipped:
            // Mirrors DiagnosticsEngine's beta.2 definition and only counts a
            // positive difference, so a saturating subtraction can never wrap.
            return s.totalTriggers >= s.evaluatedTriggers
                       ? static_cast<double>(s.totalTriggers - s.evaluatedTriggers)
                       : 0.0;
        case WindowMetric::MatrixCacheHits:    return static_cast<double>(s.matrixCacheHits);
        case WindowMetric::MatrixCacheMisses:  return static_cast<double>(s.matrixCacheMisses);
        case WindowMetric::ArenaAllocations:   return static_cast<double>(s.arenaAllocations);
        case WindowMetric::HeapAllocationsAvoided:
            return static_cast<double>(s.heapAllocationsAvoided);
        case WindowMetric::ArenaPeakBytes:     return static_cast<double>(s.arenaPeakBytes);
        case WindowMetric::TotalObjects:       return static_cast<double>(s.totalObjects);
        case WindowMetric::ActiveParticles:    return static_cast<double>(s.activeParticles);
        case WindowMetric::VisibleObjects:     return static_cast<double>(s.visibleObjects);
        default: return 0.0;
    }
}

} // namespace

const char* windowMetricName(WindowMetric metric) {
    size_t index = static_cast<size_t>(metric);
    if (index >= kWindowMetricCount) return "unknown";
    return kMetricDescriptors[index].name;
}

MetricAggregation windowMetricAggregation(WindowMetric metric) {
    size_t index = static_cast<size_t>(metric);
    if (index >= kWindowMetricCount) return MetricAggregation::Mean;
    return kMetricDescriptors[index].aggregation;
}

// ---------------------------------------------------------------------------
// MetricAccumulator
// ---------------------------------------------------------------------------

void MetricAccumulator::reset() {
    count = 0;
    sum = 0.0;
    min = std::numeric_limits<double>::infinity();
    max = -std::numeric_limits<double>::infinity();
}

void MetricAccumulator::add(double value) {
    if (!std::isfinite(value)) return;
    ++count;
    sum += value;
    if (value < min) min = value;
    if (value > max) max = value;
}

double MetricAccumulator::mean() const {
    return count > 0 ? (sum / static_cast<double>(count)) : 0.0;
}

double MetricAccumulator::safeMin() const {
    return (count > 0 && std::isfinite(min)) ? min : 0.0;
}

double MetricAccumulator::safeMax() const {
    return (count > 0 && std::isfinite(max)) ? max : 0.0;
}

double MetricAccumulator::aggregate(WindowMetric metric) const {
    switch (windowMetricAggregation(metric)) {
        case MetricAggregation::Sum: return std::isfinite(sum) ? sum : 0.0;
        case MetricAggregation::Max: return safeMax();
        case MetricAggregation::Mean:
        default: return mean();
    }
}

// ---------------------------------------------------------------------------
// SampleWindow
// ---------------------------------------------------------------------------

void SampleWindow::configure(const SampleWindowConfig& config) {
    m_config = config;
    if (!std::isfinite(m_config.histogramMaxMs) || m_config.histogramMaxMs <= 0.0) {
        m_config.histogramMaxMs = 250.0;
    }
    if (m_config.histogramBins < 2) m_config.histogramBins = 2;
    if (m_config.histogramBins > (1u << 20)) m_config.histogramBins = (1u << 20);
    if (!std::isfinite(m_config.hitchThresholdMs) || m_config.hitchThresholdMs <= 0.0) {
        m_config.hitchThresholdMs = 1000.0 / 30.0;
    }
    if (!std::isfinite(m_config.stallThresholdMs) || m_config.stallThresholdMs <= 0.0) {
        m_config.stallThresholdMs = 250.0;
    }

    m_config.ringCapacity = std::min<size_t>(m_config.ringCapacity, 4u << 20);
    m_ring.assign(m_config.ringCapacity, 0.0f);
    m_ringWritePos = 0;
    m_sortScratch.assign(m_config.ringCapacity, 0.0);

    m_histogramBinWidthMs = m_config.histogramMaxMs / static_cast<double>(m_config.histogramBins);
    // +1 bin: the final bin collects everything >= histogramMaxMs.
    m_histogram.assign(m_config.histogramBins + 1, 0u);

    reset();
}

bool SampleWindow::isStallIntervalUs(double intervalUs) const {
    if (!std::isfinite(intervalUs) || intervalUs <= 0.0) return false;
    return (intervalUs / 1000.0) > m_config.stallThresholdMs;
}

void SampleWindow::reset() {
    m_ringWritePos = 0;
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    std::fill(m_histogram.begin(), m_histogram.end(), 0u);
    m_histogramSaturatedSamples = 0;

    m_samples = 0;
    m_rejectedSamples = 0;
    m_excludedSamples = 0;
    m_overflowSamples = 0;
    m_hitchFrames = 0;
    m_stallFrames = 0;
    m_excludedTimeMs = 0.0;
    m_longestExcludedMs = 0.0;
    m_hitchTotalMs = 0.0;

    m_sumFrameTimeMs = 0.0;
    m_sumFrameTimeSqMs = 0.0;
    m_minFrameTimeMs = std::numeric_limits<double>::infinity();
    m_maxFrameTimeMs = -std::numeric_limits<double>::infinity();

    for (auto& metric : m_metrics) metric.reset();
}

size_t SampleWindow::histogramBinFor(double frameTimeMs) {
    if (!(frameTimeMs >= 0.0)) return m_histogram.empty() ? 0 : m_histogram.size() - 1;
    if (m_histogramBinWidthMs <= 0.0) return m_histogram.empty() ? 0 : m_histogram.size() - 1;
    double index = std::floor(frameTimeMs / m_histogramBinWidthMs);
    if (!(index >= 0.0)) index = 0.0;
    size_t bin = static_cast<size_t>(index);
    if (bin >= m_histogram.size()) {
        bin = m_histogram.size() - 1; // overflow bin
        ++m_histogramSaturatedSamples;
    }
    return bin;
}

void SampleWindow::recordExcluded(double frameTimeUs) {
    ++m_excludedSamples;
    if (std::isfinite(frameTimeUs) && frameTimeUs > 0.0) {
        const double ms = frameTimeUs / 1000.0;
        m_excludedTimeMs += ms;
        if (ms > m_longestExcludedMs) m_longestExcludedMs = ms;
    }
}

void SampleWindow::pushFrameTime(double frameTimeUs) {
    core::FrameStats stats{};
    stats.totalFrameTimeUs = frameTimeUs;
    push(stats);
}

void SampleWindow::push(const core::FrameStats& stats) {
    double frameTimeUs = stats.totalFrameTimeUs;
    if (!std::isfinite(frameTimeUs) || frameTimeUs <= 0.0) {
        ++m_rejectedSamples;
        return;
    }
    pushAccepted(frameTimeUs / 1000.0, &stats);
}

void SampleWindow::pushAccepted(double frameTimeMs, const core::FrameStats* stats) {
    ++m_samples;
    m_sumFrameTimeMs += frameTimeMs;
    m_sumFrameTimeSqMs += frameTimeMs * frameTimeMs;
    if (frameTimeMs < m_minFrameTimeMs) m_minFrameTimeMs = frameTimeMs;
    if (frameTimeMs > m_maxFrameTimeMs) m_maxFrameTimeMs = frameTimeMs;

    // Histogram: always updated, so percentiles remain available (approximate)
    // even after the ring has overflowed.
    if (!m_histogram.empty()) {
        size_t bin = histogramBinFor(frameTimeMs);
        if (bin < m_histogram.size() && m_histogram[bin] < 0xFFFFFFFFu) {
            ++m_histogram[bin];
        }
    }

    // Hitches stay in the distribution but are also counted, so a report can
    // show stutter without re-deriving it from the percentiles.
    if (frameTimeMs > m_config.hitchThresholdMs) {
        ++m_hitchFrames;
        m_hitchTotalMs += frameTimeMs;
    }
    if (frameTimeMs > m_config.stallThresholdMs) {
        ++m_stallFrames;
    }

    // Ring: bounded. On overflow the oldest exact sample is lost and the loss is
    // *counted*, which is what flips the reported percentile method from
    // `exact` to `histogram-approximate`.
    if (m_config.ringCapacity > 0 && !m_ring.empty()) {
        if (m_samples > m_config.ringCapacity) {
            ++m_overflowSamples;
        }
        m_ring[m_ringWritePos] = static_cast<float>(frameTimeMs);
        m_ringWritePos = (m_ringWritePos + 1) % m_ring.size();
    }

    if (stats != nullptr) {
        for (size_t i = 0; i < kWindowMetricCount; ++i) {
            m_metrics[i].add(extractMetric(static_cast<WindowMetric>(i), *stats));
        }
    } else {
        // Frame-time-only sample: keep the accumulator counts aligned with the
        // sample count so `mean()` cannot divide by a different n.
        m_metrics[static_cast<size_t>(WindowMetric::UpdateCpuUs)].add(0.0);
    }
}

double SampleWindow::medianFromSorted(const double* sorted, size_t n) {
    if (n == 0 || sorted == nullptr) return 0.0;
    if (n % 2 == 0) {
        return 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
    }
    return sorted[n / 2];
}

double SampleWindow::percentileFromSorted(const double* sorted, size_t n, double p) {
    if (n == 0 || sorted == nullptr) return 0.0;
    if (!std::isfinite(p)) return 0.0;
    if (p <= 0.0) return sorted[0];
    if (p >= 1.0) return sorted[n - 1];

    // Nearest-rank: rank = ceil(p * n), index = rank - 1, clamped.
    double rankExact = p * static_cast<double>(n);
    unsigned long long rank = static_cast<unsigned long long>(std::ceil(rankExact));
    if (rank < 1) rank = 1;
    if (rank > n) rank = n;
    return sorted[rank - 1];
}

WindowStatistics SampleWindow::computeStatistics(StatPrecision precision) const {
    WindowStatistics out{};
    out.samples = m_samples;
    out.rejectedSamples = m_rejectedSamples;
    out.excludedSamples = m_excludedSamples;
    out.overflowSamples = m_overflowSamples;
    out.excludedTimeMs = m_excludedTimeMs;
    out.ringCapacity = m_config.ringCapacity;
    out.ringUsed = std::min<uint64_t>(m_samples, m_config.ringCapacity);
    out.histogramMaxMs = m_config.histogramMaxMs;
    out.histogramBinWidthMs = m_histogramBinWidthMs;
    out.hitchThresholdMs = m_config.hitchThresholdMs;
    out.stallThresholdMs = m_config.stallThresholdMs;
    out.hitchFrames = m_hitchFrames;
    out.hitchTotalMs = m_hitchTotalMs;
    out.stallFrames = m_stallFrames;
    out.longestExcludedMs = m_longestExcludedMs;
    out.ringOverflow = m_overflowSamples > 0;
    out.histogramSaturated = m_histogramSaturatedSamples > 0;
    out.metrics = m_metrics;

    if (m_samples == 0) {
        out.percentileMethod = PercentileMethod::None;
        return out;
    }

    out.totalFrameTimeMs = m_sumFrameTimeMs;
    out.meanFrameTimeMs = m_sumFrameTimeMs / static_cast<double>(m_samples);
    out.minFrameTimeMs = std::isfinite(m_minFrameTimeMs) ? m_minFrameTimeMs : 0.0;
    out.maxFrameTimeMs = std::isfinite(m_maxFrameTimeMs) ? m_maxFrameTimeMs : 0.0;

    double variance = (m_sumFrameTimeSqMs / static_cast<double>(m_samples)) -
                      (out.meanFrameTimeMs * out.meanFrameTimeMs);
    if (!std::isfinite(variance) || variance < 0.0) variance = 0.0;
    out.varianceFrameTimeMs = variance;
    out.stdDevFrameTimeMs = std::sqrt(variance);

    out.fpsFromMeanFrameTime = out.meanFrameTimeMs > 0.0 ? (1000.0 / out.meanFrameTimeMs) : 0.0;
    double windowSeconds = out.totalFrameTimeMs / 1000.0;
    out.fpsFromWindow = windowSeconds > 0.0
                            ? (static_cast<double>(m_samples) / windowSeconds)
                            : 0.0;

    // Exact percentiles need every retained sample. If the ring overflowed, the
    // oldest samples are gone and only the histogram still describes the whole
    // window, so the method is reported as approximate.
    bool exactAvailable = (precision == StatPrecision::Exact) && (m_config.ringCapacity > 0) &&
                          (m_samples <= m_config.ringCapacity) && !m_ring.empty();
    out.precisionWasFast = (precision == StatPrecision::Fast);

    if (exactAvailable) {
        m_sortScratch.resize(m_samples);
        for (size_t i = 0; i < m_samples; ++i) {
            m_sortScratch[i] = static_cast<double>(m_ring[i]);
        }
        std::sort(m_sortScratch.begin(),
                  m_sortScratch.begin() + static_cast<std::ptrdiff_t>(m_samples));
        const double* sorted = m_sortScratch.data();
        out.p50FrameTimeMs = medianFromSorted(sorted, m_samples);
        out.p95FrameTimeMs = percentileFromSorted(sorted, m_samples, 0.95);
        out.p99FrameTimeMs = percentileFromSorted(sorted, m_samples, 0.99);
        out.percentileMethod = PercentileMethod::Exact;
        return out;
    }

    if (m_histogram.empty() || m_histogramBinWidthMs <= 0.0) {
        // No estimator available: report the mean-derived values only and make
        // the absence explicit rather than returning plausible-looking zeros.
        out.percentileMethod = PercentileMethod::None;
        out.p50FrameTimeMs = out.meanFrameTimeMs;
        out.p95FrameTimeMs = out.maxFrameTimeMs;
        out.p99FrameTimeMs = out.maxFrameTimeMs;
        return out;
    }

    // Histogram walk. Rank is the same nearest-rank definition as the exact
    // path; the value returned is the bin's UPPER bound, so the estimate can
    // exceed the true percentile by at most one bin width and never under it.
    auto rankFor = [&](double p) -> uint64_t {
        double rankExact = p * static_cast<double>(m_samples);
        unsigned long long rank = static_cast<unsigned long long>(std::ceil(rankExact));
        if (rank < 1) rank = 1;
        if (rank > m_samples) rank = static_cast<unsigned long long>(m_samples);
        return rank;
    };
    auto valueAtRank = [&](uint64_t rank) -> double {
        uint64_t cumulative = 0;
        for (size_t bin = 0; bin < m_histogram.size(); ++bin) {
            cumulative += m_histogram[bin];
            if (cumulative >= rank) {
                if (bin + 1 >= m_histogram.size() && m_histogramSaturatedSamples > 0 &&
                    cumulative == m_histogram[bin]) {
                    // Reached the overflow bin: the true value is unknown but is
                    // at least histogramMaxMs. Report the lower bound of the
                    // overflow bin and flag saturation.
                    return m_config.histogramMaxMs;
                }
                return static_cast<double>(bin + 1) * m_histogramBinWidthMs;
            }
        }
        return m_config.histogramMaxMs;
    };

    double median = 0.0;
    if (m_samples % 2 == 0 && m_samples >= 2) {
        // Keep the median definition identical to the exact path: average the
        // two central ranks.
        double lower = valueAtRank(m_samples / 2);
        double upper = valueAtRank(m_samples / 2 + 1);
        median = 0.5 * (lower + upper);
    } else {
        median = valueAtRank((m_samples + 1) / 2);
    }

    out.p50FrameTimeMs = median;
    out.p95FrameTimeMs = valueAtRank(rankFor(0.95));
    out.p99FrameTimeMs = valueAtRank(rankFor(0.99));

    // The histogram estimator is monotone in rank by construction, but the
    // even-count median averages two ranks, so clamp defensively. A clamp that
    // fires would be a bug; the assertion in the test suite is what catches it.
    if (out.p95FrameTimeMs < out.p50FrameTimeMs) out.p95FrameTimeMs = out.p50FrameTimeMs;
    if (out.p99FrameTimeMs < out.p95FrameTimeMs) out.p99FrameTimeMs = out.p95FrameTimeMs;

    out.percentileMethod = PercentileMethod::HistogramApproximate;
    return out;
}

size_t SampleWindow::getOwnedBytes() const {
    return m_ring.size() * sizeof(float) +
           m_sortScratch.size() * sizeof(double) +
           m_histogram.size() * sizeof(uint32_t);
}

} // namespace optigd::diagnostics
