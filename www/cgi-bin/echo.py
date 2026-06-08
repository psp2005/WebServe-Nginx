#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
#  echo.py  —  POST 본문을 그대로 되돌려주는 CGI 예제
#  표준입력(stdin)으로 들어온 요청 본문을 읽어, 그대로 본문에 echo 합니다.
#  (서버가 본문을 CGI stdin 으로 잘 흘려보내는지 검증용)
# ----------------------------------------------------------------------------
import sys

data = sys.stdin.read()                          # 요청 본문(POST 데이터) 읽기

sys.stdout.write("Content-Type: text/plain; charset=utf-8\r\n")
sys.stdout.write("\r\n")                          # 헤더 끝 빈 줄
sys.stdout.write("ECHO[" + data + "]")
