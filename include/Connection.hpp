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
 *  이 클래스는 소켓을 직접 poll 에 등록하지 않습니다. 그 일은 Server 가 하고,
 *  Connection 은 "지금 읽고 싶은지(wantsRead)/쓰고 싶은지(wantsWrite)"만 알려줍니다.
 * ========================================================================== */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "Config.hpp"

#include <string>
#include <vector>
#include <cstddef>      // std::size_t

class Connection
{
public:
    // fd        : 이 클라이언트와 통신할 소켓 디스크립터
    // servers   : 이 연결이 들어온 listen 소켓에 묶인 server 블록들
    //             (나중에 Host 헤더로 가상호스트를 고를 때 사용)
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
    std::string                         _inBuf;     // 받은 요청을 모으는 버퍼
    std::string                         _outBuf;    // 보낼 응답 전체
    std::size_t                         _outSent;   // _outBuf 중 지금까지 보낸 바이트 수
    std::vector<const ServerConfig *>   _servers;   // 이 연결에서 후보가 되는 가상호스트들

    // 요청 헤더의 끝(빈 줄, "\r\n\r\n")이 도착했는지 검사.
    bool requestComplete() const;

    // (임시) 고정 응답을 만들어 _outBuf 에 채웁니다.
    // 다음 단계에서 HttpRequest 파싱 + RequestHandler 로 교체될 자리입니다.
    void buildStubResponse();

    // 복사 금지: 소켓 fd 와 버퍼 상태를 가진 객체라 복사 의미가 없습니다.
    Connection(const Connection &);
    Connection &operator=(const Connection &);
};

#endif // CONNECTION_HPP
