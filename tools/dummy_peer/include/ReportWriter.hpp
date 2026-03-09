#pragma once
#include "DummyPeer.hpp"
#include <string>

namespace dummy_peer {

/**
 * テスト結果をJSON文字列に変換し、オプションでファイルに書き出す。
 * 外部JSONライブラリ非依存（手動フォーマット）。
 */
class ReportWriter {
public:
    /// TestResult を JSON 文字列に変換
    static std::string ToJson(const TestResult& result, const Config& config);

    /// JSON をファイルに書き出し
    static bool WriteToFile(const std::string& path, const std::string& json);
};

} // namespace dummy_peer
