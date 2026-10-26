# PC test build (no devkitPPC needed): runs CubeVerse on a PC, drawing
# through gxemu.c and talking to server.py instead of Juxt, for screenshots.
SRC = ../source
CFLAGS = -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-format-truncation -I inc -I $(SRC) -I ../third_party/stb -DPC_TEST
# the secure-connection code needs mbedTLS and the GameCube's network library
APP = $(filter-out $(SRC)/sound.c $(SRC)/conn.c $(SRC)/castore.c $(SRC)/gc_entropy.c $(SRC)/netio.c $(SRC)/https.c, $(wildcard $(SRC)/*.c))
apptest: apptest.c gxemu.c pc_sound.c conn_pc.c $(APP)
	gcc $(CFLAGS) -o $@ $^ -lm -lpthread
parsetest: parsetest.c $(SRC)/jparse.c $(SRC)/html.c $(SRC)/jfont.c $(SRC)/inflate.c
	gcc $(CFLAGS) -o $@ $^
sfxpreview: sfxpreview.c $(SRC)/sfxsynth.c $(SRC)/musicsynth.c
	gcc $(CFLAGS) -o $@ $^ -lm
