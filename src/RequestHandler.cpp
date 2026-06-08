/* ============================================================================
 *  RequestHandler.cpp  —  RequestHandler.hpp 의 구현
 * ----------------------------------------------------------------------------
 *  읽는 순서 추천: handle() → (메서드별) handleGet/Post/Delete → 보조 함수들.
 * ========================================================================== */

#include "RequestHandler.hpp"
#include "Utils.hpp"

#include <fstream>      // std::ofstream (업로드 저장)
#include <sstream>
#include <cstdio>       // std::remove (DELETE)
#include <dirent.h>     // opendir, readdir, closedir (autoindex)

/* -------------------------------------------------------------------------- */
/*  이 파일 안에서만 쓰는 작은 도우미들                                        */
/* -------------------------------------------------------------------------- */
namespace
{
    // 경로 두 개를 슬래시 하나로 안전하게 잇습니다.
    std::string joinPath(const std::string &a, const std::string &b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        bool aSlash = (a[a.size() - 1] == '/');
        bool bSlash = (b[0] == '/');
        if (aSlash && bSlash) return a + b.substr(1);
        if (!aSlash && !bSlash) return a + "/" + b;
        return a + b;
    }

    // 경로에 ".." 구간이 있으면 위험(상위 폴더 탈출) → 거부 대상.
    bool hasDotDot(const std::string &path)
    {
        std::vector<std::string> segs = utils::split(path, '/');
        for (std::size_t i = 0; i < segs.size(); ++i)
            if (segs[i] == "..")
                return true;
        return false;
    }

    // 경로의 마지막 조각(파일명)을 돌려줍니다. "/a/b/c.txt" -> "c.txt"
    std::string lastSegment(const std::string &path)
    {
        std::string::size_type slash = path.find_last_of('/');
        if (slash == std::string::npos) return path;
        return path.substr(slash + 1);
    }

    // 파일 확장자로 Content-Type 을 추정합니다.
    std::string contentType(const std::string &path)
    {
        std::string ext = utils::toLower(utils::getExtension(path));
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css")  return "text/css";
        if (ext == ".js")   return "application/javascript";
        if (ext == ".json") return "application/json";
        if (ext == ".png")  return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif")  return "image/gif";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".ico")  return "image/x-icon";
        if (ext == ".txt")  return "text/plain; charset=utf-8";
        return "application/octet-stream";
    }

    // location 의 허용 메서드들을 "GET, POST" 처럼 이어 붙임(405 의 Allow 헤더용).
    std::string joinMethods(const LocationConfig &loc)
    {
        std::string out;
        for (std::set<std::string>::const_iterator it = loc.methods.begin();
             it != loc.methods.end(); ++it)
        {
            if (!out.empty()) out += ", ";
            out += *it;
        }
        return out;
    }

    // 커스텀 에러페이지가 없을 때 보여줄 기본 에러 HTML.
    std::string defaultErrorPage(int code)
    {
        std::ostringstream o;
        o << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          << "<title>" << code << "</title></head><body>"
          << "<h1>" << code << " " << HttpResponse::reasonPhrase(code) << "</h1>"
          << "<hr><p>webserv</p></body></html>";
        return o.str();
    }
}

/* ==============================  생성/공통  ============================= */

RequestHandler::RequestHandler(const std::vector<const ServerConfig *> &servers)
    : _servers(servers)
{
}

const ServerConfig *RequestHandler::selectServer(const HttpRequest &req) const
{
    if (_servers.empty())
        return 0;

    // Host 헤더에서 포트(":8080")는 떼고 이름만 비교.
    std::string host = req.header("host");
    std::string::size_type colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);

    for (std::size_t i = 0; i < _servers.size(); ++i)
    {
        const ServerConfig *s = _servers[i];
        for (std::size_t j = 0; j < s->serverNames.size(); ++j)
            if (s->serverNames[j] == host)
                return s;
    }
    // 못 맞추면 첫 번째 server 가 기본 가상호스트.
    return _servers[0];
}

std::string RequestHandler::resolvePath(const LocationConfig &loc,
                                        const std::string &reqPath) const
{
    // location 경로 접두사를 떼어낸 나머지를 root 뒤에 붙입니다.
    // 예) location /kapouet (root /tmp/www), 요청 /kapouet/a/b
    //     -> 나머지 "/a/b" -> "/tmp/www/a/b"
    std::string rel = reqPath;
    if (reqPath.size() >= loc.path.size()
        && reqPath.compare(0, loc.path.size(), loc.path) == 0)
        rel = reqPath.substr(loc.path.size());

    return joinPath(loc.root, rel);
}

/* ==============================  route()  ============================== */

