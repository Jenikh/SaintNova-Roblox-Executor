#include "saintnova/pipe_server.hpp"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace saintnova {

namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

constexpr DWORD kPipeBuffer = 64 * 1024;

// The pipe can execute arbitrary Luau and shut down the server. Give access
// only to the account running this process and LocalSystem.
PSECURITY_DESCRIPTOR make_pipe_security_descriptor() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;

    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (required == 0) {
        CloseHandle(token);
        return nullptr;
    }

    std::vector<std::uint8_t> token_user(required);
    if (!GetTokenInformation(token, TokenUser, token_user.data(), required, &required)) {
        CloseHandle(token);
        return nullptr;
    }
    CloseHandle(token);

    LPWSTR sid_text = nullptr;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(token_user.data());
    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text)) return nullptr;

    const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid_text + L")";
    LocalFree(sid_text);

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
    return descriptor;
}

// Issues one overlapped read or write and waits for it to finish.
//
// The handles are opened with FILE_FLAG_OVERLAPPED because Windows serialises
// all I/O on a synchronous file object: a blocked ReadFile would prevent the
// job thread from writing a log line, deadlocking the connection. Overlapped
// operations have no such coupling, so we use one event per operation and wait
// for it immediately.
bool overlapped_io(HANDLE pipe, bool read, void* buffer, DWORD size, DWORD& transferred) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) return false;

    transferred = 0;
    const BOOL issued = read ? ReadFile(pipe, buffer, size, &transferred, &overlapped)
                             : WriteFile(pipe, buffer, size, &transferred, &overlapped);

    bool ok = issued != FALSE;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) {
            DWORD completed = 0;
            ok = GetOverlappedResult(pipe, &overlapped, &completed, FALSE) != FALSE;
            transferred = completed;
        }
    }

    CloseHandle(overlapped.hEvent);
    return ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// PipeClient
// ---------------------------------------------------------------------------

PipeClient::PipeClient(PipeServer& server, std::uint64_t id, void* handle)
    : server_(server), id_(id), handle_(handle) {}

PipeClient::~PipeClient() {
    alive_ = false;
    join_job();
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

bool PipeClient::send(MessageType type, std::string_view payload) {
    std::string frame = encode_frame(type, payload);
    std::lock_guard<std::mutex> lock(write_mutex_);
    return write_all(frame.data(), frame.size());
}

bool PipeClient::send_error(const std::string& code, const std::string& message, const std::string& request_id) {
    std::string payload = "{\"code\":\"" + code + "\",\"message\":";
    // JSON-escape the message conservatively; messages are short and mostly
    // human-readable, but they can embed script text pulled from an error.
    payload.push_back('"');
    for (char c : message) {
        switch (c) {
            case '"': payload += "\\\""; break;
            case '\\': payload += "\\\\"; break;
            case '\n': payload += "\\n"; break;
            case '\r': payload += "\\r"; break;
            case '\t': payload += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    payload += '?';
                } else {
                    payload.push_back(c);
                }
        }
    }
    payload += "\"";
    if (!request_id.empty()) {
        payload += ",\"id\":\"" + request_id + "\"";
    }
    payload += "}";
    return send(MessageType::Error, payload);
}

bool PipeClient::write_all(const char* data, std::size_t size) {
    HANDLE pipe = static_cast<HANDLE>(handle_);
    std::size_t offset = 0;
    while (offset < size) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - offset, 1u << 20));
        DWORD written = 0;
        if (!overlapped_io(pipe, false, const_cast<char*>(data + offset), chunk, written) || written == 0) {
            alive_ = false;
            return false;
        }
        offset += written;
    }
    return true;
}

bool PipeClient::read_exact(void* buffer, std::size_t size) {
    HANDLE pipe = static_cast<HANDLE>(handle_);
    std::size_t offset = 0;
    auto* bytes = static_cast<char*>(buffer);
    while (offset < size) {
        DWORD received = 0;
        if (!overlapped_io(pipe, true, bytes + offset, static_cast<DWORD>(size - offset), received) || received == 0) {
            return false;
        }
        offset += received;
    }
    return true;
}

void PipeClient::reader_loop() {
    std::string payload;
    while (alive_) {
        std::uint8_t header_bytes[kFrameHeaderSize];
        if (!read_exact(header_bytes, sizeof(header_bytes))) break;

        FrameHeader header;
        if (!decode_header(header_bytes, sizeof(header_bytes), header)) break;

        payload.resize(header.length);
        if (header.length != 0 && !read_exact(payload.data(), header.length)) break;

        server_.dispatch(*this, header, payload);
    }

    alive_ = false;
    server_.notify_disconnect(*this);
}

