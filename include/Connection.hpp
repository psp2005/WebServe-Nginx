/* ============================================================================
 *  Connection.hpp  —  "클라이언트 한 명과의 연결" 하나를 표현하는 클래스
 * ----------------------------------------------------------------------------
 *  웹 서버는 동시에 여러 명의 손님(브라우저)을 상대합니다. 손님 한 명당
 *  연결(소켓) 하나가 생기는데, 그 연결의 상태(지금 요청을 받는 중인지, 답을
 *  보내는 중인지)와 주고받는 데이터(버퍼)를 이 클래스가 들고 있습니다.
 *
 *  중요한 채점 규칙(반드시 지킴):
 *   - recv()/send() 는 poll() 이 "준비됐다"고 알려줬을 때만 호출합니다.
 *   - recv()/send() 호출 뒤에 errno 를 절대 검사하지 않습니다.
 *     반환값이 0 이하(0=상대가 닫음, -1=오류)면 그냥 연결을 닫습니다.
 *
 *  요청을 받는 일은 HttpRequest(파서)에게, 그 요청으로 무엇을 돌려줄지는
 *  RequestHandler 에게 맡깁니다. Connection 은 그 사이에서 바이트를 나르고
 *  상태(읽기/쓰기)를 관리하는 '중개자' 역할입니다.
 * ========================================================================== */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "Config.hpp"
#include "HttpRequest.hpp"

#include <string>
#include <vector>
#include <cstddef>      // std::size_t

class Connection
{
public:
    // fd        : 이 클라이언트와 통신할 소켓 디스크립터
    // servers   : 이 연결이 들어온 listen 소켓에 묶인 server 블록들
    //             (Host 헤더로 가상호스트를 고를 때 사용)
    Connection(int fd, const std::vector<const ServerConfig *> &servers);
    ~Connection();

    int  fd() const;

    // poll 에 등록할 관심사: 지금 읽고 싶은가 / 쓰고 싶은가
    bool wantsRead() const;
    bool wantsWrite() const;

    // poll 이 "읽기 가능"이라고 알릴 때 호출. 반환값 false = 연결을 닫아야 함.
    bool onReadable();
    // poll 이 "쓰기 가능"이라고 알릴 때 호출. 반환값 false = 연결을 닫아야 함.
    bool onWritable();

private:
    // 연결의 두 가지 단계.
    //   READING : 아직 요청을 다 못 받음 → 더 읽어야 함
    //   WRITING : 응답을 만들어 두고 보내는 중
    enum State { READING, WRITING };

    int                                 _fd;
    State                               _state;
    HttpRequest                         _request;   // 요청 파서(상태를 들고 있음)
    std::string                         _outBuf;    // 보낼 응답 전체
    std::size_t                         _outSent;   // _outBuf 중 지금까지 보낸 바이트 수
    std::vector<const ServerConfig *>   _servers;   // 이 연결의 가상호스트 후보들

    // 요청이 완성/실패했을 때 응답 문자열을 만들어 _outBuf 에 채우고 쓰기 단계로.
    void buildResponse();

    // 복사 금지: 소켓 fd 와 버퍼 상태를 가진 객체라 복사 의미가 없습니다.
    Connection(const Connection &);
    Connection &operator=(const Connection &);
};

#endif // CONNECTION_HPP
