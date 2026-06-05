# ============================================================================
#  Webserv Makefile
# ----------------------------------------------------------------------------
#  과제 규칙(General rules)이 요구하는 필수 규칙:
#     $(NAME), all, clean, fclean, re
#  그리고 "불필요한 재링크(relinking)를 하지 말 것" 요구사항을 만족시키기 위해
#  헤더 의존성 자동 추적(-MMD -MP)을 사용합니다.
#
#  ▶ 한 줄 요약: 소스(.cpp)들을 컴파일해 오브젝트(.o)로 만들고,
#               그것들을 한데 묶어(link) 'webserv' 실행 파일을 만든다.
# ============================================================================

# ---- 실행 파일 이름 (과제에서 정한 프로그램 이름) ----
NAME        := webserv

# ---- 컴파일러와 플래그 ----
#  CXX     : C++ 컴파일러
#  CXXFLAGS: 과제가 강제하는 경고 플래그 3종 + C++98 표준 고정
#            -Wall  : 일반적인 경고 모두 켜기
#            -Wextra: 추가 경고까지 켜기
#            -Werror: 경고를 '에러'로 취급(경고 0개를 강제)
#            -std=c++98 : C++98 표준만 사용하도록 제한
CXX         := c++
CXXFLAGS    := -Wall -Wextra -Werror -std=c++98
#  -MMD -MP : 각 .o를 만들 때 .d(의존성) 파일을 같이 생성.
#             헤더가 바뀌면 그 헤더를 쓰는 소스만 다시 컴파일 → 불필요한 재빌드 방지.
DEPFLAGS    := -MMD -MP

# ---- 디렉터리 구조 ----
SRC_DIR     := src
INC_DIR     := include
OBJ_DIR     := obj

# ---- 소스 목록 ----
#  src 폴더의 모든 .cpp 파일을 자동으로 수집한다.
SRCS        := $(wildcard $(SRC_DIR)/*.cpp)
#  각 src/foo.cpp  ->  obj/foo.o 로 대응시킨다.
OBJS        := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
#  .o 마다 생성되는 .d(의존성) 파일 목록.
DEPS        := $(OBJS:.o=.d)

# 헤더는 -I 로 포함 경로를 알려준다.
INCLUDES    := -I$(INC_DIR)

# ---- 기본 목표: 'make' 만 치면 all 이 실행됨 ----
all: $(NAME)

# ---- 링크 단계: 오브젝트들을 모아 실행 파일 생성 ----
$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo "✅  $(NAME) 빌드 완료"

# ---- 컴파일 단계: src/%.cpp -> obj/%.o ----
#  | $(OBJ_DIR) : obj 폴더가 먼저 존재해야 한다는 'order-only' 선행조건
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

# ---- obj 폴더 생성 ----
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ---- clean: 중간 산출물(.o, .d)만 삭제 ----
clean:
	rm -rf $(OBJ_DIR)
	@echo "🧹  오브젝트 파일 삭제"

# ---- fclean: 중간 산출물 + 실행 파일까지 모두 삭제 ----
fclean: clean
	rm -f $(NAME)
	@echo "🧹  실행 파일까지 삭제"

# ---- re: 처음부터 다시 빌드 (fclean 후 all) ----
re: fclean all

# ---- 가짜 목표 선언: 같은 이름의 파일이 있어도 항상 실행되도록 ----
.PHONY: all clean fclean re

# ---- 자동 생성된 의존성 파일들을 포함(있을 때만) ----
-include $(DEPS)