bool PipeClient::try_begin_job() {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    // A Cancel frame that arrived while idle must not poison the next run.
    cancel_ = false;
    return true;
}

void PipeClient::end_job() {
    busy_ = false;
    cancel_ = false;
}

void PipeClient::spawn_job(std::function<void()> job) {
    join_job();
    job_ = std::thread([this, fn = std::move(job)]() mutable {
        fn();
        busy_ = false;
    });
}

void PipeClient::join_job() {
    if (job_.joinable()) job_.join();
}

// ---------------------------------------------------------------------------
// PipeServer
// ---------------------------------------------------------------------------

PipeServer::PipeServer() = default;

PipeServer::~PipeServer() {
    stop();
}

bool PipeServer::start(const std::string& name, FrameHandler handler, std::string& error) {
    if (running_) {
        error = "server already running";
        return false;
    }

    name_ = name;
    full_name_ = "\\\\.\\pipe\\" + name;
    handler_ = std::move(handler);

    // Probe for an existing instance with the same name; two servers on one
    // pipe name would silently round-robin clients.
    HANDLE probe = CreateFileW(widen(full_name_).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        error = "another SaintNova server is already listening on " + full_name_;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&PipeServer::accept_loop, this);
    return true;
}

void PipeServer::accept_loop() {
    const std::wstring full_name = widen(full_name_);

    PSECURITY_DESCRIPTOR descriptor = make_pipe_security_descriptor();
    if (descriptor == nullptr) {
        running_ = false;
        return;
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};

    while (running_) {
        HANDLE pipe = CreateNamedPipeW(full_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, kPipeBuffer, kPipeBuffer, 0, &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // ConnectNamedPipe requires an OVERLAPPED structure on this handle.
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &overlapped);
        DWORD last_error = connected ? ERROR_SUCCESS : GetLastError();

        if (!connected && last_error == ERROR_IO_PENDING) {
            if (WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) {
                DWORD ignored = 0;
                connected = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
                last_error = connected ? ERROR_SUCCESS : GetLastError();
            }
        }
        if (overlapped.hEvent != nullptr) CloseHandle(overlapped.hEvent);

        if (!running_) {
            CloseHandle(pipe);
            break;
        }
        if (!connected && last_error != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            continue;
        }

        std::uint64_t id = next_id_++;
        std::shared_ptr<PipeClient> client(new PipeClient(*this, id, pipe));
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[id] = client;
        }

        if (on_connect_) on_connect_(*client);

        std::thread([client]() { client->reader_loop(); }).detach();
    }
    LocalFree(descriptor);
}

void PipeServer::stop() {
    if (!running_.exchange(false)) return;

    // Unblock ConnectNamedPipe by connecting to ourselves, then drop it.
    HANDLE wake = CreateFileW(widen(full_name_).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);

    if (accept_thread_.joinable()) accept_thread_.join();

    std::vector<std::shared_ptr<PipeClient>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& entry : clients_) clients.push_back(entry.second);
        clients_.clear();
    }

    for (auto& client : clients) {
        client->alive_ = false;
        if (client->handle_ != nullptr) {
            CancelIoEx(static_cast<HANDLE>(client->handle_), nullptr);
        }
    }

    // Reader threads drop their shared_ptr on exit; give them a moment so the
    // process does not tear down sockets under a running thread.
    for (int i = 0; i < 50 && !clients.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        clients.erase(std::remove_if(clients.begin(), clients.end(),
                                     [](const std::shared_ptr<PipeClient>& c) { return !c->alive_; }),
                      clients.end());
    }
}

void PipeServer::broadcast(MessageType type, std::string_view payload) {
    std::vector<std::shared_ptr<PipeClient>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.reserve(clients_.size());
        for (auto& entry : clients_) clients.push_back(entry.second);
    }
    for (auto& client : clients) client->send(type, payload);
}

void PipeServer::dispatch(PipeClient& client, const FrameHeader& header, std::string_view payload) {
    if (handler_) handler_(client, header, payload);
}

void PipeServer::notify_disconnect(PipeClient& client) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(client.id());
    if (it != clients_.end()) clients_.erase(it);
}

std::string default_pipe_name() {
    return "saintnova-default";
}

}  // namespace saintnova
