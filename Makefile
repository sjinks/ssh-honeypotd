TARGET = ssh-honeypotd
C_SRC  = main.c globals.c cmdline.c pidfile.c daemon.c worker.c log.c
C_DEPS = $(patsubst %.c,%.d,$(C_SRC))
OBJS   = $(patsubst %.c,%.o,$(C_SRC))
LIBFLAGS = -lssh -lssh_threads -pthread

all: $(TARGET)

ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif

ssh-honeypotd: $(OBJS)
	$(CC) $^ $(LIBFLAGS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) -fvisibility=hidden -Wall -Werror -Wno-error=attributes $(CFLAGS) -c "$<" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@"

clean: objclean depclean
	-rm -f $(TARGET)

objclean:
	-rm -f $(OBJS)

depclean:
	-rm -f $(C_DEPS)

keys:
	mkdir -p keys
	ssh-keygen -f keys/ssh_host_dsa_key -N '' -t dsa
	ssh-keygen -f keys/ssh_host_rsa_key -N '' -t rsa
	ssh-keygen -f keys/ssh_host_ecdsa_key -N '' -t ecdsa
	ssh-keygen -f keys/ssh_host_ed25519_key -N '' -t ed25519

docker-build: $(TARGET) keys

.PHONY: clean
