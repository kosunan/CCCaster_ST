#pragma once
#include <cstdint>
#include <iostream>
#include <algorithm>

namespace dummy_peer {

// 受信したフレーム番号の連続性を検証するバリデータ (#5)
class FrameSequenceValidator {
public:
    void Validate(uint32_t receivedFrame) {
        _totalReceived++;
        if (_firstFrame) {
            _expectedFrame = receivedFrame + 1;
            _firstFrame = false;
            return;
        }
        if (receivedFrame == _expectedFrame) {
            _expectedFrame++;
        } else if (receivedFrame > _expectedFrame) {
            _gaps += (receivedFrame - _expectedFrame);
            _expectedFrame = receivedFrame + 1;
        } else {
            _outOfOrder++;
        }
    }

    void PrintReport() const {
        std::cout << "  --- Frame Sequence Report ---\n";
        std::cout << "    Total Received: " << _totalReceived << "\n";
        std::cout << "    Gaps (missed):  " << _gaps << "\n";
        std::cout << "    Out-of-Order:   " << _outOfOrder << "\n";
        if (_totalReceived > 0) {
            double integrity = 100.0 * (1.0 - static_cast<double>(_gaps + _outOfOrder) / _totalReceived);
            std::cout << "    Integrity:      " << integrity << "%\n";
        }
    }

    uint64_t GetGaps() const { return _gaps; }
    uint64_t GetOutOfOrder() const { return _outOfOrder; }
    uint64_t GetTotalReceived() const { return _totalReceived; }

private:
    bool _firstFrame = true;
    uint32_t _expectedFrame = 0;
    uint64_t _totalReceived = 0;
    uint64_t _gaps = 0;
    uint64_t _outOfOrder = 0;
};

// フレーム間隔のヒストグラム (#7)
class FrameIntervalHistogram {
public:
    void Record(int intervalMs) {
        _totalFrames++;
        if (intervalMs < 5) _bucketBurst++;
        else if (intervalMs < 16) _bucketFast++;
        else if (intervalMs <= 30) _bucketNormal++;
        else _bucketSlow++;
    }

    void PrintReport() const {
        std::cout << "  --- Frame Interval Histogram ---\n";
        std::cout << "    <5ms  (burst):  " << _bucketBurst << " frames\n";
        std::cout << "    5-15ms (fast):  " << _bucketFast << " frames\n";
        std::cout << "    16-30ms (norm): " << _bucketNormal << " frames\n";
        std::cout << "    >30ms (slow):   " << _bucketSlow << " frames\n";
    }

private:
    uint64_t _totalFrames = 0;
    uint64_t _bucketBurst = 0;  // <5ms
    uint64_t _bucketFast = 0;   // 5-15ms
    uint64_t _bucketNormal = 0; // 16-30ms
    uint64_t _bucketSlow = 0;   // >30ms
};

// 疑似ロールバック・ロールアップシミュレータ
// ロールバック: 遅れた相手入力を受信した時に発生
// ロールアップ: ロールバック後に現在フレームまで早送り（1実Fあたり rollupSpeed F 進む）
class RollbackSimulator {
public:
    explicit RollbackSimulator(int rollupSpeed = 5) 
        : _rollupSpeed(rollupSpeed) {}

    // 毎フレーム呼び出し: ロールアップ中なら消化する
    // 戻り値: このフレームでロールアップ中だったか（true = 通常処理不可）
    bool Tick() {
        if (_rollupRemaining > 0) {
            // ロールアップ中: rollupSpeed F分を1実Fで消化
            int consumed = std::min(_rollupRemaining, _rollupSpeed);
            _rollupRemaining -= consumed;
            _totalRollupTicks++;
            return true; // ロールアップ中（ゲームは一時停止/スキップ描画状態）
        }
        return false;
    }

    // 相手の入力パケットを受信した時に呼ぶ
    // localFrame: 自分の現在フレーム番号
    // remoteFrame: 相手が送信したフレーム番号
    void OnRemoteInput(uint32_t localFrame, uint32_t remoteFrame) {
        _totalRemoteReceived++;
        
        // ウォームアップ期間（最初の30パケットは無視）
        if (_totalRemoteReceived <= 30) {
            _lastConfirmedRemoteFrame = remoteFrame;
            return;
        }

        // ロールバック判定: 受信した相手のフレーム番号が前回確認値より古い
        // = ネットワーク遅延/順序逆転により過去のフレームが遅れて到着
        if (remoteFrame < _lastConfirmedRemoteFrame && _rollupRemaining == 0) {
            int depth = static_cast<int>(_lastConfirmedRemoteFrame - remoteFrame);
            
            if (depth > 0 && depth <= 15) {
                _rollbackCount++;
                _totalRollbackDepth += depth;
                if (depth > _maxRollbackDepth) _maxRollbackDepth = depth;
                
                // ロールアップコスト
                _rollupRemaining = depth;
                
                // 深度別ヒストグラム
                if (depth <= 2) _depthBucket1_2++;
                else if (depth <= 5) _depthBucket3_5++;
                else if (depth <= 10) _depthBucket6_10++;
                else _depthBucket11plus++;
            }
        }

        if (remoteFrame > _lastConfirmedRemoteFrame) {
            _lastConfirmedRemoteFrame = remoteFrame;
        }
    }

    void PrintReport() const {
        std::cout << "  --- Rollback Simulation Report ---\n";
        std::cout << "    Rollup Speed:     1F = " << _rollupSpeed << "F (rollup)\n";
        std::cout << "    Rollback Count:   " << _rollbackCount << "\n";
        std::cout << "    Max Depth:        " << _maxRollbackDepth << " F\n";
        std::cout << "    Avg Depth:        " 
                  << (_rollbackCount > 0 ? static_cast<double>(_totalRollbackDepth) / _rollbackCount : 0) << " F\n";
        std::cout << "    Total Rollup:     " << _totalRollupTicks << " real frames spent in rollup\n";
        std::cout << "    Depth Histogram:\n";
        std::cout << "      1-2F:  " << _depthBucket1_2 << "\n";
        std::cout << "      3-5F:  " << _depthBucket3_5 << "\n";
        std::cout << "      6-10F: " << _depthBucket6_10 << "\n";
        std::cout << "      11F+:  " << _depthBucket11plus << "\n";
    }

    uint32_t GetRollbackCount() const { return _rollbackCount; }
    int GetMaxDepth() const { return _maxRollbackDepth; }
    uint32_t GetTotalRollupTicks() const { return _totalRollupTicks; }

private:
    int _rollupSpeed;
    uint32_t _lastConfirmedRemoteFrame = 0;
    uint32_t _totalRemoteReceived = 0;
    int _rollupRemaining = 0;

    // 統計
    uint32_t _rollbackCount = 0;
    int _maxRollbackDepth = 0;
    uint64_t _totalRollbackDepth = 0;
    uint32_t _totalRollupTicks = 0;

    // 深度別ヒストグラム
    uint32_t _depthBucket1_2 = 0;
    uint32_t _depthBucket3_5 = 0;
    uint32_t _depthBucket6_10 = 0;
    uint32_t _depthBucket11plus = 0;
};

} // namespace dummy_peer
