/* ============================================================================
 *  CgiHandler.hpp  —  CGI(외부 프로그램으로 요청 처리) 실행기
 * ----------------------------------------------------------------------------
 *  CGI 가 뭔가요?
 *   "특정 확장자(.py 등)의 요청이 오면, 그 파일을 정적으로 보내는 대신
 *    인터프리터(python3)로 '실행'해서, 그 프로그램이 찍어낸 출력을 응답으로
 *    돌려주는" 방식입니다. 그래서 게시판·로그인 같은 '동적' 기능이 가능합니다.
 *
 *  통신 방법(파이프):
 *   - 서버 → CGI : 요청 본문(POST 데이터)을 CGI 의 표준입력(stdin)으로 흘려보냄.
 *   - CGI → 서버 : CGI 가 표준출력(stdout)에 찍은 내용을 서버가 읽음.
 *   - 요청 정보(메서드/경로/헤더 등)는 '환경변수'로 전달합니다(CGI/1.1 규약).
 *
 *  이 클래스는 두 가지 정적 기능만 제공합니다(상태를 들고 있지 않음):
 *   1) start()        : 파이프를 만들고 fork+execve 로 CGI 를 띄움.
 *                       부모는 논블로킹 파이프 fd 2개와 자식 PID 를 돌려받음.
 *   2) buildResponse(): CGI 가 찍어낸 출력(헤더+본문)을 HttpResponse 로 변환.
 *
 *  실제 파이프 읽기/쓰기와 poll 등록은 Connection 이 담당합니다.
 *  (채점 규칙: CGI 파이프도 단일 poll 로 감시하며 논블로킹으로 처리)
 * ========================================================================== */

#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "Config.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

#include <string>
#include <sys/types.h>      // pid_t

class CgiHandler
{
public:
    // 띄운 CGI 프로세스의 손잡이.
    struct Process
    {
        pid_t pid;      // 자식 프로세스 번호 (-1 = 없음)
        int   inFd;     // 부모가 본문을 써넣을 파이프(자식 stdin) (-1 = 닫힘)
        int   outFd;    // 부모가 출력을 읽을 파이프(자식 stdout) (-1 = 닫힘)
        Process() : pid(-1), inFd(-1), outFd(-1) {}
    };

    // CGI 를 실행합니다.
    //   req         : 환경변수로 넘길 요청 정보
    //   scriptPath  : 실행할 스크립트의 실제 파일 경로
    //   interpreter : 인터프리터 경로(예: /usr/bin/python3)
    //   server      : SERVER_NAME/SERVER_PORT 환경변수용
    //   out         : 성공 시 pid/inFd/outFd 가 채워짐
    // 성공하면 true. 파이프/fork 실패 시 false.
    static bool start(const HttpRequest &req,
                      const std::string &scriptPath,
                      const std::string &interpreter,
                      const ServerConfig &server,
                      Process &out);

    // CGI 가 찍어낸 원본 출력을 HTTP 응답으로 변환합니다.
    // (앞부분의 "Name: value" 헤더들과 빈 줄 뒤 본문을 구분해 처리. 'Status:'
    //  헤더가 있으면 상태코드로 사용, Content-Type 없으면 기본값을 채움.)
    static HttpResponse buildResponse(const std::string &cgiOutput);

private:
    // 요청/설정으로부터 CGI 환경변수 목록("KEY=VALUE")을 만듭니다.
    static std::vector<std::string> buildEnv(const HttpRequest &req,
                                             const std::string &scriptPath,
                                             const ServerConfig &server);
};

#endif // CGIHANDLER_HPP
