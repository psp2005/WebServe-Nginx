/* ============================================================================
 *  Config.cpp  —  Config.hpp 의 데이터 클래스들에 대한 구현
 * ----------------------------------------------------------------------------
 *  대부분은 "기본값 설정"과 "간단한 조회"라서 짧습니다.
 *  복잡한 파싱 로직은 여기 없습니다(그건 ConfigParser 의 몫).
 * ========================================================================== */

#include "Config.hpp"

/* ===========================  LocationConfig  ============================= */

LocationConfig::LocationConfig()
    : path(),
      root(),
      index(),
      autoindex(false),     // 기본은 디렉터리 목록 끔(보안상 안전한 쪽)
      methods(),
      uploadStore(),
      cgi(),
      hasMaxBodySize(false),
      maxBodySize(0),
      hasRedirect(false),
      redirectCode(0),
      redirectTarget()
{
}

bool LocationConfig::allowsMethod(const std::string &method) const
{
    // set 에 들어 있으면 허용. (메서드는 ConfigParser 에서 대문자로 저장됨)
    return methods.find(method) != methods.end();
}

bool LocationConfig::hasUpload() const
{
    return !uploadStore.empty();
}

std::string LocationConfig::cgiInterpreterFor(const std::string &extension) const
{
    std::map<std::string, std::string>::const_iterator it = cgi.find(extension);
    if (it != cgi.end())
        return it->second;
    return std::string();   // 못 찾으면 빈 문자열
}

/* ============================  ServerConfig  ============================== */

ServerConfig::ServerConfig()
    : host("0.0.0.0"),          // 지정이 없으면 모든 인터페이스에서 듣기
      port(80),                 // 지정이 없으면 80 포트
      serverNames(),
      // 본문 최대 크기 기본값 1MB. 0 으로 두면 "무제한"이 되어 위험하므로 제한을 둡니다.
      clientMaxBodySize(1048576),
      errorPages(),
      locations()
{
}

const LocationConfig *ServerConfig::matchLocation(const std::string &requestPath) const
{
    const LocationConfig *best = 0;     // 아직 못 찾음
    std::size_t           bestLen = 0;  // 지금까지 찾은 가장 긴 prefix 길이

    for (std::size_t i = 0; i < locations.size(); ++i)
    {
        const std::string &p = locations[i].path;

        // p 가 requestPath 의 맨 앞부분(prefix)과 정확히 일치하는가?
        // (p 가 requestPath 보다 길면 compare 가 0이 아니므로 자동으로 걸러집니다.)
        if (requestPath.compare(0, p.size(), p) != 0)
            continue;

        // "경계" 검사: "/up" 이 "/upload" 를 잘못 가로채면 안 됩니다.
        // 다음 중 하나면 올바른 경계로 봅니다.
        //   1) 길이가 완전히 같다            (예: "/upload" == "/upload")
        //   2) p 가 '/' 로 끝난다            (예: "/upload/")
        //   3) 매칭 바로 뒤 글자가 '/' 이다   (예: "/upload" + "/file")
        bool boundaryOk =
            requestPath.size() == p.size() ||
            (p.size() > 0 && p[p.size() - 1] == '/') ||
            requestPath[p.size()] == '/';
        if (!boundaryOk)
            continue;

        // 더 긴(=더 구체적인) prefix 를 우선합니다.
        if (p.size() >= bestLen)
        {
            best    = &locations[i];
            bestLen = p.size();
        }
    }
    return best;
}

std::string ServerConfig::errorPageFor(int code) const
{
    std::map<int, std::string>::const_iterator it = errorPages.find(code);
    if (it != errorPages.end())
        return it->second;
    return std::string();
}
