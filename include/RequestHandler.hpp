/* ============================================================================
 *  RequestHandler.hpp  —  "해석된 요청"을 "실제 응답"으로 바꾸는 라우터
 * ----------------------------------------------------------------------------
 *  HttpRequest 가 요청을 해석해 주면, 이 클래스가 설정(Config)을 보고
 *  "그래서 무엇을 돌려줄지"를 결정합니다. 하는 일:
 *
 *    1) 가상호스트 선택 : Host 헤더로 어떤 server 블록인지 고름.
 *    2) 라우트 선택     : 요청 경로에 맞는 location 을 찾음(최장 prefix).
 *    3) 리다이렉트/메서드 검사 : return 설정이면 이동, 허용 안 된 메서드면 405.
 *    4) 메서드별 처리:
 *         - GET    : 파일을 읽어 보내거나, 디렉터리면 index 파일/목록(autoindex).
 *         - POST   : 업로드 폴더가 설정돼 있으면 본문을 파일로 저장.
 *         - DELETE : 파일을 삭제.
 *    5) 잘못된 경우엔 알맞은 상태코드 + (설정된) 에러페이지로 응답.
 *
 *  CGI(.py 등) 실행은 다음 단계(CgiHandler)에서 연결됩니다. 지금은 CGI 경로가
 *  매칭되면 501(아직 미구현)로 응답합니다.
 * ========================================================================== */

#ifndef REQUESTHANDLER_HPP
#define REQUESTHANDLER_HPP

#include "Config.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

#include <string>
#include <vector>

class RequestHandler
{
public:
    // 라우팅 결과. 두 갈래입니다.
    //   isCgi == false : response 에 완성된 응답이 들어 있음(정적 파일/업로드/에러 등).
    //   isCgi == true  : 이 요청은 CGI 로 처리해야 함 → scriptPath/interpreter/server 사용.
    struct Route
    {
        bool                isCgi;
        HttpResponse        response;     // isCgi == false 일 때 사용
        std::string         scriptPath;   // 실행할 스크립트 실제 경로 (isCgi)
        std::string         interpreter;  // 인터프리터 경로 (isCgi)
        const ServerConfig *server;       // 환경변수용 (isCgi)
        Route() : isCgi(false), response(), scriptPath(), interpreter(), server(0) {}
    };

    // servers: 이 연결(리스닝 소켓)에 묶인 가상호스트 후보들.
    explicit RequestHandler(const std::vector<const ServerConfig *> &servers);

    // 완성된 요청을 받아 "정적 응답" 또는 "CGI 처리 지시"를 돌려줍니다.
    Route route(const HttpRequest &req);

    // 요청 파싱이 실패했을 때(문법 오류 등) 쓸 에러 응답을 만듭니다.
    HttpResponse buildError(int code);

private:
    const std::vector<const ServerConfig *> &_servers;

    // Host 헤더로 가상호스트(server 블록) 선택. 못 맞추면 첫 번째가 기본.
    const ServerConfig *selectServer(const HttpRequest &req) const;

    // 메서드별 처리.
    HttpResponse handleGet(const HttpRequest &req, const ServerConfig &server,
                           const LocationConfig &loc);
    HttpResponse handlePost(const HttpRequest &req, const ServerConfig &server,
                            const LocationConfig &loc);
    HttpResponse handleDelete(const HttpRequest &req, const ServerConfig &server,
                              const LocationConfig &loc);

    // 보조 생성기.
    HttpResponse serveFile(const std::string &fsPath, const ServerConfig &server);
    HttpResponse autoIndex(const std::string &fsPath, const std::string &reqPath);
    HttpResponse makeRedirect(const LocationConfig &loc);
    HttpResponse makeError(const ServerConfig *server, int code);

    // URL 경로를 실제 파일 경로로 변환(location.path 접두사 제거 후 root 에 붙임).
    std::string  resolvePath(const LocationConfig &loc, const std::string &reqPath) const;
};

#endif // REQUESTHANDLER_HPP
