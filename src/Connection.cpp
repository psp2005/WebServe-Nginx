/* ============================================================================
 *  Connection.cpp  —  Connection.hpp 의 구현
 * ----------------------------------------------------------------------------
 *  핵심은 onReadable()/onWritable() 두 함수입니다. 둘 다 "poll 이 준비됐다고
 *  알려준 뒤에만" 호출되며, recv/send 의 반환값만 보고(절대 errno 안 봄) 판단합니다.
 * ========================================================================== */

#include "Connection.hpp"

#include <sstream>          // std::ostringstream
#include <sys/socket.h>     // recv, send

// 한 번에 소켓에서 읽어들일 최대 바이트 수.
static const std::size_t kReadChunk = 4096;
// 요청 헤더가 비정상적으로 클 때(공격/실수) 방어하기 위한 상한.
// 이 크기를 넘도록 헤더 끝이 안 오면 연결을 끊습니다.
static const std::size_t kMaxRequestBytes = 64 * 1024;

Connection::Connection(int fd, const std::vector<const ServerConfig *> &servers)
    : _fd(fd),
      _state(READING),
      _inBuf(),
      _outBuf(),
      _outSent(0),
      _servers(servers)
{
}

Connection::~Connection()
{
    // 소켓을 닫는 책임은 Server(생성/등록한 쪽)에 둡니다.
    // 여기서 close 하면 누가 닫았는지 헷갈리므로 Connection 은 닫지 않습니다.
}

int Connection::fd() const
{
    return _fd;
}

bool Connection::wantsRead() const
{
    return _state == READING;
}

bool Connection::wantsWrite() const
{
    return _state == WRITING;
}

bool Connection::requestComplete() const
{
    // HTTP 요청에서 헤더의 끝은 빈 줄, 즉 "\r\n\r\n" 입니다.
    return _inBuf.find("\r\n\r\n") != std::string::npos;
}

bool Connection::onReadable()
{
    char buf[kReadChunk];

    // poll 이 읽기 가능이라 했을 때만 여기 옵니다 → recv 1회 호출.
    ssize_t n = recv(_fd, buf, sizeof(buf), 0);

    // 반환값만으로 판단(절대 errno 검사 안 함):
    //   n == 0 : 상대가 정상적으로 연결을 닫음
    //   n <  0 : 오류 → 그래도 그냥 연결을 닫음
    if (n <= 0)
        return false;

    _inBuf.append(buf, static_cast<std::size_t>(n));

    // 헤더가 끝났으면 응답을 만들고 쓰기 단계로 전환.
    if (requestComplete())
    {
        buildStubResponse();
        _state   = WRITING;
        _outSent = 0;
        return true;
    }

    // 아직 헤더 끝이 안 왔는데 버퍼가 너무 커지면 비정상 → 연결 종료.
    if (_inBuf.size() > kMaxRequestBytes)
        return false;

    // 더 읽어야 함(아직 READING 유지).
    return true;
}

bool Connection::onWritable()
{
    // 보낼 데이터가 남아 있는 만큼 한 번 send 시도.
    std::size_t remaining = _outBuf.size() - _outSent;
    if (remaining == 0)
        return false;   // 보낼 게 없으면 연결 정리(정상적으로는 도달하지 않음)

    ssize_t n = send(_fd, _outBuf.data() + _outSent, remaining, 0);

    // recv 와 동일하게 반환값만 보고 판단.
    if (n <= 0)
        return false;

    _outSent += static_cast<std::size_t>(n);

    // 응답을 끝까지 다 보냈으면 연결을 닫습니다.
    // (지금은 keep-alive 미구현 → 응답 1개 후 종료. 다음 단계에서 확장 예정)
    if (_outSent >= _outBuf.size())
        return false;

    // 아직 덜 보냄 → 다음 쓰기 기회를 기다림.
    return true;
}

void Connection::buildStubResponse()
{
    // ── 임시 응답 ──
    // 아직 HTTP 파서/핸들러가 없으므로, 요청 내용과 무관하게 고정 페이지를
    // 돌려줍니다. 이 단계의 목적은 "소켓 + 단일 poll 루프"가 실제로
    // 연결을 받고, 읽고, 응답을 끝까지 보내는지 검증하는 것입니다.
    std::string host = "default";
    if (!_servers.empty() && !_servers[0]->serverNames.empty())
        host = _servers[0]->serverNames[0];

    std::string body =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>webserv</title></head>"
        "<body><h1>webserv 동작 확인</h1>"
        "<p>소켓 + 단일 poll() 루프가 정상 동작하고 있습니다.</p>"
        "<p>이 응답은 아직 HTTP 파서/핸들러 연결 전의 임시 페이지입니다.</p>"
        "</body></html>";

    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Server: webserv\r\n"
         << "X-Vhost: " << host << "\r\n"
         << "Content-Type: text/html; charset=utf-8\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;

    _outBuf = resp.str();
}
