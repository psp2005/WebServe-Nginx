/* ============================================================================
 *  CgiHandler.cpp  —  CgiHandler.hpp 의 구현
 * ----------------------------------------------------------------------------
 *  start() 의 fork 이후 '자식'은 stdin/stdout 을 파이프로 바꿔치기(dup2)하고
 *  스크립트 폴더로 이동(chdir)한 뒤 인터프리터를 execve 로 실행합니다.
 *  '부모'는 논블로킹 파이프 fd 두 개를 들고 돌아와, 이후 Connection 이 그
 *  파이프들을 poll 로 감시하며 본문을 써넣고 출력을 읽어갑니다.
 * ========================================================================== */

#include "CgiHandler.hpp"
#include "Utils.hpp"

#include <vector>
#include <sstream>
#include <unistd.h>     // pipe, fork, dup2, close, execve, chdir, _exit, STDIN_FILENO
#include <fcntl.h>      // fcntl, F_SETFL, O_NONBLOCK

namespace
{
    // 파이프 fd 를 논블로킹으로. (fcntl 은 F_SETFL + O_NONBLOCK 만 사용 — 과제 제약)
    bool setNonBlocking(int fd)
    {
        return fcntl(fd, F_SETFL, O_NONBLOCK) >= 0;
    }

    // 헤더 이름을 CGI 환경변수 형태로: "Content-Type" -> "HTTP_CONTENT_TYPE"
    std::string toEnvName(const std::string &headerName)
    {
        std::string out = "HTTP_";
        for (std::size_t i = 0; i < headerName.size(); ++i)
        {
            char c = headerName[i];
            if (c == '-')
                out += '_';
            else if (c >= 'a' && c <= 'z')
                out += static_cast<char>(c - 'a' + 'A');
            else
                out += c;
        }
        return out;
    }
}

std::vector<std::string> CgiHandler::buildEnv(const HttpRequest &req,
                                              const std::string &scriptPath,
                                              const ServerConfig &server)
{
    std::vector<std::string> env;

    // CGI/1.1 규약에서 자주 쓰이는 표준 변수들.
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_PROTOCOL=HTTP/1.1");
    env.push_back("SERVER_SOFTWARE=webserv");
    env.push_back("REDIRECT_STATUS=200");           // php-cgi 등이 요구(무해)
    env.push_back("REQUEST_METHOD=" + req.method());
    env.push_back("SCRIPT_NAME=" + req.path());
    env.push_back("SCRIPT_FILENAME=" + scriptPath);
    env.push_back("PATH_INFO=" + scriptPath);
    env.push_back("PATH_TRANSLATED=" + scriptPath);
    env.push_back("QUERY_STRING=" + req.query());
    env.push_back("REMOTE_ADDR=127.0.0.1");

    // 본문 길이/타입: POST 데이터 처리에 필요.
    env.push_back("CONTENT_LENGTH=" + utils::toString(static_cast<long>(req.body().size())));
    env.push_back("CONTENT_TYPE=" + req.header("content-type"));

    // 서버 이름/포트.
    std::string serverName = server.serverNames.empty() ? server.host : server.serverNames[0];
    env.push_back("SERVER_NAME=" + serverName);
    env.push_back("SERVER_PORT=" + utils::toString(server.port));

    // 요청 헤더 전체를 HTTP_* 로도 전달.
    const std::map<std::string, std::string> &hs = req.headers();
    for (std::map<std::string, std::string>::const_iterator it = hs.begin();
         it != hs.end(); ++it)
        env.push_back(toEnvName(it->first) + "=" + it->second);

    return env;
}

