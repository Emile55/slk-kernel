CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -DSLK_ENABLE_STRINGS -I./include

# Cible principale : compiler et tester
all: test_kernel
	./test_kernel

test_kernel: tests/test_kernel.c src/kernel.c include/slk_types.h include/slk_axioms.h
	$(CC) $(CFLAGS) -o test_kernel tests/test_kernel.c src/kernel.c

# Noyau seul (objet)
kernel.o: src/kernel.c include/slk_types.h include/slk_axioms.h
	$(CC) $(CFLAGS) -c src/kernel.c -o kernel.o

# Vérifier la taille
size: kernel.o
	size kernel.o

# Nettoyer
clean:
	rm -f test_kernel test_kernel.exe kernel.o

# Cross-compilation ARM (si arm-none-eabi-gcc installé)
arm: src/kernel.c
	arm-none-eabi-gcc -DSLK_ENABLE_STRINGS -I./include \
	    -mcpu=cortex-m4 -mthumb -O2 -std=c99 \
	    -c src/kernel.c -o kernel_arm.o
	arm-none-eabi-size kernel_arm.o

.PHONY: all size clean arm
