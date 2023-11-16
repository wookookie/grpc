//
//
// Copyright 2017 gRPC authors.
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
//
//

#include <memory>

#include "gtest/gtest.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that we don't make any further attempts after we exceed the
// max buffer size.
// - 1 retry allowed for ABORTED status
// - buffer size set to 2 bytes
// - client sends a 3-byte message
// - first attempt gets ABORTED but is not retried
CORE_END2END_TEST(RetryTest, RetryExceedsBufferSizeInInitialBatch) {
  InitServer(ChannelArgs());
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_SERVICE_CONFIG,
               "{\n"
               "  \"methodConfig\": [ {\n"
               "    \"name\": [\n"
               "      { \"service\": \"service\", \"method\": \"method\" }\n"
               "    ],\n"
               "    \"retryPolicy\": {\n"
               "      \"maxAttempts\": 2,\n"
               "      \"initialBackoff\": \"1s\",\n"
               "      \"maxBackoff\": \"120s\",\n"
               "      \"backoffMultiplier\": 1.6,\n"
               "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
               "    }\n"
               "  } ]\n"
               "}")
          .Set(GRPC_ARG_PER_RPC_RETRY_BUFFER_SIZE, 2));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingMessage server_message;
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
