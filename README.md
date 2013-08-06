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

The plugin is controlled from within the message window.
All commands are prefixed by `/tts`. For example

    /tts on
    /tts off

will globally turn the plugin on/off.
The `buddy on/off` commands allow to control only the current conversation.

The `status` command logs the current status into the conversation window.

You might also be interested in controlling the language and volume

    /tts volume 100
    /tts lang de

Note that the volume must be a number between 0 and 200.
You can find possible values for the language by typing `espeak --voices` in your shell.

Advanced configuration can be understood by looking at the source code.
