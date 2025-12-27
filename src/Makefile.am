if COND_REPLAY
  MAYBE_REPLAY = replay
else
  MAYBE_REPLAY =
endif

if COND_CLIENT
  MAYBE_CLIENT = client
else
  MAYBE_CLIENT =
endif

SUBDIRS = common server $(MAYBE_CLIENT) $(MAYBE_REPLAY)
