# CubeVerse

Juxtaposition, Pretendo Network's Miiverse revival, on the Nintendo GameCube. Read and post in communities, give Yeahs, comment, draw, and follow people, from a menu built like the GameCube's own.

CubeVerse is homebrew, made with the blessing of the Juxt team. Download the latest `cubeverse` zip from [Releases](https://github.com/Arial-Bold/CubeVerse/releases), copy it to your SD card, and launch `cubeverse.dol` from Swiss or any other DOL loader.

You need a network adapter (Broadband Adapter, or a WIZnet-based adapter such as the ETH2GC) and a Pretendo Network ID.

## Features

### Reading
- **Activity:** your feed, posts from people you follow, or everyone (My Feed, People, Global).
- **Communities:** popular and new communities, or all of them, each with its icon, name and followers.
- **A community:** its newest or most popular posts, Follow, and Post here.
- **A post:** the post, big, with its comments.
- **Notifications:** Yeahs, comments and new followers. The number of new ones shows on the home screen.
- **My page** and anyone's page: their Mii, bio, followers, and their posts or Yeahs.
- **Japanese** posts show in Japanese (kana and about 6,300 kanji).
- Pictures (Mii faces, icons, drawings, screenshots) are kept on the SD card (`cubeverse/cache`, up to 48 MB), so pages you've seen before load quickly.

### Joining in
- **Yeah!** any post with X or its button.
- **Write** posts and comments with the on-screen keyboard. Pick a feeling and mark spoilers.
- **Draw** on the drawing pad (Juxt's 320 x 120 black and white): three pen sizes, an eraser, undo and clear.
- **Follow** communities and people.

### Signing in
- Type your Pretendo Network ID and password with the on-screen keyboard, or scan the QR code with a phone on the same network and sign in there.
- CubeVerse remembers your ID and password on the SD card (scrambled, in `cubeverse/account.ini`) so it can sign you in again by itself when Juxt's session runs out. Sign out in Settings to forget them.
- Juxt only lets accounts that have opened Juxt once on a Wii U or 3DS use it, so a brand-new account needs that first.

### Settings (START on the home screen)
- 4:3 or 16:9, standard (640) or full (704) width, screen margin, video mode.
- Opening animation, pointer speed, menu sounds and music, volumes.
- Your Pretendo account (sign out), software updates, and what Exit does.

## Controls

| | |
|---|---|
| Control Stick | Move the pointer (turn the cubes on the home screen) |
| A | Click |
| B | Back |
| D-pad | Hop the pointer between things |
| C-stick | Scroll |
| L / R | Switch tabs (move the text cursor when typing, pen size when drawing) |
| X | Yeah! (delete when typing, eraser when drawing) |
| Y | New post or comment (space when typing) |
| Z | The poster's page (shift when typing, undo when drawing) |
| START | Settings on the home screen, post when writing or drawing |

## Building

CubeVerse builds with devkitPPC and libogc2. The easiest way is GitHub Actions (see `.github/workflows`), or with Docker:

```
sh get-deps.sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
```

Copy `cubeverse.dol` and the `sd/cubeverse` folder to your SD card.

The `pc` folder has a test build that runs CubeVerse on a PC against a stand-in for Juxt (`pc/server.py`), for screenshots.

## Thanks

To the Pretendo Network and Juxtaposition teams. CubeVerse isn't affiliated with Nintendo.

Japanese text uses M PLUS 1p and Noto Sans CJK JP (SIL Open Font License). Every sound and the music are CubeVerse's own, made from scratch.
