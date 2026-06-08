#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
#  slow.py  —  일부러 2초 걸리는 CGI (비동기 검증용)
#  이 스크립트가 도는 동안에도 서버가 '다른' 요청을 막힘없이 처리해야
#  단일 poll 논블로킹 설계가 올바른 것입니다.
# ----------------------------------------------------------------------------
import time

time.sleep(2)
print("Content-Type: text/plain; charset=utf-8")
print()
print("slow CGI done (2s)")
