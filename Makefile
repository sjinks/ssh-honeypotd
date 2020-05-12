TARGET = ssh-honeypotd
C_SRC  = main.c globals.c cmdline.c pidfile.c daemon.c worker.c
C_DEPS = $(patsubst %.c,%.d,$(C_SRC))
OBJS   = $(patsubst %.c,%.o,$(C_SRC))

all: $(TARGET)

ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif

ssh-honeypotd: $(OBJS)
	$(CC) $^ -lssh -lssh_threads -pthread $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) -fvisibility=hidden -Wall -Werror $(CFLAGS) -c "$<" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@"

clean: objclean depclean
	-rm -f $(TARGET)

objclean:
	-rm -f $(OBJS)

depclean:
	-rm -f $(C_DEPS)

.PHONY: clean
