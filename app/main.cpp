#include <iostream>
#include <string>
#include <sstream>
#include <limits>
#include "auth_client.h"
#include "../core/core.h"

#ifdef _WIN32
#include <windows.h>
#endif

static void disableConsoleEcho(bool disable) {
#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hStdin, &mode)) {
        if (disable) mode &= ~ENABLE_ECHO_INPUT;
        else mode |= ENABLE_ECHO_INPUT;
        SetConsoleMode(hStdin, mode);
    }
#endif
}

int main(int argc, char* argv[]) {
    // defaults
    int fps = 30;
    int width = 1280;
    int height = 720;
    bool audioEnabled = false;
    std::string productKey;
    std::string serverUrl = "http://127.0.0.1:8000";

    // Parse CLI
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fps" && i + 1 < argc) {
            fps = std::stoi(argv[++i]);
        } else if (arg == "--res" && i + 1 < argc) {
            std::string res = argv[++i];
            if (res == "720p") { width = 1280; height = 720; }
            else if (res == "1080p") { width = 1920; height = 1080; }
            else if (res == "1440p") { width = 2560; height = 1440; }
        } else if (arg == "--audio") {
            audioEnabled = true;
        } else if (arg == "--product-key" && i + 1 < argc) {
            productKey = argv[++i];
        } else if (arg == "--server" && i + 1 < argc) {
            serverUrl = argv[++i];
        }
    }

    AuthClient auth(serverUrl);

    bool authOk = false;

    // If product key provided try that first
    if (!productKey.empty()) {
        std::cout << "Using product key login..." << std::endl;
        authOk = auth.loginWithProductKey(productKey);
        if (!authOk) std::cerr << "Product key login failed." << std::endl;
    } else {
        // Try cached rec_token first (offline grace)
        std::string cached = auth.getCachedRecToken();
        if (!cached.empty()) {
            std::cout << "Cached rec_token found, attempting offline validation..." << std::endl;
            // validateEntitlement will accept cached token if accessToken missing
            if (auth.validateEntitlement()) {
                authOk = true;
            }
        }

        if (!authOk) {
            // Prompt user for credentials
            std::string email;
            std::string password;
            std::cout << "Email: ";
            std::getline(std::cin, email);
            std::cout << "Password: ";
            disableConsoleEcho(true);
            std::getline(std::cin, password);
            disableConsoleEcho(false);
            std::cout << std::endl;

            authOk = auth.login(email, password);
            if (!authOk) {
                std::cerr << "Login failed." << std::endl;
                return 1;
            }
        }
    }

    // Validate entitlement (fetch rec_token and cache it)
    if (!auth.validateEntitlement()) {
        std::cerr << "Entitlement validation failed. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Authentication and entitlement validated." << std::endl;

    // Initialize core and start capture pipeline
    Core core;
    if (!core.initialize(width, height, fps)) {
        std::cerr << "Failed to initialize core." << std::endl;
        return 1;
    }

    if (!core.start("recording.avi")) {
        std::cerr << "Failed to start capture pipeline." << std::endl;
        return 1;
    }

    std::cout << "Recording... Press ENTER to stop." << std::endl;
    std::string dummy;
    std::getline(std::cin, dummy);

    core.stop();
    std::cout << "Stopped." << std::endl;
    return 0;
}