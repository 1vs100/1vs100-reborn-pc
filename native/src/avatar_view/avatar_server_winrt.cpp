// LAN page for replacing the Avatar from a phone or PC:
//   GET  /            upload page
//   GET  /avatar.png  current Avatar
//   PUT  /avatar      request body is avatarmanifest.bin or a sona code
// One request per connection, no TLS, reachable on the local network only.
#include "avatar_view.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <winrt/windows.foundation.h>
#include <winrt/windows.networking.sockets.h>
#include <winrt/windows.storage.streams.h>

#include <rex/logging.h>

#include "onboarding_host/lan_address.h"

namespace avatar_view {
namespace {

using winrt::Windows::Networking::Sockets::StreamSocket;
using winrt::Windows::Networking::Sockets::StreamSocketListener;
using winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InputStreamOptions;

constexpr uint16_t kPort = 8081;
constexpr size_t kMaxHeaderBytes = 8 * 1024;
constexpr size_t kMaxBodyBytes = 8 * 1024;  // a manifest is 1000 bytes, a sona code under 2 KB
constexpr uint64_t kMaxDrainBytes = 16 * 1024 * 1024;
constexpr std::chrono::seconds kDeadline{15};
constexpr int kMaxConnections = 8;

constexpr char kPage[] = R"(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>1 vs 100 Avatar</title>
<style>body{font-family:sans-serif;background:#12151c;color:#eee;max-width:34em;margin:2em auto;padding:0 1em}
a{color:#b28cff}img{display:block;background:#262930;width:288px;height:384px;margin:1em 0}
button,input{font-size:1em}textarea{width:100%;height:9em;font-family:monospace;font-size:.8em;
background:#1c1f28;color:#eee;border:1px solid #444;box-sizing:border-box}ol{padding-left:1.3em}
li{margin:.4em 0}.result{min-height:1.3em}</style></head>
<body><h1>Your Avatar</h1>
<p>This page changes the avatar the game shows for you.</p>
<ol>
<li>Create an account at <a href="https://360sona.com" target="_blank" rel="noopener">360sona.com</a> and sign in.</li>
<li>In the editor, click <b>My Sonas</b>, select your avatar and click <b>Import / Export</b>.</li>
<li>Copy the <b>360sona avatar</b> text, paste it below and press <b>Use code</b>.
Or click <b>Save the description (.bin)</b> and upload that file under Avatar file.</li>
</ol>
<p>Or enter your Xbox gamertag below to use the avatar 360sona has for it.</p>
<p>The preview shows your avatar once it is saved. The game uses it the next time it loads your avatar.</p>
<img id="preview" src="/avatar.png" alt="Avatar">
<h2>Sona code</h2>
<textarea id="code" placeholder="360sona avatar v1&#10;# Paste this whole thing ...&#10;AAAA..."></textarea>
<p><button id="sendCode">Use code</button></p>
<p class="result" id="codeResult"></p>
<h2>Avatar file</h2>
<input type="file" id="file"> <button id="sendFile">Upload</button>
<p class="result" id="fileResult"></p>
<h2>Xbox gamertag</h2>
<input id="gamertag" maxlength="64" placeholder="Gamertag"> <button id="sendGamertag">Use gamertag</button>
<p class="result" id="gamertagResult"></p>
<script>
async function send(body, result) {
  try {
    const response = await fetch('/avatar', {method: 'PUT', body: body});
    result.textContent = await response.text();
  } catch (error) {
    result.textContent = 'The console did not answer: ' + error;
  }
  document.getElementById('preview').src = '/avatar.png?' + Date.now();
}
document.getElementById('sendCode').onclick = () => {
  const code = document.getElementById('code').value.trim();
  const result = document.getElementById('codeResult');
  if (!code) { result.textContent = 'Paste a sona code first.'; return; }
  send(code, result);
};
document.getElementById('sendFile').onclick = () => {
  const file = document.getElementById('file').files[0];
  const result = document.getElementById('fileResult');
  if (!file) { result.textContent = 'Pick a file first.'; return; }
  send(file, result);
};
// This browser fetches the manifest, so the console needs no internet access.
// A plain GET needs no CORS preflight; 360sona allows any origin for it.
document.getElementById('sendGamertag').onclick = async () => {
  const gamertag = document.getElementById('gamertag').value.trim();
  const result = document.getElementById('gamertagResult');
  if (!gamertag) { result.textContent = 'Enter a gamertag first.'; return; }
  let manifest;
  try {
    const response = await fetch('https://360sona.com/sona/v1/gamertag/' + encodeURIComponent(gamertag) +
                                 '?client=1vs100rb&format=bin');
    if (!response.ok) {
      const reason = await response.json().then(body => body && body.error, () => null);
      result.textContent = reason || '360sona.com answered with status ' + response.status + '.';
      return;
    }
    manifest = await response.blob();
  } catch (error) {
    result.textContent = '360sona.com did not answer: ' + error;
    return;
  }
  send(manifest, result);
};
</script></body></html>
)";

std::atomic<int> g_connections{0};
std::mutex g_address_mutex;
std::string g_address;  // set once the listener is bound
StreamSocketListener g_listener{nullptr};

class Connection {
 public:
  explicit Connection(StreamSocket socket)
      : socket_(std::move(socket)), reader_(socket_.InputStream()), writer_(socket_.OutputStream()) {
    reader_.InputStreamOptions(InputStreamOptions::Partial);
  }

  // Appends what arrives before the deadline; false when the peer stops sending.
  bool ReadMore(std::string* data, std::chrono::steady_clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) return false;
    auto load = reader_.LoadAsync(4096);
    if (load.wait_for(left) != winrt::Windows::Foundation::AsyncStatus::Completed) {
      load.Cancel();
      return false;
    }
    const uint32_t loaded = load.GetResults();
    if (!loaded) return false;
    const size_t at = data->size();
    data->resize(at + loaded);
    reader_.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t*>(data->data() + at),
                                                 reinterpret_cast<uint8_t*>(data->data() + at + loaded)));
    return true;
  }

