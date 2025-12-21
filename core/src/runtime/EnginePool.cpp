#include "ijccrl/core/runtime/EnginePool.h"

#include <iostream>

namespace ijccrl::core::runtime {

EngineLease::EngineLease(EnginePool* pool, int white_id, int black_id)
    : pool_(pool), white_id_(white_id), black_id_(black_id) {}

EngineLease::EngineLease(EngineLease&& other) noexcept
    : pool_(other.pool_), white_id_(other.white_id_), black_id_(other.black_id_) {
    other.pool_ = nullptr;
    other.white_id_ = -1;
    other.black_id_ = -1;
}

EngineLease& EngineLease::operator=(EngineLease&& other) noexcept {
    if (this != &other) {
        Release();
        pool_ = other.pool_;
        white_id_ = other.white_id_;
        black_id_ = other.black_id_;
        other.pool_ = nullptr;
        other.white_id_ = -1;
        other.black_id_ = -1;
    }
    return *this;
}

EngineLease::~EngineLease() {
    Release();
}

ijccrl::core::uci::UciEngine& EngineLease::white() {
    return pool_->engine(white_id_);
}

ijccrl::core::uci::UciEngine& EngineLease::black() {
    return pool_->engine(black_id_);
}

void EngineLease::Release() {
    if (pool_) {
        pool_->ReleasePair(white_id_, black_id_);
        pool_ = nullptr;
    }
}

EnginePool::EnginePool(std::vector<EngineSpec> specs)
    : specs_(std::move(specs)) {
    engines_.reserve(specs_.size());
    for (const auto& spec : specs_) {
        engines_.emplace_back(spec.name, spec.command, spec.args);
    }
    busy_.assign(engines_.size(), false);
}

bool EnginePool::StartAll(const std::string& working_dir) {
    working_dir_ = working_dir;
    for (size_t i = 0; i < engines_.size(); ++i) {
        if (!InitializeEngine(static_cast<int>(i))) {
            return false;
        }
    }
    return true;
}

EngineLease EnginePool::AcquirePair(int white_id, int black_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() {
        return !busy_[static_cast<size_t>(white_id)] &&
               !busy_[static_cast<size_t>(black_id)];
    });
    busy_[static_cast<size_t>(white_id)] = true;
    busy_[static_cast<size_t>(black_id)] = true;
    return EngineLease(this, white_id, black_id);
}

void EnginePool::ReleasePair(int white_id, int black_id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_[static_cast<size_t>(white_id)] = false;
        busy_[static_cast<size_t>(black_id)] = false;
    }
    cv_.notify_all();
}

bool EnginePool::RestartEngine(int engine_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_id < 0 || engine_id >= static_cast<int>(engines_.size())) {
        return false;
    }
    engines_[static_cast<size_t>(engine_id)].Stop();
    return InitializeEngine(engine_id);
}

ijccrl::core::uci::UciEngine& EnginePool::engine(int engine_id) {
    return engines_[static_cast<size_t>(engine_id)];
}

bool EnginePool::InitializeEngine(int engine_id) {
    auto& engine = engines_[static_cast<size_t>(engine_id)];
    if (!engine.Start(working_dir_)) {
        std::cerr << "[engine-pool] Failed to start engine " << engine_id << '\n';
        return false;
    }
    if (!engine.UciHandshake()) {
        std::cerr << "[engine-pool] UCI handshake failed for engine " << engine_id << '\n';
        return false;
    }
    for (const auto& [name, value] : specs_[static_cast<size_t>(engine_id)].uci_options) {
        engine.SetOption(name, value);
    }
    engine.IsReady();
    return true;
}

}  // namespace ijccrl::core::runtime
