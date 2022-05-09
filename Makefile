PROJ=proj2
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic -pthread
CC=gcc
RM=rm -f

.PHONY: processes processes_clean

$(PROJ) : $(PROJ).c

pack:
	zip proj2.zip proj2.c Makefile

processes:
	ps ux | grep "proj2"

processes_clean:
	killall proj2

clean :
	$(RM) $(PROJ)