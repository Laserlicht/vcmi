# Installation on Switch

## Prerequisites

- A Switch console set up to run homebrew software.
- VCMI requires original *Heroes of Might and Magic III: Shadow of Death* or *Complete* data files.

## Step 1: Copy VCMI to your SD card

Copy `vcmiclient.nro` onto your SD card, for example to:

```text
sdmc:/switch/vcmi/vcmiclient.nro
```

## Step 2: Copy Heroes III data files

Copy these folders from your Heroes III installation onto the SD card:

```text
sdmc:/switch/vcmi/Data/
sdmc:/switch/vcmi/Maps/
sdmc:/switch/vcmi/Mp3/
```

## Step 3: Launch VCMI

Starting VCMI directly from the plain Homebrew Launcher only gives it a small amount of memory, which is not enough to run the game — it will crash or fail to load. VCMI needs to be started in **title override** mode instead, so it gets the full memory a game normally has:

1. From the Switch Home Menu, hold **R** and select any installed game to launch the Homebrew Launcher in title override mode.
2. Select **VCMI** from the list.

If you don't want to hold **R** every time, you can instead install a **forwarder**, so it launches directly from its own icon on the Home Menu with full memory.

Saves, settings, logs and mods are all stored under `sdmc:/switch/vcmi/`.

## Controls

- Joy-Con / Pro Controller and the touchscreen both work out of the box.
- Tapping a text field (hero name, save name, chat) opens the on-screen keyboard.

## Reporting bugs

Please report issues on [GitHub](https://github.com/vcmi/vcmi/issues).
