#include "INetApi.h"
#include "JsonCpp/json.hpp"
#include "Excel/book.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

namespace {
struct Session final : Foundation::ITcpSession {
    int received = 0;
    bool deleteOnReceive = false;
    int OnReceive(const void*, int size) override {
        if (deleteOnReceive) {
            delete this;
            return size;
        }
        received += size;
        return size;
    }
};

struct ThrowingWriter final : Json::StreamWriter {
    static inline int alive = 0;
    ThrowingWriter() { ++alive; }
    ~ThrowingWriter() override { --alive; }
    int write(const Json::Value&, Json::OStream*) override {
        throw std::runtime_error("writer failure");
    }
};
struct WriterFactory final : Json::StreamWriter::Factory {
    Json::StreamWriter* newStreamWriter() const override { return new ThrowingWriter; }
};
struct ThrowingReader final : Json::CharReader {
    static inline int alive = 0;
    ThrowingReader() { ++alive; }
    ~ThrowingReader() override { --alive; }
    bool parse(const char*, const char*, Json::Value*, Json::String*) override {
        throw std::runtime_error("reader failure");
    }
};
struct ReaderFactory final : Json::CharReader::Factory {
    Json::CharReader* newCharReader() const override { return new ThrowingReader; }
};
}

int main(int argc, char** argv) {
    using Foundation::ITcpSession;
    using Foundation::eTcpSessionEvent;
    Session* session = new Session;
    const auto oldHandle = session->CaptureDispatchHandle();
    int consumed = -1;
    assert(ITcpSession::DispatchReceive(oldHandle, "a", 1, consumed));
    assert(consumed == 1 && session->received == 1);
    delete session;
    assert(!ITcpSession::DispatchReceive(oldHandle, "a", 1, consumed));
    assert(consumed == 0);
    assert(!ITcpSession::DispatchTcpSessionEvent(oldHandle, eTcpSessionEvent::Connected));

    session = new Session;
    const auto handle = session->CaptureDispatchHandle();
    assert(handle != oldHandle);
    bool laterCallbackRan = false;
    session->RegistTcpSessionEventCallback<Session>(eTcpSessionEvent::Connected,
        [](Session* value) { delete value; });
    session->RegistTcpSessionEventCallback<Session>(eTcpSessionEvent::Connected,
        [&laterCallbackRan](Session*) { laterCallbackRan = true; });
    assert(ITcpSession::DispatchTcpSessionEvent(handle, eTcpSessionEvent::Connected));
    assert(!laterCallbackRan);
    assert(!ITcpSession::DispatchTcpSessionEvent(handle, eTcpSessionEvent::Connected));

    session = new Session;
    const auto receiveHandle = session->CaptureDispatchHandle();
    session->deleteOnReceive = true;
    assert(ITcpSession::DispatchReceive(receiveHandle, "abc", 3, consumed));
    assert(consumed == 3);
    assert(!ITcpSession::DispatchReceive(receiveHandle, "abc", 3, consumed));

    // Callbacks on one live Session are serialized; destruction follows joins.
    session = new Session;
    const auto concurrentHandle = session->CaptureDispatchHandle();
    auto receive = [concurrentHandle]() {
        for (int i = 0; i < 1000; ++i) {
            int count;
            assert(ITcpSession::DispatchReceive(concurrentHandle, "x", 1, count));
        }
    };
    std::thread first(receive), second(receive);
    first.join();
    second.join();
    assert(session->received == 2000);
    delete session;

    session = new Session;
    const auto throwingHandle = session->CaptureDispatchHandle();
    session->RegistTcpSessionEventCallback<Session>(eTcpSessionEvent::Connected,
        [](Session* value) {
            delete value;
            throw std::runtime_error("callback failure");
        });
    try {
        ITcpSession::DispatchTcpSessionEvent(throwingHandle, eTcpSessionEvent::Connected);
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(!ITcpSession::DispatchTcpSessionEvent(throwingHandle, eTcpSessionEvent::Connected));

    Json::Value original;
    original["answer"] = 42;
    original.setComment("// original", Json::commentBefore);
    Json::Value copy(original);
    copy.setComment("// copied", Json::commentBefore);
    assert(original.getComment(Json::commentBefore) == "// original");
    Json::Value assigned;
    assigned.setComment("// replaced", Json::commentBefore);
    assigned = copy;
    assert(assigned.getComment(Json::commentBefore) == "// copied");
    Json::Value moved(std::move(assigned));
    assigned = std::move(copy);
    assert(moved["answer"].asInt() == 42);
    assigned = assigned;
    assigned = std::move(assigned);
    assert(assigned["answer"].asInt() == 42);
    std::ostringstream output;
    output << original;
    std::istringstream input(output.str());
    Json::Value parsed;
    input >> parsed;
    assert(parsed["answer"].asInt() == 42);
    assert(!Json::writeString(Json::StreamWriterBuilder(), original).empty());

    try {
        Json::writeString(WriterFactory(), original);
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(ThrowingWriter::alive == 0);
    try {
        std::istringstream text("{}");
        Json::String errors;
        Json::parseFromStream(ReaderFactory(), text, &parsed, &errors);
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(ThrowingReader::alive == 0);
#if defined(__APPLE__) || defined(__linux__)
    if (argc > 1) {
        void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        assert(module);
        using Create = ITcpSession* (*)();
        Create create = reinterpret_cast<Create>(dlsym(module, "CreateManualOwnershipSession"));
        assert(create);
        ITcpSession* moduleSession = create();
        const auto moduleHandle = moduleSession->CaptureDispatchHandle();
        assert(ITcpSession::DispatchReceive(moduleHandle, "dll", 3, consumed));
        assert(consumed == 3);
        delete moduleSession;
        assert(!ITcpSession::DispatchReceive(moduleHandle, "dll", 3, consumed));
        dlclose(module);
    }
#endif

}
