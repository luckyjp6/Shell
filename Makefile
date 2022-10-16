TARGET = npshell.o

npshell: $(TARGET)
	g++ -ggdb3 -o npshell $(TARGET)

npshell.o: npshell.cpp
	g++ -ggdb3 -c npshell.cpp

.PHONY: clean
clean:
	-rm edit npshell.o