  void Send(int code, std::string_view reason, std::string_view type, std::string_view body) {
    const std::string head = "HTTP/1.1 " + std::to_string(code) + " " + std::string(reason) +
                             "\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
                             std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    const auto bytes = [](std::string_view text) {
      return winrt::array_view<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()),
                                              reinterpret_cast<const uint8_t*>(text.data()) + text.size());
    };
    writer_.WriteBytes(bytes(head));
    if (!body.empty()) writer_.WriteBytes(bytes(body));
    writer_.StoreAsync().get();
    writer_.FlushAsync().get();
  }

 private:
  StreamSocket socket_;
  DataReader reader_;
  DataWriter writer_;
};

std::optional<uint64_t> ContentLength(std::string_view head) {
  for (size_t at = head.find("\r\n"); at != std::string_view::npos; at = head.find("\r\n", at + 2)) {
    const std::string_view line = head.substr(at + 2, head.find("\r\n", at + 2) - (at + 2));
    constexpr std::string_view kName = "content-length:";
    if (line.size() < kName.size()) continue;
    bool match = true;
    for (size_t i = 0; i < kName.size() && match; ++i) match = std::tolower(uint8_t(line[i])) == kName[i];
    if (!match) continue;
    std::string_view value = line.substr(kName.size());
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    uint64_t length = 0;
    if (std::from_chars(value.data(), value.data() + value.size(), length).ec != std::errc()) return std::nullopt;
    return length;
  }
  return std::nullopt;
}

void Handle(StreamSocket socket) {
  Connection connection(std::move(socket));
  const auto deadline = std::chrono::steady_clock::now() + kDeadline;
  std::string data;
  size_t header_end;
  while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
    if (data.size() > kMaxHeaderBytes || !connection.ReadMore(&data, deadline)) return;
  }
  const std::string_view head(data.data(), header_end);
  const std::string_view request_line = head.substr(0, head.find("\r\n"));
  const size_t space = request_line.find(' ');
  const std::string_view method = request_line.substr(0, space);
  std::string_view target = request_line.substr(space + 1, request_line.find(' ', space + 1) - space - 1);
  target = target.substr(0, target.find('?'));
  if (method == "GET" && target == "/") {
    connection.Send(200, "OK", "text/html; charset=utf-8", kPage);
  } else if (method == "GET" && target == "/avatar.png") {
    const auto png = PreviewPng();
    if (png.empty()) {
      connection.Send(503, "Service Unavailable", "text/plain", "The avatar asset pack is not installed.");
    } else {
      connection.Send(200, "OK", "image/png", std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
    }
  } else if (method == "PUT" && target == "/avatar") {
    const auto length = ContentLength(head);
    if (!length || *length > kMaxBodyBytes) {
      // Read the body first: closing with it unread resets the connection and the browser never sees the answer.
      uint64_t drained = data.size() - (header_end + 4);
      while (length && drained < std::min<uint64_t>(*length, kMaxDrainBytes)) {
        std::string chunk;
        if (!connection.ReadMore(&chunk, deadline)) break;
        drained += chunk.size();
      }
      connection.Send(413, "Payload Too Large", "text/plain", "That is too large for an avatar file or sona code.");
      return;
    }
    std::string body = data.substr(header_end + 4);
    while (body.size() < *length) {
      if (!connection.ReadMore(&body, deadline)) return;
    }
    body.resize(*length);
    const auto result = Store(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    connection.Send(result.ok ? 200 : 400, result.ok ? "OK" : "Bad Request", "text/plain; charset=utf-8",
                    result.message);
  } else {
    connection.Send(404, "Not Found", "text/plain", "Unknown request.");
  }
}

}  // namespace

void StartUploadServer() {
  if (g_listener) return;
  g_listener = StreamSocketListener();
  g_listener.Control().KeepAlive(false);
  g_listener.ConnectionReceived([](const StreamSocketListener&,
                                    const StreamSocketListenerConnectionReceivedEventArgs& args) {
    if (g_connections.fetch_add(1) >= kMaxConnections) {
      g_connections.fetch_sub(1);
      return;
    }
    std::thread([socket = args.Socket()]() mutable {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      try {
        Handle(std::move(socket));
      } catch (...) {
        // A bad request must never take the game down.
      }
      g_connections.fetch_sub(1);
    }).detach();
  });
  // Binding waits on the network stack, so it runs off the caller's thread.
  std::thread([] {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
      g_listener.BindServiceNameAsync(winrt::to_hstring(kPort)).get();
      const auto addresses = onboarding::LanIPv4Addresses();
      std::lock_guard<std::mutex> lock(g_address_mutex);
      if (!addresses.empty()) g_address = addresses.front();
      for (const auto& address : addresses) REXKRNL_INFO("Avatar upload page: http://{}:{}/", address, kPort);
    } catch (...) {
    }
  }).detach();
}

std::string UploadUrl() {
  std::lock_guard<std::mutex> lock(g_address_mutex);
  return g_address.empty() ? std::string() : "http://" + g_address + ":" + std::to_string(kPort) + "/";
}

std::string UploadHint() {
  const std::string url = UploadUrl();
  return url.empty() ? std::string("The upload page is not reachable on this network.")
                     : "Open " + url +
                           " on a phone or PC on the same network to upload avatarmanifest.bin or use your Xbox "
                           "gamertag.";
}

}  // namespace avatar_view
