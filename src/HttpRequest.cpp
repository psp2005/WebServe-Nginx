/* ============================================================================
 *  HttpRequest.cpp  —  HttpRequest.hpp 의 구현 (증분 HTTP 요청 파서)
 * ----------------------------------------------------------------------------
 *  핵심 흐름: parse() 가 새 바이트를 _buf 에 붙인 뒤, 현재 단계(_stage)에
 *  맞는 처리 함수를 '진전이 없을 때까지' 반복 호출합니다. 각 단계 함수는
 *  필요한 만큼 _buf 를 소비하고, 다음 단계로 넘어가거나 더 받기를 기다립니다.
 * ========================================================================== */

#include "HttpRequest.hpp"
#include "Utils.hpp"

#include <sstream>
#include <cstdlib>      // std::strtol (chunk 크기 16진수 파싱)

// 헤더 부분이 비정상적으로 크면(공격/실수) 방어하기 위한 상한.
static const std::size_t kMaxHeaderBytes = 64 * 1024;

HttpRequest::HttpRequest()
    : _stage(ST_LINE),
      _error(0),
      _buf(),
      _maxBody(1048576),        // 기본 1MB (설정값으로 덮어씀)
      _method(),
      _target(),
      _path(),
      _query(),
      _version(),
      _headers(),
      _body(),
      _hasContentLength(false),
      _contentLength(0),
      _chunkRemaining(0)
{
}

void HttpRequest::setMaxBodySize(std::size_t maxBytes)
{
    _maxBody = maxBytes;
}

HttpRequest::ParseState HttpRequest::state() const
{
    if (_stage == ST_DONE)  return COMPLETE;
    if (_stage == ST_FAIL)  return ERROR;
    return PARSING;
}

int HttpRequest::errorStatus() const
{
    return _error;
}

const std::string &HttpRequest::method() const  { return _method; }
const std::string &HttpRequest::target() const  { return _target; }
const std::string &HttpRequest::path() const    { return _path; }
const std::string &HttpRequest::query() const   { return _query; }
const std::string &HttpRequest::version() const { return _version; }
const std::string &HttpRequest::body() const    { return _body; }

bool HttpRequest::hasHeader(const std::string &name) const
{
    return _headers.find(utils::toLower(name)) != _headers.end();
}

std::string HttpRequest::header(const std::string &name) const
{
    std::map<std::string, std::string>::const_iterator it = _headers.find(utils::toLower(name));
    if (it != _headers.end())
        return it->second;
    return std::string();
}

void HttpRequest::fail(int status)
{
    _error = status;
    _stage = ST_FAIL;
}

bool HttpRequest::getLine(std::string &line)
{
    std::string::size_type pos = _buf.find("\r\n");
    if (pos == std::string::npos)
        return false;               // 아직 한 줄이 다 안 옴
    line = _buf.substr(0, pos);
    _buf.erase(0, pos + 2);         // 줄 내용 + CRLF 제거
    return true;
}

/* ==============================  parse()  =============================== */

HttpRequest::ParseState HttpRequest::parse(const char *data, std::size_t len)
{
    if (_stage == ST_DONE || _stage == ST_FAIL)
        return state();

    _buf.append(data, len);

    // 진전이 있는 한 계속 단계를 처리. (한 번에 여러 줄/덩어리가 도착할 수 있음)
    bool progress = true;
    while (progress)
    {
        switch (_stage)
        {
            case ST_LINE:        progress = doRequestLine(); break;
            case ST_HEADERS:     progress = doHeaders();     break;
            case ST_BODY_LENGTH: progress = doBodyLength();  break;
            case ST_CHUNK_SIZE:
            case ST_CHUNK_DATA:
            case ST_CHUNK_CRLF:
            case ST_CHUNK_TRAILER: progress = doChunked();   break;
            default:             progress = false;           break;
        }
    }
    return state();
}

/* ===========================  요청 줄 처리  ============================= */

bool HttpRequest::doRequestLine()
{
    std::string line;
    if (!getLine(line))
    {
        // 줄이 아직 안 왔는데 버퍼가 너무 크면 비정상.
        if (_buf.size() > kMaxHeaderBytes)
            fail(400);
        return false;
    }

    // 일부 클라이언트는 요청 줄 앞에 빈 줄을 보냄 → 무시하고 다음 줄을 기다림.
    if (line.empty())
        return true;

    // "METHOD SP TARGET SP VERSION" 세 토막으로 나눕니다.
    std::vector<std::string> parts = utils::splitWhitespace(line);
    if (parts.size() != 3)
    {
        fail(400);
        return false;
    }
    _method  = parts[0];
    _target  = parts[1];
    _version = parts[2];

    // 버전 형식 최소 검증.
    if (!utils::startsWith(_version, "HTTP/"))
    {
        fail(400);
        return false;
    }

    // 대상(target)을 경로와 쿼리로 분리: "/a?b=1" -> path "/a", query "b=1"
    std::string::size_type q = _target.find('?');
    if (q == std::string::npos)
    {
        _path  = _target;
        _query = "";
    }
    else
    {
        _path  = _target.substr(0, q);
        _query = _target.substr(q + 1);
    }

    _stage = ST_HEADERS;
    return true;
}

/* =============================  헤더 처리  ============================== */

