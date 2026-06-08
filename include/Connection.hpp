/* ============================================================================
 *  Connection.hpp  —  "클라이언트 한 명과의 연결" 하나를 표현하는 클래스
 * ----------------------------------------------------------------------------
 *  손님 한 명당 연결(소켓) 하나가 생기고, 그 상태(요청 받는 중/CGI 실행 중/
 *  응답 보내는 중)와 버퍼를 이 클래스가 들고 있습니다.
 *
 *  중요한 채점 규칙(반드시 지킴):
 *   - recv/send/read/write 는 poll() 이 "준비됐다"고 알린 fd 에만 호출.
 *   - 그 뒤 errno 를 절대 검사하지 않고, 반환값<=0 이면 닫거나 종료로 처리.
 *
 *  한 연결이 여러 개의 fd 를 가질 수 있습니다:
 *   - 클라이언트 소켓 (항상)
 *   - CGI 실행 시: CGI 의 stdin(우리가 본문을 씀)·stdout(우리가 출력을 읽음) 파이프
 *  그래서 Server 에게 "내가 감시받고 싶은 fd 목록"을 pollFds() 로 알려주고,
 *  어떤 fd 에 이벤트가 나면 onEvent(fd) 로 처리합니다.
 * ========================================================================== */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "Config.hpp"
#include "HttpRequest.hpp"
#include "CgiHandler.hpp"

#include <string>
#include <vector>
#include <cstddef>
#include <poll.h>       // struct pollfd

class Connection
{
public:
    Connection(int fd, const std::vector<const ServerConfig *> &servers);
    ~Connection();

    // 이 연결의 클라이언트 소켓 fd.
    int  clientFd() const;

    // 이 연결이 감시받고 싶은 fd 들(소켓 + CGI 파이프)을 out 에 추가합니다.
    void pollFds(std::vector<struct pollfd> &out) const;

    // 어떤 fd 에 이벤트가 발생했을 때 호출. 반환값 false = 연결을 닫아야 함.
    bool onEvent(int fd, short revents);

    // poll 이 한가할 때(타임아웃마다) 주기적으로 호출 — CGI 폭주/멈춤 감시용.
    void onIdleTick();

private:
    // 연결의 단계.
    //   READING : 요청을 받는 중
    //   RUN_CGI : CGI 프로세스를 실행/통신하는 중
    //   WRITING : 응답을 보내는 중
    enum State { READING, RUN_CGI, WRITING };

    int                                 _fd;
    State                               _state;
    HttpRequest                         _request;
    std::string                         _outBuf;
    std::size_t                         _outSent;
    std::vector<const ServerConfig *>   _servers;

    // ---- CGI 상태 ----
    CgiHandler::Process                 _cgi;           // pid/inFd/outFd
    std::size_t                         _cgiBodySent;   // 본문 중 CGI 로 보낸 양
    std::string                         _cgiOut;        // CGI 출력 누적
    int                                 _cgiTicksLeft;  // 남은 타임아웃 틱

    // ---- 단계별 내부 처리 ----
    bool onClientReadable();        // 요청 바이트 수신 + 파싱
    bool onClientWritable();        // 응답 전송
    void routeRequest();            // 요청 완성/실패 후: CGI 시작 or 응답 준비
    bool onCgiWritable();           // CGI stdin 으로 본문 쓰기
    bool onCgiReadable();           // CGI stdout 읽기
    void finishCgi();               // CGI 종료 → 출력으로 응답 만들고 WRITING
    void reapCgi();                 // 자식 프로세스 회수(좀비 방지)
    void closeCgiFds();             // CGI 파이프 fd 닫기
    void setResponse(const std::string &serialized);    // 응답 세팅 후 WRITING 전환
    void failWith(int status);      // 상태코드로 즉시 응답(에러)

    Connection(const Connection &);
    Connection &operator=(const Connection &);
};

#endif // CONNECTION_HPP
