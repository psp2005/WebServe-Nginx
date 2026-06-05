/* ============================================================================
 *  HttpRequest.hpp  —  들어온 요청 바이트를 해석하는 HTTP 요청 파서
 * ----------------------------------------------------------------------------
 *  브라우저가 보내는 요청도 '약속된 형식의 텍스트'입니다. 예:
 *
 *      GET /index.html?x=1 HTTP/1.1\r\n     <- 요청 줄(메서드 대상 버전)
 *      Host: localhost\r\n                  <- 헤더들
 *      Content-Length: 5\r\n
 *      \r\n                                 <- 헤더 끝(빈 줄)
 *      hello                                <- 본문(있을 때만)
 *
 *  네트워크 특성상 이 바이트들은 '한 번에' 도착하지 않고 조각조각 옵니다.
 *  그래서 이 파서는 '증분(incremental)' 방식입니다: 도착한 조각을 parse() 로
 *  계속 밀어넣으면, 내부 상태를 이어가며 완성될 때까지 조금씩 해석합니다.
 *
 *  본문 길이를 정하는 두 가지 방법을 모두 지원합니다:
 *   - Content-Length: 정확한 바이트 수가 헤더에 적혀 있음.
 *   - Transfer-Encoding: chunked: 본문을 여러 '덩어리(chunk)'로 나눠 보냄.
 *     각 덩어리 앞에 16진수 크기가 붙고, 크기 0 덩어리가 끝을 의미함 → 합쳐서 복원.
 * ========================================================================== */

#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>
#include <cstddef>

class HttpRequest
{
public:
    // 파싱 진행 상태.
    //   PARSING  : 아직 더 받아야 함
    //   COMPLETE : 요청 하나를 끝까지 다 읽음
    //   ERROR    : 형식 오류 → errorStatus() 로 돌려줄 상태코드 확인
    enum ParseState { PARSING, COMPLETE, ERROR };

    HttpRequest();

    // 본문 최대 허용 크기(client_max_body_size). 넘으면 413 오류.
    void setMaxBodySize(std::size_t maxBytes);

    // 새로 도착한 바이트들을 밀어넣고 파싱을 진행. 현재 상태를 돌려줍니다.
    ParseState parse(const char *data, std::size_t len);

    ParseState state() const;
    int        errorStatus() const;     // ERROR 일 때 돌려줄 HTTP 상태코드

    // ---- 파싱 결과 조회 ----
    const std::string &method() const;  // 예: "GET"
    const std::string &target() const;  // 예: "/index.html?x=1" (원본)
    const std::string &path() const;    // 예: "/index.html"     ('?' 앞)
    const std::string &query() const;   // 예: "x=1"             ('?' 뒤)
    const std::string &version() const; // 예: "HTTP/1.1"
    const std::string &body() const;

    // 헤더 값을 조회(대소문자 무시). 없으면 빈 문자열.
    std::string        header(const std::string &name) const;
    bool               hasHeader(const std::string &name) const;

private:
    // 파싱 단계(상태 기계).
    enum Stage
    {
        ST_LINE,            // 요청 줄을 읽는 중
        ST_HEADERS,         // 헤더들을 읽는 중
        ST_BODY_LENGTH,     // Content-Length 만큼 본문을 읽는 중
        ST_CHUNK_SIZE,      // chunk 크기 줄을 읽는 중
        ST_CHUNK_DATA,      // chunk 데이터를 읽는 중
        ST_CHUNK_CRLF,      // chunk 데이터 뒤의 CRLF 를 소비하는 중
        ST_CHUNK_TRAILER,   // 마지막 chunk 뒤 트레일러/빈 줄을 읽는 중
        ST_DONE,
        ST_FAIL
    };

    Stage                               _stage;
    int                                 _error;     // 오류 상태코드(0=오류 없음)
    std::string                         _buf;       // 아직 소비하지 않은 원본 바이트
    std::size_t                         _maxBody;

    std::string                         _method;
    std::string                         _target;
    std::string                         _path;
    std::string                         _query;
    std::string                         _version;
    std::map<std::string, std::string>  _headers;   // 소문자 이름 -> 값
    std::string                         _body;

    bool                                _hasContentLength;
    std::size_t                         _contentLength; // 남은 본문 바이트 수로도 사용
    std::size_t                         _chunkRemaining;

    // _buf 에서 CRLF 로 끝나는 한 줄을 꺼냅니다(CRLF 는 버림). 줄이 아직
    // 다 안 왔으면 false. 줄을 꺼냈으면 line 에 담고 true.
    bool        getLine(std::string &line);

    // 각 단계 처리. 진전이 있었으면 true(루프 계속), 더 받아야 하면 false.
    bool        doRequestLine();
    bool        doHeaders();
    bool        doBodyLength();
    bool        doChunked();

    void        fail(int status);                   // 오류 상태로 전환
    void        finishHeaders();                    // 헤더 다 읽은 뒤 본문 방식 결정
};

#endif // HTTPREQUEST_HPP
