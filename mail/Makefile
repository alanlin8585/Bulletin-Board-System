FLAGS=-Wall -Werror -Wshadow -std=c++14 -pthread
EXENAME=mail

mail: mail.cpp
	g++ $(FLAGS) -o $(EXENAME) mail.cpp
	
.PHONY: clean
clean:
	rm -rf $(EXENAME)
