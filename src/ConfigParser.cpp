/* ============================================================================
 *  ConfigParser.cpp  —  ConfigParser.hpp 선언의 실제 구현
 * ----------------------------------------------------------------------------
 *  읽는 순서 추천:
 *    1) parse()        : 전체 흐름 (파일 읽기 -> 토큰화 -> server 블록 반복)
 *    2) tokenize()     : 글자를 토큰으로 쪼개는 1단계
 *    3) parseServer/parseLocation : 블록을 해석하는 2단계
 *    4) applyXxxDirective : 개별 지시어를 구조체에 반영
 * ========================================================================== */

#include "ConfigParser.hpp"
#include "Utils.hpp"

#include <sstream>      // std::istringstream
#include <cctype>       // std::isdigit, std::toupper

/* -------------------------------------------------------------------------- */
/*  작은 내부 도우미 (이 파일 안에서만 사용)                                   */
/* -------------------------------------------------------------------------- */
namespace
{
    // 문자열을 모두 대문자로. (메서드 이름을 일관되게 저장하기 위해)
    std::string toUpper(const std::string &s)
    {
        std::string out = s;
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
        return out;
    }

    // 에러 메시지 끝에 " (줄 N)" 을 붙여줍니다.
    std::string atLine(const std::string &msg, int line)
    {
        return msg + " (줄 " + utils::toString(line) + ")";
    }
}

/* ============================  ParseError  =============================== */

ConfigParser::ParseError::ParseError(const std::string &message)
    : std::runtime_error(message)
{
}

/* ============================  Token / ctor  ============================= */

ConfigParser::Token::Token()
    : text(), line(0)
{
}

ConfigParser::Token::Token(const std::string &t, int l)
    : text(t), line(l)
{
}

ConfigParser::ConfigParser()
    : _tokens(), _pos(0)
{
}

/* ==============================  parse()  ================================ */

Config ConfigParser::parse(const std::string &path)
{
    // (1) 파일 전체를 문자열로 읽습니다.
    bool ok = false;
    std::string content = utils::readFile(path, ok);
    if (!ok)
        throw ParseError("설정 파일을 열 수 없습니다: " + path);

    // (2) 토큰화 후 커서를 처음으로.
    _tokens = tokenize(content);
    _pos    = 0;

    // (3) 최상위에는 'server' 블록만 반복해서 올 수 있습니다.
    Config config;
    while (!atEnd())
    {
        if (current().text == "server")
            parseServer(config);
        else
            throw ParseError(atLine("최상위에는 'server' 블록만 올 수 있습니다. '"
                                    + current().text + "' 를 만났습니다", current().line));
    }

    if (config.servers.empty())
        throw ParseError("설정 파일에 'server' 블록이 하나도 없습니다.");

    return config;
}

/* =============================  tokenize()  ============================== */

