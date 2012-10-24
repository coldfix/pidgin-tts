## pidgin-tts

### Description

This is a plugin for Pidgin-IM which can be used to automatically read incoming messages via an installed TTS (text-to-speech) application.

### Features

### Limitations

The program

* is tested on Linux only (and will probably not compile elsewhere)
* is designed for use with the `espeak` program. I don't know which other TTS applications it may support

### Installation

Build and install the plugin with the commands

    make && make install

This will compile the code and - in a second step - copy generated shared object to your `~/.purple/plugins/` directory.
Afterwards you have to enable the plugin in your Pidgin options.

Make sure to have the `espeak` utility installed on your system.

### Commands