bool CgiHandler::start(const HttpRequest &req,
                       const std::string &scriptPath,
                       const std::string &interpreter,
                       const ServerConfig &server,
                       Process &out)
{
    // 환경변수는 fork 전에 만들어 둡니다(자식이 복사본을 갖게 됨).
    std::vector<std::string> envStrings = buildEnv(req, scriptPath, server);

    // 스크립트의 폴더와 파일명 분리(자식이 폴더로 chdir 후 파일명으로 실행).
    std::string dir = ".";
    std::string name = scriptPath;
    std::string::size_type slash = scriptPath.find_last_of('/');
    if (slash != std::string::npos)
    {
        dir  = scriptPath.substr(0, slash);
        name = scriptPath.substr(slash + 1);
        if (dir.empty())
            dir = "/";
    }

    int inPipe[2];
    int outPipe[2];
    if (pipe(inPipe) < 0)
        return false;
    if (pipe(outPipe) < 0)
    {
        close(inPipe[0]);
        close(inPipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        return false;
    }

    if (pid == 0)
    {
        // ── 자식 프로세스 ──
        // stdin <- inPipe 읽기끝,  stdout -> outPipe 쓰기끝 으로 연결.
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);

        // 상대경로 파일 접근을 위해 스크립트 폴더로 이동.
        chdir(dir.c_str());

        // execve 에 넘길 argv / envp 를 char* 배열로 구성.
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(interpreter.c_str()));
        argv.push_back(const_cast<char *>(name.c_str()));
        argv.push_back(0);

        std::vector<char *> envp;
        for (std::size_t i = 0; i < envStrings.size(); ++i)
            envp.push_back(const_cast<char *>(envStrings[i].c_str()));
        envp.push_back(0);

        execve(interpreter.c_str(), &argv[0], &envp[0]);

        // 여기 도달하면 execve 실패 → 즉시 종료(부모는 빈 출력 EOF 로 인지).
        _exit(1);
    }

    // ── 부모 프로세스 ──
    // 자식 쪽 파이프 끝은 닫고, 부모가 쓸 끝만 남겨 논블로킹으로 설정.
    close(inPipe[0]);
    close(outPipe[1]);
    setNonBlocking(inPipe[1]);
    setNonBlocking(outPipe[0]);

    out.pid   = pid;
    out.inFd  = inPipe[1];
    out.outFd = outPipe[0];
    return true;
}

HttpResponse CgiHandler::buildResponse(const std::string &cgiOutput)
{
    HttpResponse res;
    res.setStatus(200);
    res.setHeader("Server", "webserv");

    // 헤더와 본문의 경계(빈 줄)를 찾습니다("\r\n\r\n" 우선, 없으면 "\n\n").
    std::string::size_type pos = cgiOutput.find("\r\n\r\n");
    std::size_t sepLen = 4;
    if (pos == std::string::npos)
    {
        pos = cgiOutput.find("\n\n");
        sepLen = 2;
    }

    std::string headerPart;
    std::string body;
    bool hasHeaders = false;

    if (pos != std::string::npos)
    {
        headerPart = cgiOutput.substr(0, pos);
        // 앞부분에 ':' 가 있으면 헤더로 간주(아니면 전체를 본문 취급).
        if (headerPart.find(':') != std::string::npos)
        {
            hasHeaders = true;
            body = cgiOutput.substr(pos + sepLen);
        }
    }

    if (!hasHeaders)
    {
        // CGI 가 헤더 없이 본문만 출력한 경우.
        res.setHeader("Content-Type", "text/html; charset=utf-8");
        res.setBody(cgiOutput);
        return res;
    }

    // 헤더들을 한 줄씩 해석.
    bool hasContentType = false;
    std::vector<std::string> lines = utils::split(headerPart, '\n');
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        std::string line = lines[i];
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            continue;

        std::string::size_type colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name  = utils::trim(line.substr(0, colon));
        std::string value = utils::trim(line.substr(colon + 1));

        if (utils::toLower(name) == "status")
        {
            // "Status: 404 Not Found" -> 404
            std::istringstream iss(value);
            int code = 0;
            iss >> code;
            if (code >= 100 && code <= 599)
                res.setStatus(code);
        }
        else
        {
            res.setHeader(name, value);
            if (utils::toLower(name) == "content-type")
                hasContentType = true;
        }
    }
    if (!hasContentType)
        res.setHeader("Content-Type", "text/html; charset=utf-8");

    res.setBody(body);      // Content-Length 를 본문 길이에 맞춰 재설정
    return res;
}
