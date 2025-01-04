// Copyright 2021 The Ray Authors.
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

#include "ray/gcs/gcs_server/gcs_kv_manager.h"

#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

namespace {

constexpr std::string_view kNamespacePrefix = "@namespace_";
constexpr std::string_view kNamespaceSep = ":";

std::string MakeKey(const std::string &ns, const std::string &key) {
  if (ns.empty()) {
    return key;
  }
  return absl::StrCat(kNamespacePrefix, ns, kNamespaceSep, key);
}

std::string ExtractKey(const std::string &key) {
  if (absl::StartsWith(key, kNamespacePrefix)) {
    std::vector<std::string> parts =
        absl::StrSplit(key, absl::MaxSplits(kNamespaceSep, 1));
    RAY_CHECK(parts.size() == 2) << "Invalid key: " << key;

    return parts[1];
  }
  return key;
}

}  // namespace

namespace ray {
namespace gcs {

void GcsInternalKVManager::HandleInternalKVGet(
    rpc::InternalKVGetRequest request,
    rpc::InternalKVGetReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  auto status = ValidateKey(request.key());
  if (!status.ok()) {
    GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
    return;
  }
  RAY_CHECK_OK(kv_store_client_->AsyncGet(
      table_name_,
      MakeKey(request.namespace_(), request.key()),
      {[reply, send_reply_callback = std::move(send_reply_callback)](auto status,
                                                                     auto result) {
         RAY_CHECK(status.ok()) << "Fails to get key from storage " << status;
         if (result) {
           reply->set_value(*result);
           GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
         } else {
           GCS_RPC_SEND_REPLY(
               send_reply_callback, reply, Status::NotFound("Failed to find the key"));
         }
       },
       io_context_}));
}

void GcsInternalKVManager::HandleInternalKVMultiGet(
    rpc::InternalKVMultiGetRequest request,
    rpc::InternalKVMultiGetReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  for (auto &key : request.keys()) {
    auto status = ValidateKey(key);
    if (!status.ok()) {
      GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
      return;
    }
  }
  std::vector<std::string> prefixed_keys;
  prefixed_keys.reserve(request.keys().size());
  for (const auto &key : request.keys()) {
    prefixed_keys.push_back(MakeKey(request.namespace_(), key));
  }
  RAY_CHECK_OK(kv_store_client_->AsyncMultiGet(
      table_name_,
      prefixed_keys,
      {[reply, send_reply_callback = std::move(send_reply_callback)](auto result) {
         for (auto &&item : std::move(result)) {
           auto entry = reply->add_results();
           entry->set_key(ExtractKey(item.first));
           entry->set_value(std::move(item.second));
         }
         GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
       },
       io_context_}));
}

void GcsInternalKVManager::HandleInternalKVPut(
    rpc::InternalKVPutRequest request,
    rpc::InternalKVPutReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  auto status = ValidateKey(request.key());
  if (!status.ok()) {
    GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
    return;
  }
  RAY_CHECK_OK(kv_store_client_->AsyncPut(
      table_name_,
      MakeKey(request.namespace_(), request.key()),
      std::move(*request.mutable_value()),
      request.overwrite(),
      {[reply, send_reply_callback = std::move(send_reply_callback)](bool newly_added) {
         reply->set_added_num(newly_added ? 1 : 0);
         GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
       },
       io_context_}));
}

void GcsInternalKVManager::HandleInternalKVDel(
    rpc::InternalKVDelRequest request,
    rpc::InternalKVDelReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  auto status = ValidateKey(request.key());
  if (!status.ok()) {
    GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
    return;
  }
  if (!request.del_by_prefix()) {
    RAY_CHECK_OK(kv_store_client_->AsyncDelete(
        table_name_,
        MakeKey(request.namespace_(), request.key()),
        {[reply, send_reply_callback = std::move(send_reply_callback)](bool deleted) {
           reply->set_deleted_num(deleted ? 1 : 0);
           GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
         },
         io_context_}));
    return;
  }
  // If del_by_prefix, we need to get all keys first, and then call AsyncBatchDelete.
  RAY_CHECK_OK(kv_store_client_->AsyncGetKeys(
      table_name_,
      MakeKey(request.namespace_(), request.key()),
      {[this, reply, send_reply_callback = std::move(send_reply_callback)](
           auto keys) mutable {
         if (keys.empty()) {
           reply->set_deleted_num(0);
           GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
           return;
         }
         RAY_CHECK_OK(kv_store_client_->AsyncBatchDelete(
             table_name_,
             keys,
             {[reply, send_reply_callback](int64_t num_deleted) {
                reply->set_deleted_num(num_deleted);
                GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
              },
              io_context_}));
       },
       io_context_}));
}

void GcsInternalKVManager::HandleInternalKVExists(
    rpc::InternalKVExistsRequest request,
    rpc::InternalKVExistsReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  auto status = ValidateKey(request.key());
  if (!status.ok()) {
    GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
    return;
  }
  RAY_CHECK_OK(kv_store_client_->AsyncExists(
      table_name_,
      MakeKey(request.namespace_(), request.key()),
      {[reply, send_reply_callback](bool exists) {
         reply->set_exists(exists);
         GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
       },
       io_context_}));
}

void GcsInternalKVManager::HandleInternalKVKeys(
    rpc::InternalKVKeysRequest request,
    rpc::InternalKVKeysReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  auto status = ValidateKey(request.prefix());
  if (!status.ok()) {
    GCS_RPC_SEND_REPLY(send_reply_callback, reply, status);
    return;
  }
  RAY_CHECK_OK(kv_store_client_->AsyncGetKeys(
      table_name_,
      MakeKey(request.namespace_(), request.prefix()),
      {[reply, send_reply_callback = std::move(send_reply_callback)](
           std::vector<std::string> keys) {
         for (auto &key : keys) {
           reply->add_results(ExtractKey(key));
         }
         GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
       },
       io_context_}));
}

void GcsInternalKVManager::HandleGetInternalConfig(
    rpc::GetInternalConfigRequest request,
    rpc::GetInternalConfigReply *reply,
    rpc::SendReplyCallback send_reply_callback) {
  reply->set_config(raylet_config_list_);
  GCS_RPC_SEND_REPLY(send_reply_callback, reply, Status::OK());
}

Status GcsInternalKVManager::ValidateKey(const std::string &key) const {
  constexpr std::string_view kNamespacePrefix = "@namespace_";
  if (absl::StartsWith(key, kNamespacePrefix)) {
    return Status::KeyError(absl::StrCat("Key can't start with ", kNamespacePrefix));
  }
  return Status::OK();
}

}  // namespace gcs
}  // namespace ray
