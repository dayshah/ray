// Copyright 2017 The Ray Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ray/common/asio/postable.h"
#include "ray/gcs/gcs_server/gcs_kv_manager.h"
#include "ray/gcs/store_client/store_client.h"

namespace ray {
namespace gcs {

/// InternalKVInterface implementation that wraps around
/// StoreClient.
/// Please refer to InternalKVInterface for semantics
/// of public APIs.
class StoreClientInternalKV : public InternalKVInterface {
 public:
  explicit StoreClientInternalKV(std::unique_ptr<StoreClient> store_client);

  void Get(const std::string &ns,
           const std::string &key,
           Postable<void(std::optional<std::string>)> callback) override;

  void MultiGet(
      const std::string &ns,
      const std::vector<std::string> &keys,
      Postable<void(absl::flat_hash_map<std::string, std::string>)> callback) override;

  void Put(const std::string &ns,
           const std::string &key,
           std::string value,
           bool overwrite,
           Postable<void(bool)> callback) override;

  void Del(const std::string &ns,
           const std::string &key,
           bool del_by_prefix,
           Postable<void(int64_t)> callback) override;

  void Exists(const std::string &ns,
              const std::string &key,
              Postable<void(bool)> callback) override;

  void Keys(const std::string &ns,
            const std::string &prefix,
            Postable<void(std::vector<std::string>)> callback) override;

 private:
  std::unique_ptr<StoreClient> delegate_;
  const std::string table_name_;
};
}  // namespace gcs
}  // namespace ray
