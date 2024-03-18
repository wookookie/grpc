// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/core/lib/event_engine/extensions/tcp_trace.h"

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/metrics.h"
#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

const absl::string_view kTCPConnectionMetricLabel = "grpc.tcp";

const auto kTCPConnectionMetricsMinRtt =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
        "grpc.tcp.min_rtt",
        "Reports TCP's current estimate of minimum round trip time (RTT), "
        "typically used as an indication of the network health between two "
        "endpoints.",
        "{usec}", {kTCPConnectionMetricLabel}, {}, true);

const auto kTCPConnectionMetricsDeliveryRate =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Histogram(
        "grpc.tcp.delivery_rate",
        "Records the most recent non-app-limited throughput at the time that "
        "Fathom samples the connection statistics.",
        "{Bps}", {kTCPConnectionMetricLabel}, {}, true);

const auto kTCPConnectionMetricsPacketSend =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packet_send",
        "Total packets TCP send in the calculation period.", "{packets}",
        {kTCPConnectionMetricLabel}, {}, true);

const auto kTCPConnectionMetricsPacketRetx =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packet_retx",
        "Total packets lost in the calculation period, including lost or "
        "spuriously retransmitted packets.",
        "{packets}", {kTCPConnectionMetricLabel}, {}, true);

const auto kTCPConnectionMetricsPacketSpuriousRetx =
    grpc_core::GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.tcp.packet_spurious_retx",
        "Total packets spuriously retransmitted in the calculation period.",
        "{packets}", {kTCPConnectionMetricLabel}, {}, true);

void Http2TransportTcpTracer::RecordConnectionMetrics(
    ConnectionMetrics metrics) {
  // This will be called periodically by Fathom.
  // For cumulative stats, compute and get deltas to stats plugins.
  if (metrics.packet_sent.has_value() && metrics.packet_retx.has_value() &&
      metrics.packet_spurious_retx.has_value()) {
    GPR_ASSERT(metrics.packet_sent.value() > 0);
    grpc_core::MutexLock lock(&mu_);
    int packet_sent = metrics.packet_sent.value() -
                      connection_metrics_.packet_sent.value_or(0);
    int packet_retx = metrics.packet_retx.value() -
                      connection_metrics_.packet_retx.value_or(0);
    int packet_spurious_retx =
        metrics.packet_spurious_retx.value() -
        connection_metrics_.packet_spurious_retx.value_or(0);
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketSend, packet_sent,
                    {kTCPConnectionMetricLabel}, {});
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketRetx, packet_retx,
                    {kTCPConnectionMetricLabel}, {});
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .AddCounter(kTCPConnectionMetricsPacketSpuriousRetx,
                    packet_spurious_retx, {kTCPConnectionMetricLabel}, {});
    connection_metrics_.packet_sent = metrics.packet_sent;
    connection_metrics_.packet_retx = metrics.packet_retx;
    connection_metrics_.packet_spurious_retx = metrics.packet_spurious_retx;
  }
  // For non-cumulative stats: Report to stats plugins.
  if (metrics.min_rtt.has_value()) {
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .RecordHistogram(kTCPConnectionMetricsMinRtt, metrics.min_rtt.value(),
                         {kTCPConnectionMetricLabel}, {});
  }
  if (metrics.delivery_rate.has_value()) {
    grpc_core::GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        grpc_core::experimental::StatsPluginChannelScope("", ""))
        .RecordHistogram(kTCPConnectionMetricsDeliveryRate,
                         metrics.delivery_rate.value(),
                         {kTCPConnectionMetricLabel}, {});
  }
}
}  // namespace experimental
}  // namespace grpc_event_engine