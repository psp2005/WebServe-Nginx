/* ============================================================================
 *  HttpResponse.hpp  —  HTTP 응답(브라우저에게 돌려줄 답)을 조립하는 빌더
 * ----------------------------------------------------------------------------
 *  HTTP 응답은 사실 그냥 '약속된 형식의 텍스트'입니다. 예:
 *
 *      HTTP/1.1 200 OK\r\n
 *      Content-Type: text/html\r\n
 *      Content-Length: 13\r\n
 *      \r\n                <- 헤더 끝(빈 줄)
 *      Hello, world!       <- 본문(body)
 *
 *  이 클래스는 "상태코드 정하고, 헤더 몇 개 넣고, 본문 넣으면" 위 형식의
 *  완성된 문자열을 toString() 으로 만들어 줍니다. 그 문자열을 그대로 소켓에
 *  send() 하면 됩니다.
 * ========================================================================== */

#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <map>

class HttpResponse
{
public:
    HttpResponse();

    // 상태코드를 정합니다. 표준 사유문구(reason phrase)도 자동으로 맞춰집니다.
    // 예: setStatus(404) -> "HTTP/1.1 404 Not Found"
    void setStatus(int code);

    // 헤더를 추가/설정합니다(같은 이름이면 덮어씀).
    void setHeader(const std::string &name, const std::string &value);

    // 본문을 설정하고, 그 길이에 맞춰 Content-Length 헤더를 자동으로 채웁니다.
    void setBody(const std::string &body);

    // 위 설정들을 모아 '완성된 HTTP 응답 문자열'로 직렬화합니다.
    std::string toString() const;

    int code() const;

    // 상태코드에 해당하는 표준 사유문구. (예: 200 -> "OK", 404 -> "Not Found")
    static std::string reasonPhrase(int code);

private:
    int                                 _status;
    std::map<std::string, std::string>  _headers;   // 헤더 이름 -> 값
    std::string                         _body;
};

#endif // HTTPRESPONSE_HPP
