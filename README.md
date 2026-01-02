# Tilted Online (Custom Fork)
![Build status](https://github.com/jacobwasbeast/TiltedEvolution/workflows/Build%20windows/badge.svg?branch=master) [![Build linux](https://github.com/jacobwasbeast/TiltedEvolution/actions/workflows/linux.yml/badge.svg)](https://github.com/jacobwasbeast/TiltedEvolution/actions/workflows/linux.yml) 


**Tilted Online** is a framework created to enable multiplayer in Bethesda games, currently supporting **Skyrim Special Edition**.
To play the original Tilted Online, go to the [nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/69993) or the [github](https://github.com/tiltedphoques/TiltedEvolution)
> **Fork Notice:** This repository is a heavily modified fork maintained by **Jacobwasbeast**. It introduces entirely new systems (Trading, Auth, Database Sync) not present in the original codebase. This fork is intended for custom server implementation and is **not** intended to be merged back into the upstream TiltedPhoques repository.

## Exclusive Features & Overhauls

This fork fundamentally changes several core systems to support a more persistent and cooperative MMO-lite experience.

### Economy & Item Persistence

  * **Player-to-Player Trading:** A brand new, secure trading interface allowing players to exchange items directly.
  * **Server-Sided Item Drops:**
      * *Original Behavior:* Item drops were synced loosely by clients within a specific cell.
      * *Fork Behavior:* Item drops are now **Database-Backed and Server-Authoritative**, ensuring items remain persistent and synced regardless of client cell states.

### Combat & Revive System

  * **Party & In-Place Resurrection:**
      * Implemented a downed state mechanics.
      * Players can be revived in-place by **Party Members**, encouraging cooperative play.
  * **Visual Indicators:** Dead/Downed party members now **glow**, making it easier to locate teammates in chaotic fights for a revive.

### Navigation & Social

  * **TPA (Teleport Ask) System:** Replaced the immediate teleport command with a request-based system (Request -\> Accept/Deny), preventing sudden crashes and uninvited teleportation.
  * **Advanced Map Markers:**
      * Added **Party Icons** to the map for easier tracking.
  * **Identity System:**
      * **Custom Login:** Replaced the standard connection flow with a **Username & Password** system (securely hashed).
      * **Profile Pictures:** Added support for user avatars/profile pictures in nametags and UI.

### Stability & Crash Fixes

  * **Crash Fixes:** Major stability improvements regarding inventory manipulation, quest updates, and cell loading.
  * **Sync Improvements:** Fixed equipment desync upon respawning.
  * **Security:** Console commands are disabled while connected to the server to prevent client-side abuse.

-----

## Getting started

To use this specific version, you must build from the source provided in this repository.

For general information regarding the underlying framework, visit the [Tilted Online Wiki](https://wiki.tiltedphoques.com/tilted-online/).

Check out the [build guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide) for setup and development info on the project.

## Reporting bugs

If you encounter bugs specific to these custom features (Trading, TPA, Login, Reviving), please report them in the **Issues** tab of **this repository**, not the original Tilted Online repository.

## Contributing

If you wish to contribute to this custom fork:

  - Check the issues tab for current tasks.
  - Fork this repository and create pull requests here.
  - Ensure you follow the existing code guidelines.

## Main project source tree

  * [**client/**](https://www.google.com/search?q=./Code/client): Sources for the SkyrimSE and FO4 clients (Modified).
  * [**immersive\_launcher/**](https://www.google.com/search?q=./Code/immersive_launcher): Game starter/updater.
  * [**common/**](https://www.google.com/search?q=./Code/common): Common code shared between plugin and server.
  * [**encoding/**](https://www.google.com/search?q=./Code/encoding): Net-message definitions.
  * [**server/**](https://www.google.com/search?q=./Code/server): GameServer implementation (Heavily Modified with DB support).
  * [**skyrim\_ui/**](https://www.google.com/search?q=./Code/skyrim_ui): Source code for the UI (Trading, Login, Nametags).
  * [**tests/**](https://www.google.com/search?q=./Code/tests): Tests for the encoding and serialization code.
  * [**tp\_process/**](https://www.google.com/search?q=./Code/tp_process): Worker for CEF (Chromium Embedded Framework) overlay.

## Some images

* **Name Tags**  
<img src="https://github.com/Jacobwasbeast/TiltedEvolution/blob/dev/Images/nametags.jpg?raw=true" width="750">

* **Death UI**  
<img src="https://github.com/Jacobwasbeast/TiltedEvolution/blob/dev/Images/death%20ui.png?raw=true" width="750">

* **Trading UI**  
<img src="https://github.com/Jacobwasbeast/TiltedEvolution/blob/dev/Images/trading%20ui.jpg?raw=true" width="750">

* **Party Markers**  
<img src="https://github.com/Jacobwasbeast/TiltedEvolution/blob/dev/Images/party%20markers.jpg?raw=true" width="750">

## License
[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

Tilted Online is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
