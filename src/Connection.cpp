/* ============================================================================
 *  Connection.cpp  —  Connection.hpp 의 구현
 * ----------------------------------------------------------------------------
 *  핵심은 onReadable()/onWritable() 두 함수입니다. 둘 다 "poll 이 준비됐다고
 *  알려준 뒤에만" 호출되며, recv/send 의 반환값만 보고(절대 errno 안 봄) 판단합니다.
 *
 *  흐름:
 *    onReadable() 로 받은 바이트를 HttpRequest(_request)에 넣어 파싱 →
 *    요청이 완성되면 RequestHandler 로 응답을 만들어 _outBuf 에 담고 →
 *    onWritable() 로 _outBuf 를 끝까지 보낸 뒤 연결을 닫습니다.
 * ========================================================================== */

#include "Connection.hpp"
#include "RequestHandler.hpp"
#include "HttpResponse.hpp"

#include <sys/socket.h>     // recv, send

// 한 번에 소켓에서 읽어들일 최대 바이트 수.
static const std::size_t kReadChunk = 4096;

Connection::Connection(int fd, const std::vector<const ServerConfig *> &servers)
    : _fd(fd),
      _state(READING),
      _request(),
      _outBuf(),
      _outSent(0),
      _servers(servers)
{
    // 본문 최대 크기는 후보 server 들의 client_max_body_size 중 가장 큰 값을 적용
    // (Host 를 알기 전이라, 너무 빡빡하게 잡아 정상 요청을 거부하지 않도록 함).
    std::size_t maxBody = 0;
    for (std::size_t i = 0; i < _servers.size(); ++i)
        if (_servers[i]->clientMaxBodySize > maxBody)
            maxBody = _servers[i]->clientMaxBodySize;
    if (maxBody == 0)
        maxBody = 1048576;
    _request.setMaxBodySize(maxBody);
}

Connection::~Connection()
{
    // 소켓을 닫는 책임은 Server(생성/등록한 쪽)에 둡니다.
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

bool Connection::onReadable()
{
    char buf[kReadChunk];

    // poll 이 읽기 가능이라 했을 때만 여기 옴 → recv 1회.
    ssize_t n = recv(_fd, buf, sizeof(buf), 0);

    // 반환값만으로 판단(절대 errno 검사 안 함):
    //   n == 0 : 상대가 정상 종료,  n < 0 : 오류  → 둘 다 연결을 닫음
    if (n <= 0)
        return false;

    // 받은 바이트를 파서에 넣어 진행.
    _request.parse(buf, static_cast<std::size_t>(n));

    HttpRequest::ParseState st = _request.state();
    if (st == HttpRequest::PARSING)
        return true;            // 아직 더 받아야 함

    // 완성(COMPLETE) 또는 실패(ERROR) → 응답을 만들고 쓰기 단계로 전환.
    buildResponse();
    return true;
}

void Connection::buildResponse()
{
    RequestHandler handler(_servers);

    HttpResponse res;
    if (_request.state() == HttpRequest::COMPLETE)
        res = handler.handle(_request);
    else
        res = handler.buildError(_request.errorStatus());

    _outBuf  = res.toString();
    _outSent = 0;
    _state   = WRITING;
}

bool Connection::onWritable()
{
    std::size_t remaining = _outBuf.size() - _outSent;
    if (remaining == 0)
        return false;

    ssize_t n = send(_fd, _outBuf.data() + _outSent, remaining, 0);

    // recv 와 동일하게 반환값만 보고 판단.
    if (n <= 0)
        return false;

    _outSent += static_cast<std::size_t>(n);

    // 응답을 끝까지 보냈으면 연결을 닫습니다.
    // (지금은 keep-alive 미구현 → 응답 1개 후 종료. 다음 단계에서 확장 예정)
    if (_outSent >= _outBuf.size())
        return false;

    return true;
}
