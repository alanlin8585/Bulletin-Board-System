FLAGS=-Wall -Werror -Wshadow -std=c++14
EXENAME=chatroom

chat: chatroom.cpp
	g++ $(FLAGS) -o $(EXENAME) chatroom.cpp
	
.PHONY: clean
clean:
	rm -rf $(EXENAME)
