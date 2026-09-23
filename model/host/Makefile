# caique AICA test cases, model build: one host binary per case, linked with the model.  Use ../run_model.sh CASE...
CASES = $(basename $(notdir $(wildcard ../cases/*.c)))
CFLAGS = -O2 -g -Wall -Wno-unused-function
CXXFLAGS = -O2 -g -Wall -std=c++17
HDRS = $(wildcard ../cases/*.h ../src/*.h)
all: $(CASES)
aica_model.o: ../src/aica_model.cpp $(HDRS)
	g++ $(CXXFLAGS) -c -o $@ $<
io_model.o: io_model.cpp $(HDRS)
	g++ $(CXXFLAGS) -c -o $@ $<
%.case.o: ../cases/%.c $(HDRS)
	gcc $(CFLAGS) -std=gnu11 -DCASE=\"$*\" -c -o $@ $<
$(CASES): %: %.case.o io_model.o aica_model.o
	g++ -o $@ $^
clean:
	-rm -f *.o $(CASES)
