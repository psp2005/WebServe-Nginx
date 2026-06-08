/* ============================================================================
 *  Connection.cpp  —  Connection.hpp 의 구현
 * ----------------------------------------------------------------------------
 *  흐름 요약:
 *    READING  : onClientReadable() 로 바이트를 받아 HttpRequest 에 파싱.
 *               요청이 완성되면 routeRequest() 가 호출됨.
 *    routeRequest():
 *               정적 요청이면 응답을 만들어 바로 WRITING.
 *               CGI 요청이면 CgiHandler::start() 로 프로세스를 띄우고 RUN_CGI.
 *    RUN_CGI  : onCgiWritable() 로 본문을 CGI stdin 에 흘려보내고,
 *               onCgiReadable() 로 CGI stdout 을 읽다가 EOF 면 finishCgi().
 *    WRITING  : onClientWritable() 로 응답을 끝까지 보낸 뒤 연결 종료.
 *
 *  파이프/소켓 입출력은 모두 poll 이 준비를 알린 뒤에만, 반환값만 보고 처리합니다.
 * ========================================================================== */

#include "Connection.hpp"
#include "RequestHandler.hpp"
#include "HttpResponse.hpp"

#include <sys/socket.h>     // recv, send
#include <unistd.h>         // read, write, close
#include <sys/wait.h>       // waitpid, WNOHANG
#include <signal.h>         // kill, SIGKILL

// 한 번에 읽고/쓸 최대 바이트 수.
static const std::size_t kChunk = 4096;
// CGI 출력 누적 상한(폭주 방어). 큰 본문을 그대로 돌려주는 CGI(예: 업로드
// echo)도 있으므로 넉넉히 둡니다. (현재는 출력을 메모리에 모았다가 보내는
// 방식 — 추후 스트리밍으로 바꾸면 이 상한과 메모리 사용을 줄일 수 있음.)
static const std::size_t kCgiMaxOutput = 200 * 1024 * 1024;
// CGI 타임아웃: onIdleTick 은 한가할 때 약 0.5초마다 호출되므로 60틱 ≈ 30초.
static const int kCgiTimeoutTicks = 60;

Connection::Connection(int fd, const std::vector<const ServerConfig *> &servers)
    : _fd(fd),
      _state(READING),
      _request(),
      _outBuf(),
      _outSent(0),
      _servers(servers),
      _cgi(),
      _cgiBodySent(0),
      _cgiOut(),
      _cgiTicksLeft(0)
{
    // 본문 최대 크기는 후보 server 들의 client_max_body_size 중 최대값.
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
    // CGI 가 돌고 있었다면 파이프를 닫고 자식을 회수합니다(소켓은 Server 가 닫음).
    closeCgiFds();
    reapCgi();
}

int Connection::clientFd() const
{
    return _fd;
}

/* ============================  poll 목록  ============================== */

void Connection::pollFds(std::vector<struct pollfd> &out) const
{
    struct pollfd p;
    p.revents = 0;

    if (_state == READING)
    {
        p.fd = _fd; p.events = POLLIN; p.revents = 0;
        out.push_back(p);
    }
    else if (_state == WRITING)
    {
        p.fd = _fd; p.events = POLLOUT; p.revents = 0;
        out.push_back(p);
    }
    else // RUN_CGI
    {
        // 클라이언트 소켓은 이벤트 0 으로 등록 — 연결이 끊기면 POLLHUP/ERR 로 알림.
        p.fd = _fd; p.events = 0; p.revents = 0;
        out.push_back(p);

        // 아직 보낼 본문이 남았으면 CGI stdin 에 쓰기 감시.
        if (_cgi.inFd != -1 && _cgiBodySent < _request.body().size())
        {
            p.fd = _cgi.inFd; p.events = POLLOUT; p.revents = 0;
            out.push_back(p);
        }
        // CGI 출력은 항상 읽기 감시.
        if (_cgi.outFd != -1)
        {
            p.fd = _cgi.outFd; p.events = POLLIN; p.revents = 0;
            out.push_back(p);
        }
    }
}

/* =============================  이벤트  =============================== */

bool Connection::onEvent(int fd, short revents)
{
    if (fd == _fd)
    {
        if (_state == READING)
            return onClientReadable();
        if (_state == WRITING)
            return onClientWritable();
        // RUN_CGI 중 클라이언트 이벤트는 '연결 끊김'만 관심.
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            closeCgiFds();
            reapCgi();
            return false;       // 손님이 떠남 → 연결 정리
        }
        return true;
    }

    if (fd == _cgi.inFd)
        return onCgiWritable();

    if (fd == _cgi.outFd)
        return onCgiReadable();

    return true;    // 모르는/이미 닫힌 fd
}

void Connection::onIdleTick()
{
    if (_state != RUN_CGI)
        return;
    if (_cgiTicksLeft > 0)
    {
        --_cgiTicksLeft;
        return;
    }
    // 시간 초과 → CGI 강제 종료 후 504 응답.
    closeCgiFds();
    reapCgi();
    failWith(504);
}

/* ===========================  클라이언트  ============================= */