RequestHandler::Route RequestHandler::route(const HttpRequest &req)
{
    Route r;

    const ServerConfig *server = selectServer(req);
    if (!server)
    {
        r.response = makeError(0, 500);
        return r;
    }

    // 상위 폴더 탈출(../) 방어.
    if (hasDotDot(req.path()))
    {
        r.response = makeError(server, 403);
        return r;
    }

    // 경로에 맞는 라우트 찾기.
    const LocationConfig *loc = server->matchLocation(req.path());
    if (!loc)
    {
        r.response = makeError(server, 404);
        return r;
    }

    // 리다이렉트 설정이면 바로 이동.
    if (loc->hasRedirect)
    {
        r.response = makeRedirect(*loc);
        return r;
    }

    // HEAD 는 GET 과 같지만 본문 없이 헤더만 돌려줍니다(아래 dispatch 에서 처리).
    // 단, 허용 검사는 엄격하게: config 의 methods 에 명시된 메서드만 통과시킵니다.
    // (예: methods 가 GET 만이면 HEAD 는 405 — "GET ONLY" 의 엄격한 해석)
    const bool isHead = (req.method() == "HEAD");

    // 메서드 허용 검사.
    if (!loc->allowsMethod(req.method()))
    {
        r.response = makeError(server, 405);
        r.response.setHeader("Allow", joinMethods(*loc));
        return r;
    }

    // 이 경로에 별도 본문 제한이 있으면 검사(없으면 server 레벨 제한이 파서에서 이미 적용됨).
    if (loc->hasMaxBodySize && req.body().size() > loc->maxBodySize)
    {
        r.response = makeError(server, 413);
        return r;
    }

    // CGI 경로(확장자 매칭)면 CGI 처리 지시를 돌려줍니다.
    std::string ext = utils::getExtension(req.path());
    if (!ext.empty())
    {
        std::string interp = loc->cgiInterpreterFor(ext);
        if (!interp.empty())
        {
            // 스크립트 존재 여부는 인터프리터가 판단하게 둡니다(없으면 보통
            // 인터프리터가 빈 출력으로 끝나 502 가 됨). 일부 CGI 는 파일 없이도
            // 동작하므로 서버가 미리 404 로 막지 않습니다.
            r.isCgi       = true;
            r.scriptPath  = resolvePath(*loc, req.path());
            r.interpreter = interp;
            r.server      = server;
            return r;
        }
    }

    // 메서드별 정적 처리.
    if (req.method() == "GET" || isHead)
    {
        r.response = handleGet(req, *server, *loc);
        if (isHead)
            r.response.stripBody();     // HEAD: 헤더(Content-Length 포함)만, 본문은 제외
    }
    else if (req.method() == "POST")
        r.response = handlePost(req, *server, *loc);
    else if (req.method() == "DELETE")
        r.response = handleDelete(req, *server, *loc);
    else
        r.response = makeError(server, 501);    // GET/POST/DELETE/HEAD 외(예: PUT)

    return r;
}

/* ===============================  GET  ================================= */

HttpResponse RequestHandler::handleGet(const HttpRequest &req,
                                       const ServerConfig &server,
                                       const LocationConfig &loc)
{
    std::string fsPath = resolvePath(loc, req.path());

    if (utils::isDirectory(fsPath))
    {
        // 1) index 파일이 있으면 그걸 보여줌.
        if (!loc.index.empty())
        {
            std::string idx = joinPath(fsPath, loc.index);
            if (utils::isRegularFile(idx))
                return serveFile(idx, server);
        }
        // 2) autoindex 켜져 있으면 디렉터리 목록.
        if (loc.autoindex)
            return autoIndex(fsPath, req.path());
        // 3) index 도 목록도 없으면 보여줄 게 없음 → 404(자원 없음).
        return makeError(&server, 404);
    }

    if (utils::isRegularFile(fsPath))
        return serveFile(fsPath, server);

    return makeError(&server, 404);
}

HttpResponse RequestHandler::serveFile(const std::string &fsPath,
                                       const ServerConfig &server)
{
    bool ok = false;
    std::string body = utils::readFile(fsPath, ok);
    if (!ok)
        return makeError(&server, 404);

    HttpResponse r;
    r.setStatus(200);
    r.setHeader("Server", "webserv");
    r.setHeader("Content-Type", contentType(fsPath));
    r.setHeader("Connection", "close");
    r.setBody(body);
    return r;
}

HttpResponse RequestHandler::autoIndex(const std::string &fsPath,
                                       const std::string &reqPath)
{
    DIR *dir = opendir(fsPath.c_str());
    if (!dir)
        return makeError(0, 403);

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         << "<title>Index of " << reqPath << "</title></head><body>"
         << "<h1>Index of " << reqPath << "</h1><hr><ul>";

    // URL 의 디렉터리 부분(링크 기준). 끝에 '/' 보장.
    std::string base = reqPath;
    if (base.empty() || base[base.size() - 1] != '/')
        base += "/";

    struct dirent *entry;
    while ((entry = readdir(dir)) != 0)
    {
        std::string name = entry->d_name;
        if (name == ".")
            continue;
        std::string href = joinPath(base, name);
        html << "<li><a href=\"" << href << "\">" << name << "</a></li>";
    }
    closedir(dir);

    html << "</ul><hr><p>webserv</p></body></html>";

    HttpResponse r;
    r.setStatus(200);
    r.setHeader("Server", "webserv");
    r.setHeader("Content-Type", "text/html; charset=utf-8");
    r.setHeader("Connection", "close");
    r.setBody(html.str());
    return r;
}

