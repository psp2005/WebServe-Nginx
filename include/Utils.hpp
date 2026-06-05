/* ============================================================================
 *  Utils.hpp  —  프로젝트 전반에서 쓰는 작은 도우미 함수 모음
 * ----------------------------------------------------------------------------
 *  특정 클래스에 속하지 않는 "잡일" 함수들을 namespace 안에 모았습니다.
 *  문자열 다듬기, 숫자<->문자열 변환, 파일 정보 조회 같은 것들입니다.
 *
 *  왜 namespace 인가?
 *   - 전역에 함수를 그냥 풀어놓으면 다른 곳의 이름과 충돌할 수 있습니다.
 *   - utils::trim() 처럼 출처가 분명해져 코드를 읽기 쉬워집니다.
 * ========================================================================== */

#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>

namespace utils
{
    // ---- 문자열 다루기 ----

    // 앞뒤 공백(스페이스/탭/개행)을 제거한 새 문자열을 돌려줍니다.
    std::string trim(const std::string &s);

    // 문자열을 구분자(delimiter) 기준으로 잘라 토큰 벡터로 만듭니다.
    // 연속된 구분자/빈 토큰은 건너뜁니다. (예: "a  b" -> ["a","b"])
    std::vector<std::string> split(const std::string &s, char delimiter);

    // 공백류(스페이스, 탭) 기준으로 나눕니다. 설정 파일 한 줄을 단어로 쪼갤 때 사용.
    std::vector<std::string> splitWhitespace(const std::string &s);

    // 알파벳을 모두 소문자로 바꾼 새 문자열. (HTTP 헤더 이름 비교는 대소문자 무시)
    std::string toLower(const std::string &s);

    // 접미사(suffix)로 끝나는지 검사. (예: endsWith("hello.py", ".py") == true)
    bool endsWith(const std::string &s, const std::string &suffix);

    // 접두사(prefix)로 시작하는지 검사.
    bool startsWith(const std::string &s, const std::string &prefix);

    // ---- 숫자 <-> 문자열 ----
    // C++98 에는 std::to_string 이 없어서 직접 만듭니다.
    std::string toString(long value);

    // "10M", "512K", "2G", "1024" 같은 크기 표기를 바이트 수로 변환합니다.
    // 성공하면 true 와 out 에 결과를 채우고, 형식이 틀리면 false.
    bool parseSize(const std::string &s, size_t &out);

    // ---- 파일 시스템 조회 (stat 사용) ----

    // 경로가 존재하는지.
    bool pathExists(const std::string &path);
    // 경로가 '디렉터리'인지.
    bool isDirectory(const std::string &path);
    // 경로가 '일반 파일'인지.
    bool isRegularFile(const std::string &path);

    // 파일 전체를 읽어 문자열로 돌려줍니다. 실패하면 ok=false.
    std::string readFile(const std::string &path, bool &ok);

    // 파일 경로에서 확장자(점 포함)를 추출합니다. (예: "a/b.py" -> ".py")
    // 확장자가 없으면 빈 문자열.
    std::string getExtension(const std::string &path);
}

#endif // UTILS_HPP
