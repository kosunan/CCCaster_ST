#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <thread>

namespace cccaster::network {

    // UDPソケットの低レイヤ通信をカプセル化したインターフェースクラス
    class UdpSocket {
    public:
        // 第1引数: ペイロード, 第2引数: 送信元IP, 第3引数: 送信元Port
        using ReceiveCallback = std::function<void(const std::vector<uint8_t>&, const std::string&, uint16_t)>;

        // 待受用ポートを指定してインスタンス化
        UdpSocket(uint16_t bindPort, bool isIpv6 = false);
        ~UdpSocket();

        // ソケットが正常にバインドされているか確認
        bool IsValid() const;

        // 指定のIP文字列が有効なIPv4/IPv6アドレスか検証
        static bool IsValidIpAddress(const std::string& ip, bool isIpv6);
        
        // 現在バインドされているポートを取得 (0指定時に動的割当されたポートを知るため)
        uint16_t GetPort() const;
        
        // パケットの非同期(またはノンブロッキング)送信
        void Send(const std::string& targetIp, uint16_t targetPort, const std::vector<uint8_t>& data);
        
        // パケットを受信した際に呼ばれるコールバックの登録
        void OnReceive(ReceiveCallback callback);
        
        // イベントループでの受信ポーリング（レガシーな手動Pollは廃止し、バックグラウンドスレッドで自動受信）
        // void Poll(); // Removed

    private:
        uint16_t _port;
        ReceiveCallback _onReceive;

        // asio::io_context や socket 等の実態は、PimplイディオムかCpp側での隠蔽を行う
        // ここでは実装依存を避けるため後ほど cpp ファイル側で定義
        struct Impl;
        Impl* _impl; 
    };

} // namespace cccaster::network