std::vector<ConfigParser::Token> ConfigParser::tokenize(const std::string &content) const
{
    std::vector<Token> tokens;
    std::string        cur;     // 쌓고 있는 단어
    int                line = 1;

    // 쌓아둔 단어가 있으면 토큰으로 확정하고 비웁니다.
    // (람다 대신 직접 반복 — C++98 에는 람다가 없습니다.)
    for (std::size_t i = 0; i < content.size(); ++i)
    {
        char c = content[i];

        if (c == '#')                       // 주석: 줄 끝까지 통째로 무시
        {
            if (!cur.empty()) { tokens.push_back(Token(cur, line)); cur.clear(); }
            while (i < content.size() && content[i] != '\n')
                ++i;
            // 여기서 멈추면 content[i] 는 '\n' 또는 끝. for 의 ++i 전에
            // 줄바꿈 처리를 놓치지 않도록, i 를 하나 되돌려 다음 반복에서 처리.
            if (i < content.size())
                --i;
            continue;
        }
        if (c == '\n')                      // 줄바꿈: 단어 확정 + 줄 번호 증가
        {
            if (!cur.empty()) { tokens.push_back(Token(cur, line)); cur.clear(); }
            ++line;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r')   // 공백류: 단어 구분
        {
            if (!cur.empty()) { tokens.push_back(Token(cur, line)); cur.clear(); }
            continue;
        }
        if (c == '{' || c == '}' || c == ';')     // 구조 기호: 그 자체가 한 토큰
        {
            if (!cur.empty()) { tokens.push_back(Token(cur, line)); cur.clear(); }
            tokens.push_back(Token(std::string(1, c), line));
            continue;
        }
        cur += c;                           // 그 외 글자는 단어에 쌓음
    }
    if (!cur.empty())
        tokens.push_back(Token(cur, line));

    return tokens;
}

/* =========================  커서 조작 도우미  ============================= */

bool ConfigParser::atEnd() const
{
    return _pos >= _tokens.size();
}

const ConfigParser::Token &ConfigParser::current() const
{
    if (atEnd())
        throw ParseError("설정이 예상보다 일찍 끝났습니다(블록이나 ';' 가 빠졌을 수 있음).");
    return _tokens[_pos];
}

void ConfigParser::advance()
{
    if (!atEnd())
        ++_pos;
}

bool ConfigParser::isStructural(const std::string &t) const
{
    return t == "{" || t == "}" || t == ";";
}

void ConfigParser::expect(const std::string &text)
{
    if (atEnd())
        throw ParseError("'" + text + "' 가 필요한데 설정이 끝났습니다.");
    if (current().text != text)
        throw ParseError(atLine("'" + text + "' 가 필요한데 '"
                                + current().text + "' 를 만났습니다", current().line));
    advance();
}

std::vector<std::string> ConfigParser::readArgs(int directiveLine)
{
    std::vector<std::string> args;
    while (!atEnd())
    {
        const Token &t = current();
        if (t.text == ";")          // 끝 표시 발견 -> 소비하고 반환
        {
            advance();
            return args;
        }
        if (t.text == "{" || t.text == "}")     // ';' 없이 블록 기호가 나옴 = 오류
            throw ParseError(atLine("';' 로 끝나야 할 지시어에 '" + t.text + "' 가 나왔습니다",
                                    directiveLine));
        args.push_back(t.text);
        advance();
    }
    throw ParseError(atLine("지시어가 ';' 로 끝나지 않았습니다", directiveLine));
}

/* ===========================  parseServer()  ============================= */

void ConfigParser::parseServer(Config &config)
{
    expect("server");
    expect("{");

    ServerConfig server;
    while (!atEnd() && current().text != "}")
    {
        const Token &t = current();

        if (t.text == "location")           // 중첩 블록
        {
            parseLocation(server);
        }
        else if (isStructural(t.text))      // '{' ';' 등이 지시어 자리에 오면 오류
        {
            throw ParseError(atLine("server 블록 안에서 예상치 못한 '" + t.text + "'", t.line));
        }
        else                                // 일반 지시어: 이름 + 인자들 + ';'
        {
            std::string name = t.text;
            int         line = t.line;
            advance();
            std::vector<std::string> args = readArgs(line);
            applyServerDirective(server, name, args, line);
        }
    }
    expect("}");

    config.servers.push_back(server);
}

/* ==========================  parseLocation()  =========================== */

void ConfigParser::parseLocation(ServerConfig &server)
{
    expect("location");

    // location 다음에는 경로가 와야 합니다 (예: "/", "/upload").
    if (atEnd() || isStructural(current().text))
        throw ParseError("location 뒤에 경로가 필요합니다 (예: location /upload { ... }).");

    LocationConfig loc;
    loc.path = current().text;
    advance();

    expect("{");
    while (!atEnd() && current().text != "}")
    {
        const Token &t = current();
        if (isStructural(t.text))
            throw ParseError(atLine("location 블록 안에서 예상치 못한 '" + t.text + "'", t.line));

        std::string name = t.text;
        int         line = t.line;
        advance();
        std::vector<std::string> args = readArgs(line);
        applyLocationDirective(loc, name, args, line);
    }
    expect("}");

    // 메서드를 하나도 지정하지 않았다면 기본으로 GET 만 허용합니다.
    if (loc.methods.empty())
        loc.methods.insert("GET");

    server.locations.push_back(loc);
}

/* =======================  applyServerDirective()  ======================= */

void ConfigParser::applyServerDirective(ServerConfig &server,
                                        const std::string &name,
                                        const std::vector<std::string> &args,
                                        int line)
{
    if (name == "listen")
    {
        // "8080" 또는 "127.0.0.1:8080" 형태를 모두 지원합니다.
        if (args.size() != 1)
            throw ParseError(atLine("listen 은 인자 1개가 필요합니다", line));
        const std::string &v = args[0];
        std::string::size_type colon = v.find(':');
        if (colon != std::string::npos)
        {
            server.host = v.substr(0, colon);
            server.port = parseInt(v.substr(colon + 1), line);
        }
        else
        {
            server.port = parseInt(v, line);
        }
        if (server.port < 1 || server.port > 65535)
            throw ParseError(atLine("포트 번호는 1~65535 사이여야 합니다", line));
    }
    else if (name == "host")
    {
        if (args.size() != 1)
            throw ParseError(atLine("host 는 인자 1개가 필요합니다", line));
        server.host = args[0];
    }
    else if (name == "server_name")
    {
        if (args.empty())
            throw ParseError(atLine("server_name 은 최소 1개의 이름이 필요합니다", line));
        for (std::size_t i = 0; i < args.size(); ++i)
            server.serverNames.push_back(args[i]);
    }
    else if (name == "client_max_body_size")
    {
        if (args.size() != 1)
            throw ParseError(atLine("client_max_body_size 는 인자 1개가 필요합니다", line));
        std::size_t bytes = 0;
        if (!utils::parseSize(args[0], bytes))
            throw ParseError(atLine("client_max_body_size 형식이 잘못되었습니다(예: 10M, 512K)", line));
        server.clientMaxBodySize = bytes;
    }
    else if (name == "error_page")
    {
        // "error_page 500 502 503 /errors/50x.html;" 처럼
        // 마지막 인자는 페이지 경로, 그 앞은 모두 상태코드입니다.
        if (args.size() < 2)
            throw ParseError(atLine("error_page 는 '코드... 경로' 형식이어야 합니다", line));
        const std::string &page = args[args.size() - 1];
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
        {
            int code = parseInt(args[i], line);
            server.errorPages[code] = page;
        }
    }
    else
    {
        throw ParseError(atLine("알 수 없는 server 지시어 '" + name + "'", line));
    }
}

/* ======================  applyLocationDirective()  ====================== */

void ConfigParser::applyLocationDirective(LocationConfig &loc,
                                          const std::string &name,
                                          const std::vector<std::string> &args,
                                          int line)
{
    if (name == "root")
    {
        if (args.size() != 1)
            throw ParseError(atLine("root 는 인자 1개가 필요합니다", line));
        loc.root = args[0];
    }
    else if (name == "index")
    {
        if (args.size() != 1)
            throw ParseError(atLine("index 는 인자 1개가 필요합니다", line));
        loc.index = args[0];
    }
    else if (name == "autoindex")
    {
        if (args.size() != 1)
            throw ParseError(atLine("autoindex 는 'on' 또는 'off' 여야 합니다", line));
        if (args[0] == "on")
            loc.autoindex = true;
        else if (args[0] == "off")
            loc.autoindex = false;
        else
            throw ParseError(atLine("autoindex 값은 'on' 또는 'off' 만 가능합니다", line));
    }
    else if (name == "methods")
    {
        if (args.empty())
            throw ParseError(atLine("methods 는 최소 1개의 메서드가 필요합니다", line));
        for (std::size_t i = 0; i < args.size(); ++i)
            loc.methods.insert(toUpper(args[i]));   // 대문자로 통일해 저장
    }
    else if (name == "upload_store")
    {
        if (args.size() != 1)
            throw ParseError(atLine("upload_store 는 인자 1개가 필요합니다", line));
        loc.uploadStore = args[0];
    }
    else if (name == "cgi")
    {
        // "cgi .py /usr/bin/python3;" -> 확장자 + 인터프리터 경로
        if (args.size() != 2)
            throw ParseError(atLine("cgi 는 '.확장자 인터프리터경로' 2개 인자가 필요합니다", line));
        loc.cgi[args[0]] = args[1];
    }
    else if (name == "return")
    {
        // "return 301 /new;" (코드+목적지) 또는 "return 404;" (코드만) 지원
        if (args.empty() || args.size() > 2)
            throw ParseError(atLine("return 은 '코드 [목적지]' 형식이어야 합니다", line));
        loc.hasRedirect  = true;
        loc.redirectCode = parseInt(args[0], line);
        if (args.size() == 2)
            loc.redirectTarget = args[1];
    }
    else
    {
        throw ParseError(atLine("알 수 없는 location 지시어 '" + name + "'", line));
    }
}

/* ============================  parseInt()  =============================== */

int ConfigParser::parseInt(const std::string &s, int line) const
{
    if (s.empty())
        throw ParseError(atLine("숫자가 비어 있습니다", line));

    // 모든 글자가 0~9 인지 확인 (음수/소수/문자 섞임 방지).
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            throw ParseError(atLine("숫자가 아닙니다: '" + s + "'", line));
    }

    std::istringstream iss(s);
    long value = 0;
    iss >> value;
    if (iss.fail() || value < 0 || value > 2147483647L)
        throw ParseError(atLine("숫자 범위를 벗어났습니다: '" + s + "'", line));

    return static_cast<int>(value);
}
