#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
#  hello.py  —  가장 단순한 CGI 예제
#  서버가 넘겨준 환경변수(메서드/쿼리/경로)를 그대로 출력해 보여줍니다.
#  CGI 출력 규칙: 먼저 헤더(예: Content-Type)를 찍고, 빈 줄 하나 뒤에 본문.
# ----------------------------------------------------------------------------
import os

print("Content-Type: text/plain; charset=utf-8")
print()                                          # 헤더와 본문을 가르는 빈 줄
print("Hello from CGI (Python)")
print("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY_STRING=" + os.environ.get("QUERY_STRING", ""))
print("PATH_INFO=" + os.environ.get("PATH_INFO", ""))
print("SERVER_PORT=" + os.environ.get("SERVER_PORT", ""))
