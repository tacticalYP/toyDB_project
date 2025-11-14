#PUBLICDIR= /usr0/cs564/public/project
SRC= buf.c hash.c pf.c
OBJ= buf.o hash.o pf.o
HDR = pftypes.h pf.h 

pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

# tests: testhash testpf
tests: testpf

testpf: testpf.o pflayer.o
	gcc -o testpf testpf.o pflayer.o

# testhash: testhash.o pflayer.o
# 	cc -o testhash testhash.o pflayer.o

$(OBJ): $(HDR)

# testhash.o: $(HDR)

testpf.o: $(HDR)

# lint: 
# lint $(SRC)
# install: pflayer.o 

SPAGE_OBJ = spage.o

spage.o: spage.c spage.h $(HDR)
	gcc -g -c spage.c

TEST_SPAGE_SRC = test_spage.c
TEST_SPAGE_OBJ = test_spage.o

test_spage.o: $(TEST_SPAGE_SRC) spage.h $(HDR)
	gcc -g -c test_spage.c

test_spage: $(TEST_SPAGE_OBJ) $(SPAGE_OBJ) pflayer.o
	gcc -g -o test_spage $(TEST_SPAGE_OBJ) $(SPAGE_OBJ) pflayer.o



