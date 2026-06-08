/* ============================================================================
 *  Server.hpp  —  웹 서버의 심장: 리스닝 소켓 + 단일 poll() 루프
 * ----------------------------------------------------------------------------
 *  이 클래스가 하는 일을 한 문장으로:
 *   "설정에 적힌 포트들에서 연결을 기다리다가, 단 하나의 poll() 로 모든 소켓을
 *    감시하면서, 읽을 게 있으면 읽고 쓸 수 있으면 쓴다."
 *
 *  채점표의 0점 사유를 피하기 위한 핵심 설계:
 *   - listen 소켓과 모든 클라이언트 소켓을 '하나의' poll() 로 감시 (단일 poll).
 *   - poll() 이 준비됐다고 한 fd 에만 accept/recv/send 수행.
 *   - 모든 소켓은 논블로킹(O_NONBLOCK).
 *   - recv/send 후 errno 를 보지 않고 반환값만으로 판단.
 *
 *  fd 의 종류:
 *   - 리스닝 소켓(listen fd) : 새 손님을 받는(accept) 입구. _listenFds 에 보관.
 *   - 클라이언트 소켓        : 손님 한 명과의 연결. _connections 에 Connection* 로 보관.
 * ========================================================================== */

#ifndef SERVER_HPP
#define SERVER_HPP

#include "Config.hpp"
#include "Connection.hpp"

#include <vector>
#include <map>
#include <string>
#include <poll.h>       // struct pollfd, poll()

class Server
{
public:
    explicit Server(const Config &config);
    ~Server();

    // 설정에 따라 리스닝 소켓들을 만들고 bind/listen 합니다.
    // 실패하면 std::runtime_error 를 던집니다.
    void setup();

    // 단일 poll() 루프를 돕니다. 종료 신호(SIGINT 등)를 받을 때까지 블로킹합니다.
    void run();

private:
    const Config &                                    _config;
    std::vector<int>                                  _listenFds;       // 리스닝 소켓들
    // 리스닝 소켓 fd -> 그 host:port 에 묶인 server 블록들(가상호스트 후보).
    std::map<int, std::vector<const ServerConfig *> > _listenerServers;
    // 클라이언트 소켓 fd -> 그 연결을 다루는 Connection.
    std::map<int, Connection *>                       _connections;

    // host:port 하나에 대한 리스닝 소켓을 만들어 fd 를 돌려줍니다(실패 시 throw).
    int  createListenSocket(const std::string &host, int port);

    // 같은 host:port 는 한 번만 bind 하도록 묶어서 리스닝 소켓들을 준비.
    void setupListeners();

    // 매 루프마다 poll 용 배열과 "fd -> 그 fd 를 소유한 Connection" 맵을 구성.
    // (한 Connection 이 소켓 + CGI 파이프 등 여러 fd 를 가질 수 있으므로 맵이 필요)
    void buildPollSet(std::vector<struct pollfd> &pfds,
                      std::map<int, Connection *> &owners) const;

    // 모든 연결에 "한가할 때 틱"을 보냅니다(CGI 타임아웃 감시).
    void tickConnections();

    // 리스닝 소켓에 새 연결이 왔을 때 accept 해서 Connection 생성.
    void acceptNewClient(int listenFd);

    // 클라이언트 연결을 닫고 정리(소켓 close + Connection 삭제 + 맵에서 제거).
    void closeConnection(int fd);

    // 종료 시 남은 모든 소켓/연결 정리.
    void closeAll();

    // 주어진 fd 가 리스닝 소켓인지.
    bool isListener(int fd) const;

    // 소켓을 논블로킹으로 설정. (fcntl 은 F_SETFL + O_NONBLOCK 만 사용 — 과제 제약)
    static bool setNonBlocking(int fd);

    // 복사 금지(소켓/연결을 소유하는 객체).
    Server(const Server &);
    Server &operator=(const Server &);
};

#endif // SERVER_HPP
