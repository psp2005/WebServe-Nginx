/* ============================================================================
 *  ConfigParser.hpp  —  설정 파일(.conf) 텍스트를 Config 구조로 바꾸는 파서
 * ----------------------------------------------------------------------------
 *  "파서(parser)"란 사람이 읽는 텍스트를 컴퓨터가 다루기 좋은 형태(여기서는
 *  Config 클래스)로 번역하는 프로그램입니다. 우리 설정 파일은 NGINX 를 닮은
 *  형식이라, 이 파서는 다음 두 단계로 동작합니다.
 *
 *    1) 토큰화(tokenize) : 파일 글자들을 의미 단위(단어/기호)로 쪼갭니다.
 *         "listen 8080;"  ->  ["listen", "8080", ";"]
 *       이때 주석(#...)은 버리고, '{' '}' ';' 는 따로 떨어진 토큰으로 만듭니다.
 *
 *    2) 구문 분석(parse)  : 토큰을 순서대로 읽으며 server / location 블록과
 *       지시어(directive)를 이해해 Config 를 채웁니다.
 *
 *  형식이 틀리면(괄호 안 맞음, 세미콜론 빠짐, 모르는 지시어 등) 어디가 잘못됐는지
 *  '줄 번호'와 함께 ParseError 예외를 던집니다.
 * ========================================================================== */

#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "Config.hpp"

#include <string>
#include <vector>
#include <cstddef>      // std::size_t
#include <stdexcept>    // std::runtime_error

class ConfigParser
{
public:
    // 설정 형식이 잘못되었을 때 던지는 예외. 메시지에 줄 번호가 들어갑니다.
    class ParseError : public std::runtime_error
    {
    public:
        explicit ParseError(const std::string &message);
    };

    ConfigParser();

    // 설정 파일을 읽어 Config 로 만들어 돌려줍니다.
    // 파일을 못 열거나 형식이 틀리면 ParseError 를 던집니다.
    Config parse(const std::string &path);

private:
    // 토큰 하나 = 단어/기호 + 그것이 있던 줄 번호(에러 메시지에 사용).
    struct Token
    {
        std::string text;
        int         line;
        Token();
        Token(const std::string &t, int l);
    };

    std::vector<Token> _tokens;     // 토큰화 결과 전체
    std::size_t        _pos;        // 지금 보고 있는 토큰 위치(커서)

    // --- 1단계: 토큰화 ---
    std::vector<Token> tokenize(const std::string &content) const;

    // --- 토큰 커서 조작 도우미 ---
    bool         atEnd() const;                         // 토큰을 다 읽었는가
    const Token &current() const;                       // 현재 토큰 (없으면 예외)
    void         advance();                             // 커서를 한 칸 전진
    bool         isStructural(const std::string &t) const; // '{' '}' ';' 인가
    void         expect(const std::string &text);       // 이 토큰이어야 함, 아니면 예외

    // 현재 위치부터 ';' 이 나올 때까지의 인자들을 모아 돌려줍니다(';' 은 소비).
    std::vector<std::string> readArgs(int directiveLine);

    // --- 2단계: 구문 분석 ---
    void parseServer(Config &config);
    void parseLocation(ServerConfig &server);

    // 모아온 지시어 이름/인자를 해석해 해당 구조체에 반영합니다.
    void applyServerDirective(ServerConfig &server,
                              const std::string &name,
                              const std::vector<std::string> &args,
                              int line);
    void applyLocationDirective(LocationConfig &loc,
                                const std::string &name,
                                const std::vector<std::string> &args,
                                int line);

    // 문자열을 정수로 변환(숫자가 아니면 예외). 포트/상태코드 파싱에 사용.
    int parseInt(const std::string &s, int line) const;

    // 이 파서는 내부 커서 상태를 가지므로 복사는 의미가 없습니다 -> 복사 금지.
    ConfigParser(const ConfigParser &);
    ConfigParser &operator=(const ConfigParser &);
};

#endif // CONFIGPARSER_HPP
