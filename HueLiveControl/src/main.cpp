#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef _WIN32
    // Geode juz wciaga winsock.h przez windows.h, wiec uzywamy go (bez winsock2.h)
    #include <winsock.h>
    using sock_t = SOCKET;
    #define CLOSESOCK closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using sock_t = int;
    #define CLOSESOCK close
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>

using namespace geode::prelude;

namespace {
    constexpr int   PORT  = 6722;
    constexpr float START = 0.30f;
    constexpr float STEP  = 0.05f;
    constexpr float MIN_S = 0.10f;
    constexpr float MAX_S = 3.00f;

    std::atomic<float> g_speed{START};

    void applySpeed() {
        CCDirector::get()->getScheduler()->setTimeScale(g_speed.load());
    }

    // Wykonywane ZAWSZE na glownym watku gry
    void runCommand(std::string const& cmd) {
        float s = g_speed.load();

        if (cmd == "reset") {
            if (auto pl = PlayLayer::get()) pl->resetLevel();
        }
        else if (cmd == "up")   s = std::min(MAX_S, s + STEP);
        else if (cmd == "down") s = std::max(MIN_S, s - STEP);
        else if (cmd == "zero") s = START;   // wroc do predkosci startowej

        s = std::round(s * 100.f) / 100.f;
        g_speed = s;
        applySpeed();
    }

    std::string handleRequest(std::string const& req) {
        // "GET /reset HTTP/1.1"
        if (req.rfind("GET /", 0) != 0) return "bad request";
        auto end = req.find(' ', 5);
        if (end == std::string::npos) return "bad request";
        std::string path = req.substr(5, end - 5);

        if (path == "reset" || path == "up" || path == "down" || path == "zero") {
            Loader::get()->queueInMainThread([path] { runCommand(path); });
            return "ok " + path;
        }
        if (path == "status") {
            return "speed " + std::to_string(g_speed.load());
        }
        return "unknown command";
    }

    void serverLoop() {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        sock_t srv = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(PORT);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // tylko ten komputer

        if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(srv, 8) != 0) {
            log::error("Nie mozna uruchomic serwera na porcie {} (zajety?)", PORT);
            CLOSESOCK(srv);
            return;
        }
        log::info("Serwer komend dziala: http://127.0.0.1:{}/", PORT);

        while (true) {
            sock_t cli = ::accept(srv, nullptr, nullptr);
            if (cli == INVALID_SOCKET) continue;

            char buf[1024];
            int n = ::recv(cli, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = 0;
                std::string body = handleRequest(std::string(buf));
                std::string resp =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                ::send(cli, resp.c_str(), static_cast<int>(resp.size()), 0);
            }
            CLOSESOCK(cli);
        }
    }
}

$on_mod(Loaded) {
    std::thread(serverLoop).detach();
}

class $modify(HuePlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto win = CCDirector::get()->getWinSize();
        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setScale(0.5f);
        label->setPosition({win.width / 2.f, win.height - 40.f});
        m_uiLayer->addChild(label, 100);
        m_fields->label = label;

        applySpeed();   // gra zeruje predkosc przy wejsciu do levelu
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // pilnuj predkosci co klatke (gra potrafi ja zresetowac do 1x)
        auto sched = CCDirector::get()->getScheduler();
        if (std::abs(sched->getTimeScale() - g_speed.load()) > 0.001f) {
            applySpeed();
        }

        if (m_fields->label) {
            m_fields->label->setString(fmt::format("SPEED: {:.2f}x", g_speed.load()).c_str());
        }
    }
};
