/* ============================================================================
 *  HttpResponse.cpp  —  HttpResponse.hpp 의 구현
 * ========================================================================== */

#include "HttpResponse.hpp"
#include "Utils.hpp"

#include <sstream>

HttpResponse::HttpResponse()
    : _status(200),
      _headers(),
      _body()
{
}

void HttpResponse::setStatus(int code)
{
    _status = code;
}

void HttpResponse::setHeader(const std::string &name, const std::string &value)
{
    _headers[name] = value;
}

void HttpResponse::setBody(const std::string &body)
{
    _body = body;
    // 본문 길이를 헤더에 자동 반영. 브라우저는 이 길이만큼 읽고 응답이 끝났다고 판단합니다.
    _headers["Content-Length"] = utils::toString(static_cast<long>(_body.size()));
}

void HttpResponse::stripBody()
{
    // 본문만 지우고 Content-Length 는 유지(HEAD 응답).
    _body.clear();
}

int HttpResponse::code() const
{
    return _status;
}

std::string HttpResponse::toString() const
{
    std::ostringstream out;

    // 상태 줄: "HTTP/1.1 200 OK"
    out << "HTTP/1.1 " << _status << " " << reasonPhrase(_status) << "\r\n";

    // 헤더들: "이름: 값" 한 줄씩
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
    {
        out << it->first << ": " << it->second << "\r\n";
    }

    // 헤더 끝을 알리는 빈 줄, 그리고 본문.
    out << "\r\n";
    out << _body;

    return out.str();
}

std::string HttpResponse::reasonPhrase(int code)
{
    // 이 서버가 사용할 만한 상태코드들의 표준 문구.
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}