/* ==============================  POST  ================================= */

HttpResponse RequestHandler::handlePost(const HttpRequest &req,
                                        const ServerConfig &server,
                                        const LocationConfig &loc)
{
    // 업로드 폴더가 없으면 본문을 저장할 곳이 없으므로, 받기만 하고 200 으로 확인.
    // (POST 가 허용된 경로인데 저장/CGI 대상이 없을 때의 합리적 기본 동작)
    if (!loc.hasUpload())
    {
        HttpResponse r;
        r.setStatus(200);
        r.setHeader("Server", "webserv");
        r.setHeader("Content-Type", "text/plain; charset=utf-8");
        r.setHeader("Connection", "close");
        r.setBody("OK\n");
        return r;
    }

    // 저장할 파일명은 요청 경로의 마지막 조각을 사용.
    std::string name = lastSegment(req.path());
    if (name.empty())
        return makeError(&server, 400);

    std::string dest = joinPath(loc.uploadStore, name);

    // 파일로 저장(일반 파일 쓰기는 poll 대상이 아님 — 과제 허용).
    std::ofstream out(dest.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return makeError(&server, 500);
    if (!req.body().empty())
        out.write(req.body().data(), static_cast<std::streamsize>(req.body().size()));
    out.close();

    HttpResponse r;
    r.setStatus(201);
    r.setHeader("Server", "webserv");
    r.setHeader("Content-Type", "text/plain; charset=utf-8");
    r.setHeader("Connection", "close");
    r.setBody("업로드 완료: " + name + "\n");
    return r;
}

/* =============================  DELETE  =============================== */

HttpResponse RequestHandler::handleDelete(const HttpRequest &req,
                                          const ServerConfig &server,
                                          const LocationConfig &loc)
{
    std::string fsPath = resolvePath(loc, req.path());

    if (!utils::pathExists(fsPath))
        return makeError(&server, 404);
    if (utils::isDirectory(fsPath))
        return makeError(&server, 403);     // 디렉터리 삭제는 막음

    // std::remove 는 C++98 표준 라이브러리 함수입니다(파일 삭제).
    if (std::remove(fsPath.c_str()) != 0)
        return makeError(&server, 500);

    HttpResponse r;
    r.setStatus(200);
    r.setHeader("Server", "webserv");
    r.setHeader("Content-Type", "text/plain; charset=utf-8");
    r.setHeader("Connection", "close");
    r.setBody("삭제 완료\n");
    return r;
}

/* ====================  리다이렉트 / 에러 생성  ======================== */

HttpResponse RequestHandler::makeRedirect(const LocationConfig &loc)
{
    int code = (loc.redirectCode != 0) ? loc.redirectCode : 302;

    HttpResponse r;
    r.setStatus(code);
    r.setHeader("Server", "webserv");
    r.setHeader("Location", loc.redirectTarget);
    r.setHeader("Content-Type", "text/html; charset=utf-8");
    r.setHeader("Connection", "close");
    r.setBody("<html><body><p>이동: <a href=\"" + loc.redirectTarget + "\">"
              + loc.redirectTarget + "</a></p></body></html>");
    return r;
}

HttpResponse RequestHandler::buildError(int code)
{
    const ServerConfig *server = _servers.empty() ? 0 : _servers[0];
    return makeError(server, code);
}

HttpResponse RequestHandler::makeError(const ServerConfig *server, int code)
{
    std::string body;

    // 설정에 커스텀 에러페이지가 있으면 그 파일을 읽어 씁니다.
    if (server)
    {
        std::string page = server->errorPageFor(code);
        if (!page.empty())
        {
            // 에러페이지 경로도 같은 라우팅 규칙으로 실제 파일 경로를 찾습니다.
            const LocationConfig *eloc = server->matchLocation(page);
            if (eloc && !eloc->hasRedirect)
            {
                std::string fp = resolvePath(*eloc, page);
                bool ok = false;
                std::string content = utils::readFile(fp, ok);
                if (ok)
                    body = content;
            }
        }
    }

    if (body.empty())
        body = defaultErrorPage(code);

    HttpResponse r;
    r.setStatus(code);
    r.setHeader("Server", "webserv");
    r.setHeader("Content-Type", "text/html; charset=utf-8");
    r.setHeader("Connection", "close");
    r.setBody(body);
    return r;
}
