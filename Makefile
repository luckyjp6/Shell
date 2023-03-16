TARGET = shell.o

shell: $(TARGET)
	g++ -ggdb3 -o shell $(TARGET)

shell.o: shell.cpp
	g++ -ggdb3 -c shell.cpp

.PHONY: clean
clean:
	-rm shell.o shell