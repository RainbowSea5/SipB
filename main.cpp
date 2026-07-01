#include <iostream>
#include <csignal>
#include <atomic>

#include "GB28181/Gb28181Client.h"
#include "SipB/SipClient.h"
static std::atomic<bool> g_running(true);

void signalHandler(int sig) {
    WarnL << "\n[Main] Caught signal " << sig << ", shutting down...";
    g_running = false;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    auto client = std::make_shared<gb28181::Gb28181Client>(nullptr);

    client->setOnInit([]() {

    });
    client->setOnRegister([](bool s, const std::string& reason) {
        if (s) {
            InfoL << "[Callback] === Registration SUCCESS ===";
        } else {
            InfoL << "[Callback] === Registration FAILED: " << reason << " ===";
        }
    });
    client->setOnQueryDeviceInfo([](std::function<void(gb28181::DeviceInfo& info)> invoker) {
        if (invoker) {
            gb28181::DeviceInfo info("WCT","1.0.0","1.0.0");
            invoker(info);
        }
    });
    client->setOnCatalogQuery([](std::function<void(std::vector<gb28181::DeviceInfo>& ,bool)> invoker ) {
       if (invoker) {
           std::vector<gb28181::DeviceInfo> list;
           {
               gb28181::DeviceInfo info("wct","1.0.0",
                   "f1.0.0","55555555555555555557","Video");
               list.push_back(info);
           }
           {
               gb28181::DeviceInfo info2("wct","1.0.0",
                   "f1.0.0","55555555588555555557","Video2");
               list.push_back(info2);
           }
           invoker(list,false);
       }
    });
    // ========= 1. 初始化 eXosip，监听本地 UDP 5060 端口 =========
    if (!client->init(true,"wct")) {
        ErrorL << "[Main] Failed to initialize SIP stack";
        return -1;
    }

    // ========= 2. 向 SIP 服务器发起注册 =========
    // 请根据实际网络环境修改以下参数
    bool ok = client->startRegister(
        "192.168.10.188",
        15060,
        "55555555555555555557",
        "41010500000000000002",
        "4101050000",
        "55555555555555555557",
        "123456",
        3600
    );

    if (!ok) {
        std::cerr << "[Main] Failed to start registration";
        return -1;
    }

    InfoL << "[Main] SIP registration client started.";
    InfoL << "[Main] Press Ctrl+C to stop.";

    // ========= 3. 主循环（保持进程运行） =========
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // ========= 4. 停止注册并清理 =========
    // sipReg.stop();
    InfoL << "[Main] Client stopped.";
    return 0;
}
