FLAGS=-Wall -Werror -Wshadow -std=c++14
EXENAME=board

board: board.cpp
	g++ $(FLAGS) -o $(EXENAME) board.cpp
	
.PHONY: clean
clean:
	rm -rf $(EXENAME)
