/* ============================================================================
 *  Utils.cpp  —  Utils.hpp 에 선언한 도우미 함수들의 실제 구현
 * ========================================================================== */

#include "Utils.hpp"

#include <sstream>      // std::ostringstream (숫자 -> 문자열)
#include <fstream>      // std::ifstream (파일 읽기)
#include <cctype>       // std::tolower, std::isspace
#include <sys/stat.h>   // stat() : 파일 종류/존재 확인 (POSIX)

namespace utils
{
    std::string trim(const std::string &s)
    {
        // 앞쪽에서 공백이 아닌 첫 글자 위치를 찾는다.
        std::string::size_type start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        // 뒤쪽에서 공백이 아닌 마지막 글자 위치를 찾는다.
        std::string::size_type end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return s.substr(start, end - start);
    }

    std::vector<std::string> split(const std::string &s, char delimiter)
    {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream stream(s);
        // getline 으로 delimiter 단위로 끊어 읽는다.
        while (std::getline(stream, token, delimiter))
        {
            if (!token.empty())     // 빈 토큰(연속 구분자)은 버린다.
                tokens.push_back(token);
        }
        return tokens;
    }

    std::vector<std::string> splitWhitespace(const std::string &s)
    {
        // istringstream 에 >> 연산자를 쓰면 공백을 기준으로 단어를 하나씩 뽑아준다.
        std::vector<std::string> tokens;
        std::istringstream stream(s);
        std::string word;
        while (stream >> word)
            tokens.push_back(word);
        return tokens;
    }

    std::string toLower(const std::string &s)
    {
        std::string result(s);
        for (std::string::size_type i = 0; i < result.size(); ++i)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
        return result;
    }

    bool endsWith(const std::string &s, const std::string &suffix)
    {
        if (suffix.size() > s.size())
            return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool startsWith(const std::string &s, const std::string &prefix)
    {
        if (prefix.size() > s.size())
            return false;
        return s.compare(0, prefix.size(), prefix) == 0;
    }

    std::string toString(long value)
    {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    bool parseSize(const std::string &s, size_t &out)
    {
        if (s.empty())
            return false;

        // 끝 글자가 단위(K/M/G)인지 확인하고, 배수를 정한다.
        size_t multiplier = 1;
        std::string number = s;
        char last = s[s.size() - 1];
        if (last == 'K' || last == 'k') { multiplier = 1024UL; number = s.substr(0, s.size() - 1); }
        else if (last == 'M' || last == 'm') { multiplier = 1024UL * 1024UL; number = s.substr(0, s.size() - 1); }
        else if (last == 'G' || last == 'g') { multiplier = 1024UL * 1024UL * 1024UL; number = s.substr(0, s.size() - 1); }

        if (number.empty())
            return false;
        // 남은 부분은 전부 숫자여야 한다.
        for (std::string::size_type i = 0; i < number.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(number[i])))
                return false;

        std::istringstream iss(number);
        size_t value = 0;
        iss >> value;
        out = value * multiplier;
        return true;
    }

    bool pathExists(const std::string &path)
    {
        struct stat st;
        // stat 이 0을 반환하면 그 경로의 정보를 얻는 데 성공 = 존재함.
        return stat(path.c_str(), &st) == 0;
    }

    bool isDirectory(const std::string &path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            return false;
        // st_mode 에 S_ISDIR 매크로를 적용해 디렉터리 여부 판단.
        return S_ISDIR(st.st_mode);
    }

    bool isRegularFile(const std::string &path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            return false;
        return S_ISREG(st.st_mode);
    }

    std::string readFile(const std::string &path, bool &ok)
    {
        // 바이너리 모드로 열어 이미지 등도 깨지지 않게 읽는다.
        std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            ok = false;
            return std::string();
        }
        // 스트림 버퍼를 통째로 ostringstream 에 부어 문자열로 만든다.
        std::ostringstream contents;
        contents << file.rdbuf();
        ok = true;
        return contents.str();
    }

    std::string getExtension(const std::string &path)
    {
        // 마지막 '.' 위치를 찾는다.
        std::string::size_type dot = path.rfind('.');
        if (dot == std::string::npos)
            return std::string();
        // 단, '.' 이 마지막 슬래시보다 앞이면 그건 디렉터리 이름의 점이므로 무시.
        std::string::size_type slash = path.find_last_of('/');
        if (slash != std::string::npos && dot < slash)
            return std::string();
        return path.substr(dot);
    }
}