bool Connection::onClientReadable()
{
    char buf[kChunk];
    ssize_t n = recv(_fd, buf, sizeof(buf), 0);
    if (n <= 0)
        return false;       // 0=정상종료, -1=오류 → 둘 다 닫음(errno 안 봄)

    _request.parse(buf, static_cast<std::size_t>(n));

    if (_request.state() == HttpRequest::PARSING)
        return true;        // 더 받아야 함

    routeRequest();
    return true;
}

bool Connection::onClientWritable()
{
    std::size_t remaining = _outBuf.size() - _outSent;
    if (remaining == 0)
        return false;

    ssize_t n = send(_fd, _outBuf.data() + _outSent, remaining, 0);
    if (n <= 0)
        return false;

    _outSent += static_cast<std::size_t>(n);
    if (_outSent >= _outBuf.size())
        return false;       // 응답 전송 완료 → 연결 종료(keep-alive 미구현)
    return true;
}

/* ============================  라우팅  =============================== */

void Connection::routeRequest()
{
    RequestHandler handler(_servers);

    // 파싱 실패면 바로 에러 응답.
    if (_request.state() == HttpRequest::ERROR)
    {
        setResponse(handler.buildError(_request.errorStatus()).toString());
        return;
    }

    RequestHandler::Route r = handler.route(_request);

    // 정적 처리: 이미 만들어진 응답을 그대로 전송.
    if (!r.isCgi)
    {
        setResponse(r.response.toString());
        return;
    }

    // CGI 처리: 프로세스를 띄운다.
    CgiHandler::Process proc;
    if (!CgiHandler::start(_request, r.scriptPath, r.interpreter, *r.server, proc))
    {
        failWith(500);      // 파이프/fork 실패
        return;
    }

    _cgi          = proc;
    _cgiBodySent  = 0;
    _cgiOut.clear();
    _cgiTicksLeft = kCgiTimeoutTicks;
    _state        = RUN_CGI;

    // 보낼 본문이 없으면 stdin 을 바로 닫아 CGI 에 EOF 를 알린다.
    if (_request.body().empty() && _cgi.inFd != -1)
    {
        close(_cgi.inFd);
        _cgi.inFd = -1;
    }
}

/* ==============================  CGI  ================================ */

bool Connection::onCgiWritable()
{
    const std::string &body = _request.body();
    std::size_t remaining = body.size() - _cgiBodySent;

    if (remaining == 0)     // 다 보냄 → stdin 닫아 EOF
    {
        if (_cgi.inFd != -1) { close(_cgi.inFd); _cgi.inFd = -1; }
        return true;
    }

    ssize_t n = write(_cgi.inFd, body.data() + _cgiBodySent, remaining);
    if (n <= 0)             // 파이프 오류 → stdin 닫고 받은 데까지로 진행(errno 안 봄)
    {
        if (_cgi.inFd != -1) { close(_cgi.inFd); _cgi.inFd = -1; }
        return true;
    }

    _cgiBodySent += static_cast<std::size_t>(n);
    if (_cgiBodySent >= body.size())
    {
        if (_cgi.inFd != -1) { close(_cgi.inFd); _cgi.inFd = -1; }
    }
    return true;
}

bool Connection::onCgiReadable()
{
    char buf[kChunk];
    ssize_t n = read(_cgi.outFd, buf, sizeof(buf));

    if (n > 0)
    {
        _cgiOut.append(buf, static_cast<std::size_t>(n));
        if (_cgiOut.size() > kCgiMaxOutput)     // 출력 폭주 → 종료하고 502
        {
            closeCgiFds();
            reapCgi();
            failWith(502);
        }
        return true;
    }

    // n == 0(EOF) 또는 n < 0(오류) → CGI 종료로 간주.
    finishCgi();
    return true;
}

void Connection::finishCgi()
{
    closeCgiFds();
    reapCgi();

    HttpResponse res;
    if (!_cgiOut.empty())
        res = CgiHandler::buildResponse(_cgiOut);
    else
    {
        // 출력이 전혀 없음 → 보통 스크립트 실행 실패(execve 실패 등) → 502.
        RequestHandler handler(_servers);
        res = handler.buildError(502);
    }
    setResponse(res.toString());
}

void Connection::reapCgi()
{
    if (_cgi.pid == -1)
        return;
    int status = 0;
    pid_t r = waitpid(_cgi.pid, &status, WNOHANG);
    if (r == 0)
    {
        // 아직 살아있으면 강제 종료 후 회수(좀비/행 방지).
        kill(_cgi.pid, SIGKILL);
        waitpid(_cgi.pid, &status, 0);
    }
    _cgi.pid = -1;
}

void Connection::closeCgiFds()
{
    if (_cgi.inFd != -1)  { close(_cgi.inFd);  _cgi.inFd = -1; }
    if (_cgi.outFd != -1) { close(_cgi.outFd); _cgi.outFd = -1; }
}

/* ============================  응답 세팅  ============================= */

void Connection::setResponse(const std::string &serialized)
{
    _outBuf  = serialized;
    _outSent = 0;
    _state   = WRITING;
}

void Connection::failWith(int status)
{
    RequestHandler handler(_servers);
    setResponse(handler.buildError(status).toString());
}
