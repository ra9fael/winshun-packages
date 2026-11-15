#include "winshun.h"
#include <cstdio>
#include <thread>

class TestPlugin : public winshun::Plugin {
public:
    winshun::Items query(const winshun::Query&) override { ++queries; return {}; }
    int queries = 0;
};

int main()
{
    HANDLE inputRead, inputWrite, outputRead, outputWrite;
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) ||
        !CreatePipe(&outputRead, &outputWrite, nullptr, 0))
        return 1;
    TestPlugin plugin;
    // Set pipe endpoints before issuing GUI-originated asynchronous requests.
    // serve() is run below after responses have been supplied by the fake host.
    int callbacks = 0;
    bool ok = true;
    std::thread server([&] { plugin.serve(inputRead, outputWrite); });
    // Wait for initialize's reply: this also establishes that the endpoints
    // have been installed before sending tool requests from another thread.
    winshun::detail::writeLine("{\"id\":100,\"method\":\"initialize\",\"params\":{}}", inputWrite);
    winshun::detail::LineReader output(outputRead);
    std::string line;
    ok = output.next(line) && line.find("protocolVersion") != std::string::npos;
    plugin.toolAsync("clipboard_copy", winshun::Json::Object {{"text", "first"}},
                     [&](winshun::Json result) { ok = ok && !result.has("error"); ++callbacks; });
    plugin.toolAsync("clipboard_copy", winshun::Json::Object {{"text", "second"}},
                     [&](winshun::Json result) {
                         ok = ok && result["error"]["message"].toString() == "permission denied";
                         ++callbacks;
                     });
    plugin.toolAsync("clipboard_copy", winshun::Json::Object {{"text", "third"}},
                     [&](winshun::Json result) { ok = ok && result.has("error"); ++callbacks; });
    for (int i = 0; i < 3; ++i) {
        ok = output.next(line) && ok;
        const auto request = winshun::Json::parse(line);
        ok = ok && request && (*request)["params"]["name"].toString() == "clipboard_copy";
    }
    // Error and success responses arrive out of order, then a duplicate.
    winshun::detail::writeLine("{\"id\":2,\"error\":{\"message\":\"permission denied\"}}", inputWrite);
    winshun::detail::writeLine("{\"id\":1,\"result\":{}}", inputWrite);
    winshun::detail::writeLine("{\"id\":1,\"result\":{}}", inputWrite);
    winshun::detail::writeLine("{\"id\":101,\"method\":\"query\",\"params\":{}}", inputWrite);
    CloseHandle(inputWrite); // Third request must fail rather than succeed.
    server.join();
    ok = ok && callbacks == 3 && plugin.queries == 1;
    CloseHandle(inputRead);
    CloseHandle(outputRead);
    CloseHandle(outputWrite);
    std::printf("async clipboard protocol: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
