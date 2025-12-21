#pragma once

#include "ijccrl/core/uci/UciEngine.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ijccrl::core::runtime {

struct EngineSpec {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> uci_options;
};

class EnginePool;

class EngineLease {
public:
    EngineLease() = default;
    EngineLease(EnginePool* pool, int white_id, int black_id);
    EngineLease(const EngineLease&) = delete;
    EngineLease& operator=(const EngineLease&) = delete;
    EngineLease(EngineLease&& other) noexcept;
    EngineLease& operator=(EngineLease&& other) noexcept;
    ~EngineLease();

    ijccrl::core::uci::UciEngine& white();
    ijccrl::core::uci::UciEngine& black();
    int white_id() const { return white_id_; }
    int black_id() const { return black_id_; }
    bool valid() const { return pool_ != nullptr; }

private:
    void Release();

    EnginePool* pool_ = nullptr;
    int white_id_ = -1;
    int black_id_ = -1;
};

class EnginePool {
public:
    explicit EnginePool(std::vector<EngineSpec> specs,
                        std::function<void(const std::string&)> log_fn = {});

    bool StartAll(const std::string& working_dir);
    EngineLease AcquirePair(int white_id, int black_id);
    void ReleasePair(int white_id, int black_id);
    bool RestartEngine(int engine_id);
    void set_handshake_timeout_ms(int timeout_ms) { handshake_timeout_ms_ = timeout_ms; }

    ijccrl::core::uci::UciEngine& engine(int engine_id);
    const std::vector<EngineSpec>& specs() const { return specs_; }

private:
    bool InitializeEngine(int engine_id);

    std::vector<EngineSpec> specs_;
    std::vector<std::unique_ptr<ijccrl::core::uci::UciEngine>> engines_;
    std::vector<bool> busy_;
    std::string working_dir_;
    int handshake_timeout_ms_ = 10000;
    std::function<void(const std::string&)> log_fn_{};
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace ijccrl::core::runtime
