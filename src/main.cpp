/* ============================================================================
 *  main.cpp  —  프로그램의 진입점(시작 지점)
 * ----------------------------------------------------------------------------
 *  실행 방법(과제 규정):   ./webserv [설정파일]
 *    - 설정파일을 인자로 주면 그 파일을 사용합니다.
 *    - 안 주면 기본 경로 config/default.conf 를 사용합니다.
 *
 *  하는 일은 단순합니다:
 *    1) 설정 파일을 읽어 Config 로 파싱한다.
 *    2) 그 설정으로 Server 를 만들고 소켓을 준비한다(setup).
 *    3) 단일 poll 루프를 돈다(run) — 여기서 사실상 계속 머무릅니다.
 *  어디서든 오류가 나면 예외를 잡아 메시지를 보여주고 1 을 반환합니다.
 * ========================================================================== */

#include "ConfigParser.hpp"
#include "Server.hpp"

#include <iostream>
#include <exception>

int main(int argc, char **argv)
{
    // 인자는 0개(기본 경로) 또는 1개(설정 파일)만 허용.
    if (argc > 2)
    {
        std::cerr << "사용법: " << argv[0] << " [설정파일]\n";
        return 1;
    }
    std::string path = (argc == 2) ? argv[1] : "config/default.conf";

    try
    {
        // (1) 설정 파싱
        ConfigParser parser;
        Config config = parser.parse(path);

        // (2) 서버 준비(소켓 bind/listen)
        Server server(config);
        server.setup();

        std::cout << "webserv 시작됨 (설정: " << path << "). 종료하려면 Ctrl-C.\n";

        // (3) 단일 poll 루프 — 종료 신호를 받을 때까지 여기 머무름.
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "치명적 오류: " << e.what() << "\n";
        return 1;
    }

    std::cout << "webserv 정상 종료.\n";
    return 0;
}