bool HttpRequest::doHeaders()
{
    std::string line;
    if (!getLine(line))
    {
        if (_buf.size() > kMaxHeaderBytes)
            fail(431);          // 헤더가 너무 큼
        return false;
    }

    if (line.empty())           // 빈 줄 = 헤더 끝
    {
        finishHeaders();
        return true;
    }

    // "Name: value" 분리.
    std::string::size_type colon = line.find(':');
    if (colon == std::string::npos)
    {
        fail(400);
        return false;
    }
    std::string name  = utils::toLower(utils::trim(line.substr(0, colon)));
    std::string value = utils::trim(line.substr(colon + 1));
    if (name.empty())
    {
        fail(400);
        return false;
    }
    _headers[name] = value;
    return true;
}

void HttpRequest::finishHeaders()
{
    // 본문을 어떻게 읽을지 헤더로 결정합니다.
    // 1) Transfer-Encoding: chunked 가 우선.
    std::string te = utils::toLower(header("transfer-encoding"));
    if (te.find("chunked") != std::string::npos)
    {
        _stage = ST_CHUNK_SIZE;
        return;
    }

    // 2) Content-Length 가 있으면 그만큼 읽음.
    if (hasHeader("content-length"))
    {
        std::string cl = header("content-length");
        // 숫자 검증 + 변환.
        for (std::size_t i = 0; i < cl.size(); ++i)
        {
            if (cl[i] < '0' || cl[i] > '9')
            {
                fail(400);
                return;
            }
        }
        std::istringstream iss(cl);
        std::size_t n = 0;
        iss >> n;
        if (iss.fail())
        {
            fail(400);
            return;
        }
        if (n > _maxBody)
        {
            fail(413);
            return;
        }
        _hasContentLength = true;
        _contentLength    = n;          // 앞으로 읽어야 할 본문 바이트 수
        if (n == 0)
            _stage = ST_DONE;
        else
            _stage = ST_BODY_LENGTH;
        return;
    }

    // 3) 둘 다 없으면 본문 없음 → 완료.
    _stage = ST_DONE;
}

/* =====================  본문(Content-Length) 처리  ===================== */

bool HttpRequest::doBodyLength()
{
    if (_buf.empty())
        return false;

    // 남은 본문 길이와 버퍼에 있는 양 중 작은 만큼 가져옵니다.
    std::size_t take = _buf.size();
    if (take > _contentLength)
        take = _contentLength;

    _body.append(_buf, 0, take);
    _buf.erase(0, take);
    _contentLength -= take;

    if (_contentLength == 0)
    {
        _stage = ST_DONE;
        return true;
    }
    return false;       // 본문이 더 와야 함
}

/* ========================  본문(chunked) 처리  ========================= */

bool HttpRequest::doChunked()
{
    if (_stage == ST_CHUNK_SIZE)
    {
        std::string line;
        if (!getLine(line))
            return false;

        // 크기 줄에는 ";확장" 이 붙을 수 있으니 ';' 앞부분만 사용.
        std::string::size_type semi = line.find(';');
        std::string hex = (semi == std::string::npos) ? line : line.substr(0, semi);
        hex = utils::trim(hex);
        if (hex.empty())
        {
            fail(400);
            return false;
        }

        // 16진수 문자열을 숫자로.
        char *end = 0;
        long size = std::strtol(hex.c_str(), &end, 16);
        if (end == hex.c_str() || *end != '\0' || size < 0)
        {
            fail(400);
            return false;
        }

        if (size == 0)
            _stage = ST_CHUNK_TRAILER;      // 마지막 덩어리 → 트레일러로
        else
        {
            _chunkRemaining = static_cast<std::size_t>(size);
            _stage = ST_CHUNK_DATA;
        }
        return true;
    }

    if (_stage == ST_CHUNK_DATA)
    {
        if (_buf.empty())
            return false;

        std::size_t take = _buf.size();
        if (take > _chunkRemaining)
            take = _chunkRemaining;

        _body.append(_buf, 0, take);
        _buf.erase(0, take);
        _chunkRemaining -= take;

        // 누적된 본문이 상한을 넘으면 거부.
        if (_body.size() > _maxBody)
        {
            fail(413);
            return false;
        }

        if (_chunkRemaining == 0)
            _stage = ST_CHUNK_CRLF;         // 덩어리 뒤 CRLF 소비 단계로
        return true;
    }

    if (_stage == ST_CHUNK_CRLF)
    {
        // 각 덩어리 데이터 뒤에는 CRLF 가 따라옵니다(빈 줄 형태).
        std::string line;
        if (!getLine(line))
            return false;
        if (!line.empty())          // 데이터 뒤에는 반드시 빈 줄이어야 함
        {
            fail(400);
            return false;
        }
        _stage = ST_CHUNK_SIZE;     // 다음 덩어리 크기로
        return true;
    }

    if (_stage == ST_CHUNK_TRAILER)
    {
        // 마지막 0 덩어리 뒤: 트레일러 헤더(있을 수도) 들을 빈 줄까지 읽고 끝.
        std::string line;
        if (!getLine(line))
            return false;
        if (line.empty())
        {
            _stage = ST_DONE;
            return true;
        }
        // 트레일러 헤더 한 줄은 무시하고 계속(빈 줄 나올 때까지).
        return true;
    }

    return false;
}
