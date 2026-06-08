#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
#  status.py  —  CGI 가 'Status:' 헤더로 상태코드를 정하는 예제
#  CGI 는 'Status: 404 Not Found' 처럼 응답 상태코드를 직접 지정할 수 있습니다.
#  서버는 이 헤더를 읽어 HTTP 상태코드에 반영해야 합니다.
# ----------------------------------------------------------------------------
print("Status: 404 Not Found")
print("Content-Type: text/plain; charset=utf-8")
print()
print("이 페이지는 CGI 가 직접 404 로 응답했습니다.")
