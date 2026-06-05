/* ============================================================================
 *  Config.hpp  —  설정 파일(.conf)의 내용을 담아두는 "데이터 그릇" 클래스들
 * ----------------------------------------------------------------------------
 *  웹 서버는 시작할 때 설정 파일을 한 번 읽어서, "어떤 포트에서 듣고",
 *  "어떤 URL 경로를 어떤 폴더에 연결하고", "어떤 메서드를 허용할지" 등을
 *  메모리에 기억해 둡니다. 그 기억을 담는 상자가 바로 이 파일의 클래스들입니다.
 *
 *  구조는 3단계로 포개져 있습니다 (큰 것 -> 작은 것):
 *
 *    Config            : 설정 파일 전체. 여러 개의 ServerConfig 를 담습니다.
 *      └ ServerConfig  : 'server { ... }' 블록 하나 (포트, 에러페이지 등).
 *          └ LocationConfig : 'location /경로 { ... }' 블록 하나 (라우트 규칙).
 *
 *  이 클래스들은 "로직"이 거의 없는 순수 데이터 묶음입니다. 그래서 멤버를
 *  public 으로 열어두어 읽기 쉽게 했고, 자주 쓰는 조회는 도우미 함수로 뒀습니다.
 *  실제 "읽고 해석하는" 일은 ConfigParser 가 담당합니다.
 * ========================================================================== */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstddef>      // std::size_t

/* ----------------------------------------------------------------------------
 *  LocationConfig : 하나의 URL 경로(route)에 대한 규칙 묶음
 *  예) location /upload { root www; methods GET POST DELETE; ... }
 * -------------------------------------------------------------------------- */
class LocationConfig
{
public:
    LocationConfig();   // 모든 값을 안전한 기본값으로 초기화

    // 이 규칙이 적용될 URL 경로 (예: "/", "/upload", "/cgi-bin")
    std::string                         path;

    // 파일을 실제로 찾을 디스크상의 폴더.
    // 예) root 가 "www" 이고 요청이 "/upload/a.txt" 면 -> "www/a.txt" 를 찾음
    //     (location 경로 부분은 떼어내고 root 뒤에 이어 붙입니다.)
    std::string                         root;

    // 디렉터리를 요청했을 때 대신 보여줄 기본 파일 (예: "index.html")
    std::string                         index;

    // 디렉터리 목록을 자동으로 보여줄지 여부 (autoindex on/off)
    bool                                autoindex;

    // 이 경로에서 허용하는 HTTP 메서드들 (예: GET, POST, DELETE). 대문자로 저장.
    std::set<std::string>               methods;

    // 업로드된 파일을 저장할 폴더. 비어 있으면 "이 경로는 업로드 불가" 라는 뜻.
    std::string                         uploadStore;

    // 확장자 -> CGI 인터프리터 경로. 예) ".py" -> "/usr/bin/python3"
    std::map<std::string, std::string>  cgi;

    // ---- 리다이렉트(다른 주소로 보내기) 설정 ----
    bool                                hasRedirect;    // return 지시어가 있었는지
    int                                 redirectCode;   // 상태코드 (예: 301)
    std::string                         redirectTarget; // 이동할 목적지 (예: "/new")

    // ---- 자주 쓰는 조회 도우미 ----

    // 주어진 메서드가 이 경로에서 허용되는지.
    bool        allowsMethod(const std::string &method) const;

    // 이 경로가 업로드를 허용하는지 (uploadStore 가 설정되어 있는지).
    bool        hasUpload() const;

    // 확장자(점 포함, 예 ".py")에 맞는 CGI 인터프리터를 찾습니다.
    // 없으면 빈 문자열을 돌려줍니다.
    std::string cgiInterpreterFor(const std::string &extension) const;
};

/* ----------------------------------------------------------------------------
 *  ServerConfig : 'server { ... }' 블록 하나 = 하나의 가상 서버
 * -------------------------------------------------------------------------- */
class ServerConfig
{
public:
    ServerConfig();     // 기본값으로 초기화 (host 0.0.0.0, port 80 등)

    std::string                 host;               // 바인딩할 IP (0.0.0.0 = 모든 인터페이스)
    int                         port;               // 듣는 포트 번호
    std::vector<std::string>    serverNames;        // server_name 목록 (가상호스트용)
    std::size_t                 clientMaxBodySize;  // 요청 본문 최대 크기(바이트)
    std::map<int, std::string>  errorPages;         // 상태코드 -> 에러페이지 파일 경로
    std::vector<LocationConfig> locations;          // 이 서버의 라우트(location) 목록

    // ---- 조회 도우미 ----

    // 요청 경로에 가장 잘 맞는 location 을 찾습니다 (가장 긴 prefix 가 우선).
    // 맞는 것이 없으면 NULL 을 돌려줍니다.
    const LocationConfig *matchLocation(const std::string &requestPath) const;

    // 상태코드에 지정된 커스텀 에러페이지 경로. 없으면 빈 문자열.
    std::string           errorPageFor(int code) const;
};

/* ----------------------------------------------------------------------------
 *  Config : 설정 파일 전체 = ServerConfig 들의 모음
 * -------------------------------------------------------------------------- */
class Config
{
public:
    std::vector<ServerConfig> servers;  // 파일에 적힌 모든 server 블록
};

#endif // CONFIG_HPP
