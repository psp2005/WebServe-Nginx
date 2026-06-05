/* ============================================================================
 *  Server.cpp  —  Server.hpp 의 구현 (소켓 준비 + 단일 poll 루프)
 * ----------------------------------------------------------------------------
 *  읽는 순서 추천:
 *    1) setup()/setupListeners()/createListenSocket() : 입구(리스닝 소켓) 만들기
 *    2) run()              : 단일 poll() 루프 본체
 *    3) buildPollFds()     : 매 회 감시 대상 목록 구성
 *    4) acceptNewClient()  : 새 손님 받기
 *    5) closeConnection()  : 손님 정리
 * ========================================================================== */

#include "Server.hpp"
#include "Utils.hpp"

#include <stdexcept>        // std::runtime_error
#include <cstring>          // std::memset
#include <csignal>          // signal, SIG_IGN
#include <cstddef>          // std::size_t
#include <unistd.h>         // close
#include <fcntl.h>          // fcntl, F_SETFL, O_NONBLOCK
#include <sys/socket.h>     // socket, bind, listen, accept, setsockopt
#include <netdb.h>          // getaddrinfo, freeaddrinfo, gai_strerror

/* -------------------------------------------------------------------------- */
/*  종료 신호 처리                                                            */
/*  poll 루프가 무한히 돌기 때문에, Ctrl-C(SIGINT) 같은 신호로 깔끔히 멈출 수  */
/*  있어야 합니다. 신호 핸들러에서는 '플래그 하나만' 건드리는 게 안전합니다.   */
/* -------------------------------------------------------------------------- */
namespace
{
    volatile sig_atomic_t g_stop = 0;

    void onStopSignal(int)
    {
        g_stop = 1;
    }
}

/* ==============================  생성/소멸  ============================== */

Server::Server(const Config &config)
    : _config(config),
      _listenFds(),
      _listenerServers(),
      _connections()
{
}

Server::~Server()
{
    closeAll();
}

/* ===============================  setup  ================================ */

void Server::setup()
{
    setupListeners();

    // SIGPIPE 무시: 이미 닫힌 소켓에 send 하면 기본적으로 프로세스가 죽습니다.
    // 서버가 그런 이유로 죽으면 안 되므로 무시하고, send 의 반환값으로만 처리합니다.
    signal(SIGPIPE, SIG_IGN);
    // 종료 신호를 받으면 루프를 빠져나오도록 핸들러 등록.
    signal(SIGINT,  onStopSignal);
    signal(SIGTERM, onStopSignal);
}

void Server::setupListeners()
{
    // 같은 host:port 가 여러 server 블록에 나오면 소켓은 '한 번만' 만들고,
    // 그 host:port 를 공유하는 server 들을 한데 모읍니다(가상호스트 라우팅용).
    // 키 "host:port" -> 리스닝 소켓 fd.
    std::map<std::string, int> made;

    for (std::size_t i = 0; i < _config.servers.size(); ++i)
    {
        const ServerConfig &sv = _config.servers[i];
        std::string key = sv.host + ":" + utils::toString(sv.port);

        std::map<std::string, int>::iterator it = made.find(key);
        int fd;
        if (it == made.end())
        {
            // 이 host:port 는 처음 → 실제로 소켓을 만든다.
            fd = createListenSocket(sv.host, sv.port);
            made[key] = fd;
            _listenFds.push_back(fd);
        }
        else
        {
            fd = it->second;    // 이미 만든 소켓 재사용
        }
        // 이 server 블록을 해당 리스닝 소켓의 후보 목록에 추가.
        _listenerServers[fd].push_back(&sv);
    }
}

