// Part of the Concrete Compiler Project, under the BSD3 License with Zama
// Exceptions. See
// https://github.com/zama-ai/concrete-compiler-internal/blob/main/LICENSE.txt
// for license information.

#include "concretelang/Common/Keysets.h"
#include "concrete-cpu.h"
#include "concrete-protocol.pb.h"
#include "concretelang/Common/Error.h"
#include "concretelang/Common/Keys.h"
#include "concretelang/Common/Csprng.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <utime.h>

using concretelang::error::Result;
using concretelang::error::StringError;
using concretelang::keys::LweBootstrapKey;
using concretelang::keys::LweKeyswitchKey;
using concretelang::keys::LweSecretKey;
using concretelang::keys::PackingKeyswitchKey;
using concretelang::csprng::ConcreteCSPRNG;

namespace concretelang {
namespace keysets {

ClientKeyset
ClientKeyset::fromProto(const concreteprotocol::ClientKeyset &proto) {
  ClientKeyset output;
  for (auto lweSecretKeyProto : proto.lwesecretkeys()) {
    output.lweSecretKeys.push_back(LweSecretKey::fromProto(lweSecretKeyProto));
  }
  return output;
}

concreteprotocol::ClientKeyset ClientKeyset::toProto() {
  concreteprotocol::ClientKeyset output;
  for (auto lweSecretKey : this->lweSecretKeys) {
    output.mutable_lwesecretkeys()->AddAllocated(
        new concreteprotocol::LweSecretKey(lweSecretKey.toProto()));
  }
  return output;
}

ServerKeyset
ServerKeyset::fromProto(const concreteprotocol::ServerKeyset &proto) {
  ServerKeyset output;
  for (auto lweBootstrapKeyProto : proto.lwebootstrapkeys()) {
    output.lweBootstrapKeys.push_back(
        LweBootstrapKey::fromProto(lweBootstrapKeyProto));
  }
  for (auto lweKeyswitchKeyProto : proto.lwekeyswitchkeys()) {
    output.lweKeyswitchKeys.push_back(
        LweKeyswitchKey::fromProto(lweKeyswitchKeyProto));
  }
  for (auto packingKeyswitchKeyProto : proto.packingkeyswitchkeys()) {
    output.packingKeyswitchKeys.push_back(
        PackingKeyswitchKey::fromProto(packingKeyswitchKeyProto));
  }
  return output;
}

concreteprotocol::ServerKeyset ServerKeyset::toProto() {
  concreteprotocol::ServerKeyset output;
  for (auto key : this->lweBootstrapKeys) {
    output.mutable_lwebootstrapkeys()->AddAllocated(
        new concreteprotocol::LweBootstrapKey(key.toProto()));
  }
  for (auto key : this->lweKeyswitchKeys) {
    output.mutable_lwekeyswitchkeys()->AddAllocated(
        new concreteprotocol::LweKeyswitchKey(key.toProto()));
  }
  for (auto key : this->packingKeyswitchKeys) {
    output.mutable_packingkeyswitchkeys()->AddAllocated(
        new concreteprotocol::PackingKeyswitchKey(key.toProto()));
  }
  return output;
}


Keyset::Keyset(const concreteprotocol::KeysetInfo &info, CSPRNG &csprng) {
  for (auto keyInfo : info.lwesecretkeys()) {
    client.lweSecretKeys.push_back(LweSecretKey(keyInfo, csprng));
  }
  for (auto keyInfo : info.lwebootstrapkeys()) {
    server.lweBootstrapKeys.push_back(
        LweBootstrapKey(keyInfo, client.lweSecretKeys[keyInfo.inputid()],
                        client.lweSecretKeys[keyInfo.outputid()], csprng));
  }
  for (auto keyInfo : info.lwekeyswitchkeys()) {
    server.lweKeyswitchKeys.push_back(
        LweKeyswitchKey(keyInfo, client.lweSecretKeys[keyInfo.inputid()],
                        client.lweSecretKeys[keyInfo.outputid()], csprng));
  }
  for (auto keyInfo : info.packingkeyswitchkeys()) {
    server.packingKeyswitchKeys.push_back(
        PackingKeyswitchKey(keyInfo, client.lweSecretKeys[keyInfo.inputid()],
                            client.lweSecretKeys[keyInfo.outputid()], csprng));
  }
}

Keyset Keyset::fromProto(const concreteprotocol::Keyset &proto) {
  auto server = ServerKeyset::fromProto(proto.server());
  auto client = ClientKeyset::fromProto(proto.client());
  return Keyset{server, client};
}

concreteprotocol::Keyset Keyset::toProto() {
  concreteprotocol::Keyset output;
  output.set_allocated_client(
      new concreteprotocol::ClientKeyset(this->client.toProto()));
  output.set_allocated_server(
      new concreteprotocol::ServerKeyset(this->server.toProto()));
  return output;
}

template <typename ProtoKey, typename Key>
Result<Key> loadKey(std::string path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return StringError("Cannot open " + path);
  }
  ProtoKey keyProto = ProtoKey();
  bool parsed = keyProto.ParseFromFileDescriptor(fd);
  if (!parsed) {
    return StringError("Failed to parse key at path(" + path + ")");
  }
  if (close(fd) == -1) {
    return StringError("Failed to close file descriptor.");
  }
  return Key::fromProto(keyProto);
}

template <typename ProtoKey, typename Key>
Result<void> saveKey(Key key, std::string path) {
  int fd = creat(path.c_str(), O_WRONLY);
  if (fd == -1) {
    return StringError("Cannot open " + path);
  }
  ProtoKey keyProto = key.toProto();
  bool serialized = keyProto.SerializeToFileDescriptor(fd);
  if (!serialized) {
    return StringError("Failed to serialize key to path(" + path + ")");
  }
  if (close(fd) == -1) {
    return StringError("Failed to close file descriptor.");
  }
  return outcome::success();
}

Result<Keyset> loadKeysFromFiles(const concreteprotocol::KeysetInfo &keysetInfo,
                                 uint64_t seed_msb, uint64_t seed_lsb,
                                 std::string folderPath) {
#ifdef CONCRETELANG_GENERATE_UNSECURE_SECRET_KEYS
  getApproval();
#endif

  // Mark the folder as recently use.
  // e.g. so the CI can do some cleanup of unused keys.
  utime(folderPath.c_str(), nullptr);

  std::vector<LweSecretKey> secretKeys;
  std::vector<LweBootstrapKey> bootstrapKeys;
  std::vector<LweKeyswitchKey> keyswitchKeys;
  std::vector<PackingKeyswitchKey> packingKeyswitchKeys;

  // Load secret keys
  for (auto keyInfo : keysetInfo.lwesecretkeys()) {
    // TODO - Check parameters?
    // auto param = secretKeyParam.second;
    llvm::SmallString<0> path(folderPath);
    llvm::sys::path::append(path, "secretKey_" + std::to_string(keyInfo.id()));
    OUTCOME_TRY(auto key, loadKey<concreteprotocol::LweSecretKey, LweSecretKey>(
                              (std::string)path));
    secretKeys.push_back(key);
  }
  // Load bootstrap keys
  for (auto keyInfo : keysetInfo.lwebootstrapkeys()) {
    // TODO - Check parameters?
    // auto param = p.value();
    llvm::SmallString<0> path(folderPath);
    llvm::sys::path::append(path, "pbsKey_" + std::to_string(keyInfo.id()));
    OUTCOME_TRY(auto key,
                loadKey<concreteprotocol::LweBootstrapKey, LweBootstrapKey>(
                    (std::string)path));
    bootstrapKeys.push_back(key);
  }
  // Load keyswitch keys
  for (auto keyInfo : keysetInfo.lwekeyswitchkeys()) {
    // TODO - Check parameters?
    // auto param = p.value();
    llvm::SmallString<0> path(folderPath);
    llvm::sys::path::append(path, "ksKey_" + std::to_string(keyInfo.id()));
    OUTCOME_TRY(auto key,
                loadKey<concreteprotocol::LweKeyswitchKey, LweKeyswitchKey>(
                    (std::string)path));
    keyswitchKeys.push_back(key);
  }
  // Load packing keyswitch keys
  for (auto keyInfo : keysetInfo.packingkeyswitchkeys()) {
    // TODO - Check parameters?
    // auto param = p.value();
    llvm::SmallString<0> path(folderPath);
    llvm::sys::path::append(path, "pksKey_" + std::to_string(keyInfo.id()));
    OUTCOME_TRY(
        auto key,
        loadKey<concreteprotocol::PackingKeyswitchKey, PackingKeyswitchKey>(
            (std::string)path));
    packingKeyswitchKeys.push_back(key);
  }

  ClientKeyset clientKeyset = ClientKeyset{secretKeys};
  ServerKeyset serverKeyset =
      ServerKeyset{bootstrapKeys, keyswitchKeys, packingKeyswitchKeys};
  Keyset keyset = Keyset {   serverKeyset, clientKeyset };

  return keyset;
}

Result<void> saveKeys(Keyset &keyset, llvm::SmallString<0> &folderPath) {
#ifdef CONCRETELANG_GENERATE_UNSECURE_SECRET_KEYS
  getApproval();
#endif

  llvm::SmallString<0> folderIncompletePath = folderPath;
  folderIncompletePath.append(".incomplete");

  auto err = llvm::sys::fs::create_directories(folderIncompletePath);
  if (err) {
    return StringError("Cannot create directory \"")
           << std::string(folderIncompletePath) << "\": " << err.message();
  }

  auto clientKeyset = keyset.client;
  auto serverKeyset = keyset.server;

  // Save LWE secret keys
  for (auto key : clientKeyset.lweSecretKeys) {
    llvm::SmallString<0> path = folderIncompletePath;
    llvm::sys::path::append(path, "secretKey_" + std::to_string(key.getInfo().id()));
    OUTCOME_TRYV(
        saveKey<concreteprotocol::LweSecretKey, LweSecretKey>(key, path.c_str()));
  }
  // Save bootstrap keys
  for (auto key: serverKeyset.lweBootstrapKeys) {
    llvm::SmallString<0> path = folderIncompletePath;
    llvm::sys::path::append(path, "pbsKey_" + key.getInfo().id());
    OUTCOME_TRYV(
        saveKey<concreteprotocol::LweBootstrapKey, LweBootstrapKey>(key, path.c_str()));
  }
  // Save keyswitch keys
  for (auto key: serverKeyset.lweKeyswitchKeys) {
    llvm::SmallString<0> path = folderIncompletePath;
    llvm::sys::path::append(path, "ksKey_" + key.getInfo().id());
    OUTCOME_TRYV(
        saveKey<concreteprotocol::LweKeyswitchKey, LweKeyswitchKey>(key, path.c_str()));
  }
  // Save packing keyswitch keys
  for (auto key: serverKeyset.packingKeyswitchKeys) {
    llvm::SmallString<0> path = folderIncompletePath;
    llvm::sys::path::append(path, "pksKey_" + key.getInfo().id());
    OUTCOME_TRYV(
        saveKey<concreteprotocol::PackingKeyswitchKey, PackingKeyswitchKey>(
            key, path.c_str()));
  }

  err = llvm::sys::fs::rename(folderIncompletePath, folderPath);
  if (err) {
    llvm::sys::fs::remove_directories(folderIncompletePath);
  }
  if (!llvm::sys::fs::exists(folderPath)) {
    return StringError("Cannot save directory \"")
           << std::string(folderPath) << "\"";
  }

  return outcome::success();
}

KeysetCache::KeysetCache(std::string backingDirectoryPath) {
  // check key;
  this->backingDirectoryPath = backingDirectoryPath;
}

Result<Keyset> KeysetCache::getKeyset(const concreteprotocol::KeysetInfo &keysetInfo,
                                      uint64_t seed_msb, uint64_t seed_lsb) {
  std::string hashString = keysetInfo.SerializeAsString() +
                           std::to_string(seed_msb) + std::to_string(seed_lsb);
  size_t hash = std::hash<std::string>{}(hashString);
#ifdef CONCRETELANG_GENERATE_UNSECURE_SECRET_KEYS
  getApproval();
#endif

  llvm::SmallString<0> folderPath =
      llvm::SmallString<0>(this->backingDirectoryPath);
  llvm::sys::path::append(folderPath, std::to_string(hash));

  // Creating a lock for concurrent generation
  llvm::SmallString<0> lockPath(folderPath);
  lockPath.append("lock");
  int FD_lock;
  llvm::sys::fs::create_directories(llvm::sys::path::parent_path(lockPath));
  // Open or create the lock file
  auto err = llvm::sys::fs::openFile(
      lockPath, FD_lock, llvm::sys::fs::CreationDisposition::CD_OpenAlways,
      llvm::sys::fs::FileAccess::FA_Write, llvm::sys::fs::OpenFlags::OF_None);

  if (err) {
    // parent does not exists OR right issue (creation or write)
    return StringError("Cannot access \"")
           << std::string(lockPath) << "\": " << err.message();
  }

  // The lock is released when the function returns.
  // => any intermediate state in the function is not visible to others.
  auto unlockAtReturn = llvm::make_scope_exit([&]() {
    llvm::sys::fs::closeFile(FD_lock);
    llvm::sys::fs::unlockFile(FD_lock);
    llvm::sys::fs::remove(lockPath);
  });
  llvm::sys::fs::lockFile(FD_lock);

  if (llvm::sys::fs::exists(folderPath)) {
    // Once it has been generated by another process (or was already here)
    auto keys = loadKeysFromFiles(keysetInfo, seed_msb, seed_lsb,
                                 std::string(folderPath));
    if (keys.has_value()) {
      return keys;
    } else {
      std::cerr << std::string(keys.error().mesg) << "\n";
      std::cerr << "Invalid KeySetCache entry " << std::string(folderPath)
                << "\n";
      llvm::sys::fs::remove_directories(folderPath);
      // Then we can continue as it didn't exist
    }
  }

  std::cerr << "KeySetCache: miss, regenerating " << std::string(folderPath)
            << "\n";

  __uint128_t seed = seed_msb;
  seed <<= 64;
  seed += seed_lsb;

  auto csprng = ConcreteCSPRNG(seed);
  Keyset keyset =  Keyset(keysetInfo, csprng);

  OUTCOME_TRYV(saveKeys(keyset, folderPath));

  return std::move(keyset);
}

} // namespace keysets
} // namespace concretelang
