CC=nvcc  
LNK=nvcc 
OPT=  -Xptxas -O3 
EXTRA= -g  --std=c++17 --expt-relaxed-constexpr --expt-extended-lambda
EXTRA+= -gencode arch=compute_60,code=sm_60 
OBJ= hashmap.o splitvector.o

default: examples
examples: ${OBJ}

allclean:
	rm hashmap&
	rm splitvector

hashmap.o: hashmap.cu
	${CC} ${OPT} ${EXTRA}   -o hashmap hashmap.cu

splitvector.o: splitvector.cu
	${CC} ${OPT} ${EXTRA}   -o splitvector splitvector.cu