int Server::createListenSocket(const std::string &host, int port)
{
    // getaddrinfo 로 host:port 를 실제 주소 구조체로 변환합니다.
    // (inet_addr/inet_pton 은 과제 허용 함수가 아니므로 getaddrinfo 사용)
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags    = AI_PASSIVE;     // bind 용(서버) 주소

    std::string portStr = utils::toString(port);
    const char *node = host.empty() ? 0 : host.c_str();

    struct addrinfo *res = 0;
    int gai = getaddrinfo(node, portStr.c_str(), &hints, &res);
    if (gai != 0)
        throw std::runtime_error("getaddrinfo 실패 (" + host + ":" + portStr + "): "
                                 + gai_strerror(gai));

    int fd = -1;
    for (struct addrinfo *p = res; p != 0; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;

        // SO_REUSEADDR: 서버를 막 껐다 켜도 "주소가 이미 사용 중" 없이 다시 bind 가능.
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;          // bind 성공

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        throw std::runtime_error("bind 실패: " + host + ":" + portStr);

    // 연결 대기열 크기 128. 손님이 몰려도 어느 정도 줄 세워둠.
    if (listen(fd, 128) < 0)
    {
        close(fd);
        throw std::runtime_error("listen 실패: " + host + ":" + portStr);
    }

    // 논블로킹 설정(accept 가 막히지 않도록).
    if (!setNonBlocking(fd))
    {
        close(fd);
        throw std::runtime_error("논블로킹 설정 실패: " + host + ":" + portStr);
    }

    return fd;
}

bool Server::setNonBlocking(int fd)
{
    // 과제 제약상 fcntl 은 F_SETFL + O_NONBLOCK 만 사용합니다(F_GETFL 사용 불가).
    // 소켓은 다른 상태 플래그가 필요 없으므로 O_NONBLOCK 만 설정해도 충분합니다.
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        return false;
    return true;
}

/* ================================  run  ================================ */

void Server::run()
{
    while (!g_stop)
    {
        // (1) 지금 감시할 fd 목록을 새로 구성.
        std::vector<struct pollfd> pfds;
        buildPollFds(pfds);
        if (pfds.empty())
            break;      // 감시할 소켓이 하나도 없으면 종료(정상적으로는 도달 안 함)

        // (2) 단일 poll() 로 모든 소켓의 읽기/쓰기 준비를 동시에 기다림.
        //     타임아웃 500ms: 그 사이 종료 신호가 오면 다음 루프에서 빠져나가기 위함.
        int ready = poll(&pfds[0], pfds.size(), 500);
        if (ready < 0)
        {
            // poll 자체가 중단(예: 신호로 EINTR)될 수 있음. 종료 요청이면 끝내고,
            // 아니면 다음 루프로. (read/write 가 아니므로 errno 금지 대상은 아님)
            if (g_stop)
                break;
            continue;
        }
        if (ready == 0)
            continue;   // 타임아웃: 할 일 없음

        // (3) 준비된 fd 들을 하나씩 처리.
        //     pfds 는 이번 회차의 '스냅샷'이라, 도중에 연결을 닫아도 안전합니다.
        for (std::size_t i = 0; i < pfds.size(); ++i)
        {
            short re = pfds[i].revents;
            if (re == 0)
                continue;

            int fd = pfds[i].fd;

            // (a) 리스닝 소켓에 신호 → 새 손님 받기
            if (isListener(fd))
            {
                if (re & POLLIN)
                    acceptNewClient(fd);
                continue;
            }

            // (b) 클라이언트 소켓
            std::map<int, Connection *>::iterator it = _connections.find(fd);
            if (it == _connections.end())
                continue;       // 이미 닫힌 fd
            Connection *conn = it->second;

            bool keep = true;
            if (re & (POLLERR | POLLHUP | POLLNVAL))
            {
                keep = false;   // 오류/상대 종료 → 닫기
            }
            else
            {
                if (keep && (re & POLLIN))
                    keep = conn->onReadable();
                if (keep && (re & POLLOUT))
                    keep = conn->onWritable();
            }

            if (!keep)
                closeConnection(fd);
        }
    }

    closeAll();
}

void Server::buildPollFds(std::vector<struct pollfd> &pfds) const
{
    pfds.clear();

    // 리스닝 소켓: 새 연결을 받기 위해 항상 읽기(POLLIN) 감시.
    for (std::size_t i = 0; i < _listenFds.size(); ++i)
    {
        struct pollfd p;
        p.fd      = _listenFds[i];
        p.events  = POLLIN;
        p.revents = 0;
        pfds.push_back(p);
    }

    // 클라이언트 연결: 각자 원하는 방향(읽기/쓰기)만 감시.
    for (std::map<int, Connection *>::const_iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        const Connection *conn = it->second;
        int events = 0;
        if (conn->wantsRead())
            events |= POLLIN;
        if (conn->wantsWrite())
            events |= POLLOUT;

        struct pollfd p;
        p.fd      = it->first;
        p.events  = static_cast<short>(events);
        p.revents = 0;
        pfds.push_back(p);
    }
}

void Server::acceptNewClient(int listenFd)
{
    // poll 이 POLLIN 을 줬을 때만 호출 → accept 1회.
    // (논블로킹이라 대기 중 연결이 더 있으면 다음 poll 에서 또 POLLIN 으로 알려줌)
    int clientFd = accept(listenFd, 0, 0);
    if (clientFd < 0)
        return;     // 받을 게 없거나 일시적 오류 → 다음 기회에(errno 안 봄)

    if (!setNonBlocking(clientFd))
    {
        close(clientFd);
        return;
    }

    // 이 리스닝 소켓에 묶인 server 후보들을 넘겨 Connection 생성.
    _connections[clientFd] = new Connection(clientFd, _listenerServers[listenFd]);
}

void Server::closeConnection(int fd)
{
    std::map<int, Connection *>::iterator it = _connections.find(fd);
    if (it == _connections.end())
        return;

    delete it->second;      // Connection 객체 해제
    _connections.erase(it); // 맵에서 제거
    close(fd);              // 소켓 닫기(소켓 close 책임은 Server)
}

void Server::closeAll()
{
    // 남은 클라이언트 연결 모두 정리.
    for (std::map<int, Connection *>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        delete it->second;
        close(it->first);
    }
    _connections.clear();

    // 리스닝 소켓 모두 닫기.
    for (std::size_t i = 0; i < _listenFds.size(); ++i)
        close(_listenFds[i]);
    _listenFds.clear();
    _listenerServers.clear();
}

bool Server::isListener(int fd) const
{
    for (std::size_t i = 0; i < _listenFds.size(); ++i)
        if (_listenFds[i] == fd)
            return true;
    return false;
}
